/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dinference.h"

#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/gva_tensor_meta.h>
#include <nlohmann/json.hpp>
#include <openvino/openvino.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_inference_debug);
#define GST_CAT_DEFAULT gst_g3d_inference_debug

using json = nlohmann::json;

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_DEVICE,
    PROP_MODEL_TYPE,
    PROP_SCORE_THRESHOLD,
};

namespace {

constexpr const char *DEFAULT_DEVICE = "CPU";
constexpr const char *DEFAULT_MODEL_TYPE = "pointpillars";
constexpr float DEFAULT_SCORE_THRESHOLD = -1.0f;
constexpr size_t POINT_SIZE = 4;
constexpr size_t DETECTION_WIDTH = 9;

class PointPillarsRuntime {
  public:
    void load(const std::string &config_path, const std::string &device) {
        std::ifstream stream(config_path);
        if (!stream)
            throw std::runtime_error("Failed to open config: " + config_path);

        json config_json;
        stream >> config_json;

        const std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
        auto resolve_path = [&config_dir](const std::string &path_str) {
            const std::filesystem::path path(path_str);
            if (path.is_absolute())
                return path.lexically_normal().string();
            return (config_dir / path).lexically_normal().string();
        };

        _extension_lib = resolve_path(config_json.at("extension_lib").get<std::string>());
        _voxel_model_path = resolve_path(config_json.at("voxel_model").get<std::string>());
        _nn_model_path = resolve_path(config_json.at("nn_model").get<std::string>());
        _postproc_model_path = resolve_path(config_json.at("postproc_model").get<std::string>());
        _device = device;

        _core.add_extension(_extension_lib);
        _compiled_voxel = _core.compile_model(_core.read_model(_voxel_model_path), "CPU");
        _compiled_nn = _core.compile_model(_core.read_model(_nn_model_path), _device);
        _compiled_postproc = _core.compile_model(_core.read_model(_postproc_model_path), "CPU");

        _voxel_request = _compiled_voxel.create_infer_request();
        _nn_request = _compiled_nn.create_infer_request();
        _postproc_request = _compiled_postproc.create_infer_request();
    }

    std::vector<float> infer(const float *points, size_t point_count, float score_threshold) {
        ov::Tensor points_tensor(ov::element::f32, ov::Shape{point_count, POINT_SIZE}, const_cast<float *>(points));
        _voxel_request.set_input_tensor(0, points_tensor);
        _voxel_request.infer();

        _nn_request.set_input_tensor(0, _voxel_request.get_output_tensor(0));
        _nn_request.set_input_tensor(1, _voxel_request.get_output_tensor(1));
        _nn_request.set_input_tensor(2, _voxel_request.get_output_tensor(2));
        _nn_request.start_async();
        // TODO: overlap CPU work here when pipelining multiple frames (e.g., voxel for frame N+1)
        _nn_request.wait();

        _postproc_request.set_input_tensor(0, squeeze_leading_dim(_nn_request.get_output_tensor(0)));
        _postproc_request.set_input_tensor(1, squeeze_leading_dim(_nn_request.get_output_tensor(1)));
        _postproc_request.set_input_tensor(2, squeeze_leading_dim(_nn_request.get_output_tensor(2)));
        _postproc_request.infer();

        return collect_detections(_postproc_request.get_output_tensor(0), _postproc_request.get_output_tensor(1),
                                  _postproc_request.get_output_tensor(2), score_threshold);
    }

  private:
    static ov::Tensor squeeze_leading_dim(const ov::Tensor &tensor) {
        ov::Shape shape = tensor.get_shape();
        if (!shape.empty() && shape.front() == 1) {
            ov::Shape squeezed(shape.begin() + 1, shape.end());
            return ov::Tensor(tensor.get_element_type(), squeezed, const_cast<void *>(tensor.data()));
        }
        return tensor;
    }

    static std::vector<float> tensor_to_float_vector(const ov::Tensor &tensor) {
        const size_t total = tensor.get_size();
        std::vector<float> values(total, 0.0f);

        switch (tensor.get_element_type()) {
        case ov::element::f32: {
            const float *data = tensor.data<const float>();
            std::copy(data, data + total, values.begin());
            break;
        }
        case ov::element::i32: {
            const int32_t *data = tensor.data<const int32_t>();
            std::transform(data, data + total, values.begin(), [](int32_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::i64: {
            const int64_t *data = tensor.data<const int64_t>();
            std::transform(data, data + total, values.begin(), [](int64_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::u32: {
            const uint32_t *data = tensor.data<const uint32_t>();
            std::transform(data, data + total, values.begin(), [](uint32_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::u64: {
            const uint64_t *data = tensor.data<const uint64_t>();
            std::transform(data, data + total, values.begin(), [](uint64_t v) { return static_cast<float>(v); });
            break;
        }
        default:
            throw std::runtime_error("Unsupported output tensor element type");
        }

        return values;
    }

    static std::vector<float> collect_detections(const ov::Tensor &bboxes, const ov::Tensor &labels,
                                                 const ov::Tensor &scores, float score_threshold) {
        const ov::Shape bbox_shape = bboxes.get_shape();
        if (bbox_shape.size() != 2 || bbox_shape[1] != 7)
            throw std::runtime_error("Unexpected bbox tensor shape");

        const size_t bbox_count = bbox_shape[0];
        const float *bbox_data = bboxes.data<const float>();
        const std::vector<float> label_data = tensor_to_float_vector(labels);
        const std::vector<float> score_data = tensor_to_float_vector(scores);
        const size_t count = std::min({bbox_count, label_data.size(), score_data.size()});

        std::vector<float> flattened;
        flattened.reserve(count * DETECTION_WIDTH);
        for (size_t index = 0; index < count; ++index) {
            if (score_threshold >= 0.0f && score_data[index] < score_threshold)
                continue;

            const float *bbox = bbox_data + index * 7;
            flattened.insert(flattened.end(), bbox, bbox + 7);
            flattened.push_back(score_data[index]);
            flattened.push_back(label_data[index]);
        }
        return flattened;
    }

    ov::Core _core;
    ov::CompiledModel _compiled_voxel;
    ov::CompiledModel _compiled_nn;
    ov::CompiledModel _compiled_postproc;
    ov::InferRequest _voxel_request;
    ov::InferRequest _nn_request;
    ov::InferRequest _postproc_request;
    std::string _device;
    std::string _extension_lib;
    std::string _voxel_model_path;
    std::string _nn_model_path;
    std::string _postproc_model_path;
};

GValueArray *vector_to_gvalue_array(const std::vector<guint> &values) {
    GValueArray *array = g_value_array_new(values.size());
    GValue gvalue = G_VALUE_INIT;
    g_value_init(&gvalue, G_TYPE_UINT);
    for (guint value : values) {
        g_value_set_uint(&gvalue, value);
        g_value_array_append(array, &gvalue);
    }
    return array;
}

void copy_buffer_to_structure(GstStructure *structure, const std::vector<float> &buffer) {
    if (buffer.empty())
        return;

    GVariant *variant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, buffer.data(), buffer.size() * sizeof(float), 1);
    gsize nbytes = 0;
    gst_structure_set(structure, "data_buffer", G_TYPE_VARIANT, variant, "data", G_TYPE_POINTER,
                      g_variant_get_fixed_array(variant, &nbytes, 1), NULL);
}

LidarMeta *get_lidar_meta(GstBuffer *buffer) {
    return reinterpret_cast<LidarMeta *>(gst_buffer_get_meta(buffer, LIDAR_META_API_TYPE));
}

PointPillarsRuntime *get_runtime(GstG3DInference *filter) {
    return reinterpret_cast<PointPillarsRuntime *>(filter->runtime);
}

void set_tensor_metadata(GstGVATensorMeta *tensor_meta, const std::vector<float> &detections,
                         const char *model_type) {
    gst_structure_set_name(tensor_meta->data, "detection");
    gst_structure_set(tensor_meta->data, "element_id", G_TYPE_STRING, "g3dinference", "model_name", G_TYPE_STRING,
                      model_type ? model_type : DEFAULT_MODEL_TYPE, "layer_name", G_TYPE_STRING,
                      "pointpillars_3d_detection", "format",
                      G_TYPE_STRING, "pointpillars_3d", "precision", G_TYPE_INT, GVA_PRECISION_FP32, "layout",
                      G_TYPE_INT, GVA_LAYOUT_NC, "rank", G_TYPE_INT, 2, NULL);

    const std::vector<guint> dims = {static_cast<guint>(detections.size() / DETECTION_WIDTH),
                                     static_cast<guint>(DETECTION_WIDTH)};
    GValueArray *array = vector_to_gvalue_array(dims);
    gst_structure_set_array(tensor_meta->data, "dims", array);
    g_value_array_free(array);

    copy_buffer_to_structure(tensor_meta->data, detections);
}

} // namespace

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

static void gst_g3d_inference_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_inference_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_g3d_inference_finalize(GObject *object);
static gboolean gst_g3d_inference_start(GstBaseTransform *trans);
static gboolean gst_g3d_inference_stop(GstBaseTransform *trans);
static GstFlowReturn gst_g3d_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static GstCaps *gst_g3d_inference_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter);

G_DEFINE_TYPE(GstG3DInference, gst_g3d_inference, GST_TYPE_BASE_TRANSFORM);

static void gst_g3d_inference_class_init(GstG3DInferenceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_g3d_inference_debug, "g3dinference", 0, "3D LiDAR inference element");

    gobject_class->set_property = gst_g3d_inference_set_property;
    gobject_class->get_property = gst_g3d_inference_get_property;
    gobject_class->finalize = gst_g3d_inference_finalize;

    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_string("config", "Config",
                                                        "Path to PointPillars OpenVINO JSON configuration", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_DEVICE,
                                    g_param_spec_string("device", "Device", "OpenVINO device for NN model",
                                                        DEFAULT_DEVICE,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_MODEL_TYPE,
                                    g_param_spec_string("model-type", "Model Type", "3D detector model type",
                                                        DEFAULT_MODEL_TYPE,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SCORE_THRESHOLD,
        g_param_spec_float("score-threshold", "Score Threshold",
                           "Drop detections below this score (-1 keeps postproc output unchanged)", -1.0, 1.0,
                           DEFAULT_SCORE_THRESHOLD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        gstelement_class, "G3D Inference", "Filter/Analyzer",
        "Runs PointPillars inference on LiDAR point clouds and attaches tensor metadata", "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_g3d_inference_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_inference_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_g3d_inference_transform_ip);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_g3d_inference_transform_caps);
}

static void gst_g3d_inference_init(GstG3DInference *filter) {
    filter->config = NULL;
    filter->device = g_strdup(DEFAULT_DEVICE);
    filter->model_type = g_strdup(DEFAULT_MODEL_TYPE);
    filter->score_threshold = DEFAULT_SCORE_THRESHOLD;
    filter->initialized = FALSE;
    filter->runtime = NULL;

    g_mutex_init(&filter->mutex);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(filter), TRUE);
}

static void gst_g3d_inference_finalize(GObject *object) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    delete get_runtime(filter);
    filter->runtime = NULL;

    g_clear_pointer(&filter->config, g_free);
    g_clear_pointer(&filter->device, g_free);
    g_clear_pointer(&filter->model_type, g_free);
    g_mutex_clear(&filter->mutex);

    G_OBJECT_CLASS(gst_g3d_inference_parent_class)->finalize(object);
}

static void gst_g3d_inference_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_free(filter->config);
        filter->config = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(filter->device);
        filter->device = g_value_dup_string(value);
        break;
    case PROP_MODEL_TYPE:
        g_free(filter->model_type);
        filter->model_type = g_value_dup_string(value);
        break;
    case PROP_SCORE_THRESHOLD:
        filter->score_threshold = g_value_get_float(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_g3d_inference_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_string(value, filter->config);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, filter->device);
        break;
    case PROP_MODEL_TYPE:
        g_value_set_string(value, filter->model_type);
        break;
    case PROP_SCORE_THRESHOLD:
        g_value_set_float(value, filter->score_threshold);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_g3d_inference_start(GstBaseTransform *trans) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);

    if (!filter->config || !*filter->config) {
        GST_ERROR_OBJECT(filter, "Property 'config' is required");
        return FALSE;
    }

    if (g_ascii_strcasecmp(filter->model_type, DEFAULT_MODEL_TYPE) != 0) {
        GST_ERROR_OBJECT(filter, "Unsupported model type: %s", filter->model_type);
        return FALSE;
    }

    try {
        auto *runtime = new PointPillarsRuntime();
        runtime->load(filter->config, filter->device ? filter->device : DEFAULT_DEVICE);
        delete get_runtime(filter);
        filter->runtime = runtime;
        filter->initialized = TRUE;
        GST_INFO_OBJECT(filter, "Loaded PointPillars runtime with config=%s device=%s", filter->config,
                        filter->device ? filter->device : DEFAULT_DEVICE);
        return TRUE;
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(filter, "Failed to initialize PointPillars runtime: %s", e.what());
        delete get_runtime(filter);
        filter->runtime = NULL;
        filter->initialized = FALSE;
        return FALSE;
    }
}

static gboolean gst_g3d_inference_stop(GstBaseTransform *trans) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);
    delete get_runtime(filter);
    filter->runtime = NULL;
    filter->initialized = FALSE;
    return TRUE;
}

static GstFlowReturn gst_g3d_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buffer) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);
    g_mutex_lock(&filter->mutex);

    try {
        if (!filter->initialized || !get_runtime(filter))
            throw std::runtime_error("Runtime is not initialized");

        LidarMeta *lidar_meta = get_lidar_meta(buffer);
        if (!lidar_meta)
            throw std::runtime_error("LidarMeta is missing from input buffer");

        GstMapInfo map_info;
        if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ))
            throw std::runtime_error("Failed to map input buffer");

        const gsize expected_size = static_cast<gsize>(lidar_meta->lidar_point_count) * POINT_SIZE * sizeof(float);
        if (map_info.size != expected_size) {
            gst_buffer_unmap(buffer, &map_info);
            throw std::runtime_error("Input payload size does not match LidarMeta point count");
        }

        const float *points = reinterpret_cast<const float *>(map_info.data);
        std::vector<float> detections =
            get_runtime(filter)->infer(points, lidar_meta->lidar_point_count, filter->score_threshold);
        gst_buffer_unmap(buffer, &map_info);

        GstGVATensorMeta *tensor_meta = GST_GVA_TENSOR_META_ADD(buffer);
        if (!tensor_meta || !tensor_meta->data)
            throw std::runtime_error("Failed to allocate GstGVATensorMeta");

        set_tensor_metadata(tensor_meta, detections, filter->model_type);

        GST_DEBUG_OBJECT(filter, "Attached PointPillars tensor with %zu detections for frame_id=%zu",
                         detections.size() / DETECTION_WIDTH, lidar_meta->frame_id);
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_OK;
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(filter, "Failed to process LiDAR buffer: %s", e.what());
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_ERROR;
    }
}

static GstCaps *gst_g3d_inference_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter_caps) {
    (void)trans;
    (void)caps;
    (void)filter_caps;

    if (direction == GST_PAD_SINK)
        return gst_caps_from_string("application/x-lidar");

    return gst_caps_from_string("application/x-lidar");
}