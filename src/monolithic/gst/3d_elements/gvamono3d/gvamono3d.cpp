/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvamono3d.h"

#include "gmutex_lock_guard.h"

#include <dlstreamer/gst/metadata/camera_3d_od_mtd.h>
#include <gst/analytics/analytics.h>

#define GST_USE_UNSTABLE_API
#include <gst/va/gstvaallocator.h>
#include <gst/va/gstvadisplay.h>
#include <gst/va/gstvautils.h>
#include <va/va.h>

#define CL_TARGET_OPENCL_VERSION 300
#define OV_GPU_USE_OPENCL_HPP
#include <openvino/core/preprocess/pre_post_process.hpp>
#include <openvino/openvino.hpp>
#include <openvino/runtime/intel_gpu/ocl/va.hpp>
#include <openvino/runtime/intel_gpu/remote_properties.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(gst_gva_mono3d_debug);
#define GST_CAT_DEFAULT gst_gva_mono3d_debug

enum {
    PROP_0,
    PROP_MODEL,
    PROP_DEVICE,
    PROP_CALIBRATION_FILE,
    PROP_THRESHOLD,
    PROP_TOPK,
};

namespace {

constexpr const char *DEFAULT_DEVICE = "CPU";
constexpr gfloat DEFAULT_THRESHOLD = -1.0f; /* negative -> use model rt_info default */
constexpr gfloat FALLBACK_THRESHOLD = 0.2f;
constexpr guint DEFAULT_TOPK = 50;

const std::vector<std::string> FALLBACK_LABELS = {"Pedestrian", "Car", "Cyclist"};

/* Parse a whitespace/comma-separated list of floats from an rt_info string. */
std::vector<float> parse_floats(const std::string &text) {
    std::vector<float> out;
    std::string token;
    std::istringstream stream(text);
    while (stream >> token) {
        for (char &c : token) {
            if (c == ',')
                c = ' ';
        }
        std::istringstream inner(token);
        float value = 0.0f;
        while (inner >> value)
            out.push_back(value);
    }
    return out;
}

std::vector<std::string> split_tokens(const std::string &text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token)
        out.push_back(token);
    return out;
}

std::string any_to_string(const ov::Any &value) {
    try {
        return value.as<std::string>();
    } catch (...) {
    }
    try {
        return std::to_string(value.as<double>());
    } catch (...) {
    }
    return "";
}

/* Raw MonoDETR heads for one frame (batch dimension stripped). Layouts:
 * logits [Q, num_classes], boxes [Q, 6] (cxcylrtb), dim3d [Q, 3] (h,w,l residual),
 * depth [Q, 2] (depth, sigma), angle [Q, 24] (12 heading bins + 12 residuals). */
struct MonoDetrOutputs {
    std::vector<float> logits;
    std::vector<float> boxes;
    std::vector<float> dim3d;
    std::vector<float> depth;
    std::vector<float> angle;
    size_t num_queries = 0;
    size_t num_classes = 0;
};

/* Loads a monocular 3D detection IR (e.g. MonoDETR) and runs it directly through
 * OpenVINO. The IR takes three inputs (image, calibration P2, image size) and
 * emits five raw heads; this runtime keeps the model + infer request and the
 * preprocessing/decoding parameters read from the IR's rt_info/model_info. */
class MonoDetrRuntime {
  public:
    /* Reads the IR, its rt_info and the calibration; identifies the three inputs and pins
     * batch=1. Compilation is deferred to compile_system()/compile_surface() because the
     * chosen path depends on the negotiated caps (system BGR vs VAAPI NV12 surface). */
    void load(const std::string &model_path, const std::string &calibration_file) {
        _model = _core.read_model(model_path);

        read_model_info(_model);

        if (_model->inputs().size() != 3)
            throw std::runtime_error("Monocular 3D model must have exactly 3 inputs (image, calib, image size), got " +
                                     std::to_string(_model->inputs().size()));

        /* Identify the three inputs by rank: image=NCHW(4), calib=[1,3,4](3), image size=[1,2](2). */
        for (size_t i = 0; i < _model->inputs().size(); ++i) {
            const ov::Output<ov::Node> in = _model->input(i);
            const size_t rank = in.get_partial_shape().rank().get_length();
            const std::string name = in.get_any_name();
            if (rank == 4) {
                _image_input_name = name;
                _image_input_index = i;
            } else if (rank == 3) {
                _calib_input_name = name;
            } else if (rank == 2) {
                _imgsize_input_name = name;
            }
        }
        if (_image_input_name.empty() || _calib_input_name.empty() || _imgsize_input_name.empty())
            throw std::runtime_error("Could not identify image/calib/image-size inputs by rank");

        /* Pin batch=1; the export already fixes every non-batch dim. */
        std::map<ov::Output<ov::Node>, ov::PartialShape> shapes;
        for (const ov::Output<ov::Node> &input : _model->inputs()) {
            ov::PartialShape ps = input.get_partial_shape();
            if (ps.rank().is_static() && ps.size() > 0)
                ps[0] = 1;
            shapes[input] = ps;
        }
        _model->reshape(shapes);

        const ov::Shape image_shape = _model->input(_image_input_index).get_shape();
        if (image_shape.size() != 4)
            throw std::runtime_error("Image input must be NCHW");
        _net_height = image_shape[2];
        _net_width = image_shape[3];

        _p2 = load_p2(calibration_file);
    }

    /* System-memory path: the element feeds packed BGR host frames. */
    void compile_system(const std::string &device) {
        std::shared_ptr<ov::Model> model = _model->clone();
        configure_image_preproc(model);
        /* ACCURACY keeps the backbone in f32 (its layer4 activations overflow
         * f16); the default PERFORMANCE mode forces f16 everywhere and yields
         * NaNs on GPU. */
        _compiled = _core.compile_model(model, device, ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        _request = _compiled.create_infer_request();
        _device = device;
        _surface_mode = false;
    }

    /* VAAPI zero-copy path: the element feeds NV12 GPU surfaces; OpenVINO shares the surface,
     * converts colour, resizes and normalises on the GPU. Requires a GPU device + VADisplay. */
    void compile_surface(const std::string &device, void *va_display, size_t surf_width, size_t surf_height) {
        std::shared_ptr<ov::Model> model = _model->clone();
        configure_image_preproc_surface(model, surf_width, surf_height);

        ov::intel_gpu::ocl::VAContext va_context(_core, va_display, 0);
        _remote_context = va_context;
        _compiled =
            _core.compile_model(model, va_context, ov::hint::execution_mode(ov::hint::ExecutionMode::ACCURACY));
        _request = _compiled.create_infer_request();
        _device = device;
        _surface_mode = true;

        /* The NV12 split turns the single image input into two ("y"/"uv"); capture their ports.
         * Distinguish them from the calib/imgsize inputs (unchanged) and from each other by the
         * channel count (NHWC last dim: y=1, uv=2). */
        _y_port = ov::Output<const ov::Node>();
        _uv_port = ov::Output<const ov::Node>();
        for (const ov::Output<const ov::Node> &in : _compiled.inputs()) {
            const std::unordered_set<std::string> &names = in.get_names();
            if (names.count(_calib_input_name) || names.count(_imgsize_input_name))
                continue;
            const ov::PartialShape &ps = in.get_partial_shape();
            const size_t rank = ps.rank().is_static() ? ps.size() : 0;
            bool is_uv = names.count("uv") > 0;
            bool is_y = names.count("y") > 0;
            if (!is_uv && !is_y && rank == 4 && ps[rank - 1].is_static()) {
                const int64_t channels = ps[rank - 1].get_length();
                is_uv = (channels == 2);
                is_y = (channels == 1);
            }
            if (is_uv)
                _uv_port = in;
            else if (is_y)
                _y_port = in;
        }
        if (_y_port.get_node() == nullptr || _uv_port.get_node() == nullptr)
            throw std::runtime_error("Could not identify NV12 y/uv inputs after preprocessing");
    }

    bool is_surface_mode() const {
        return _surface_mode;
    }

    /* Runs inference on one BGR frame. calib (P2) and image size are fed as the
     * two auxiliary inputs; the image is preprocessed by the model's own PPP. */
    MonoDetrOutputs infer(const uint8_t *bgr, size_t width, size_t height, size_t row_stride, float orig_width,
                          float orig_height) {
        const size_t dense_stride = width * 3;
        ov::Tensor image_tensor;
        if (row_stride == dense_stride) {
            image_tensor = ov::Tensor(ov::element::u8, {1, height, width, 3}, const_cast<uint8_t *>(bgr));
        } else {
            /* The GPU plugin ignores per-row padding on host tensors (produces a sheared image);
             * pack into a dense buffer. On CPU the strided view would work but this is cheap. */
            _dense_bgr.resize(height * dense_stride);
            for (size_t y = 0; y < height; ++y)
                std::memcpy(_dense_bgr.data() + y * dense_stride, bgr + y * row_stride, dense_stride);
            image_tensor = ov::Tensor(ov::element::u8, {1, height, width, 3}, _dense_bgr.data());
        }
        _request.set_tensor(_image_input_name, image_tensor);

        ov::Tensor calib_tensor(ov::element::f32, {1, 3, 4}, _p2.data());
        _request.set_tensor(_calib_input_name, calib_tensor);

        float img_size[2] = {orig_width, orig_height};
        ov::Tensor imgsize_tensor(ov::element::f32, {1, 2}, img_size);
        _request.set_tensor(_imgsize_input_name, imgsize_tensor);

        _request.infer();
        return read_outputs();
    }

    /* Zero-copy inference on a VAAPI NV12 surface. surf_width/height are the surface (native)
     * dimensions; orig_width/height feed the model's image-size input for back-projection. */
    MonoDetrOutputs infer_surface(uint32_t va_surface, size_t surf_width, size_t surf_height, float orig_width,
                                  float orig_height) {
        ov::AnyMap tensor_params = {{ov::intel_gpu::shared_mem_type.name(), std::string("VA_SURFACE")},
                                    {ov::intel_gpu::dev_object_handle.name(), va_surface},
                                    {ov::intel_gpu::va_plane.name(), uint32_t(0)}};
        ov::RemoteTensor y_tensor = _remote_context.create_tensor(ov::element::u8, {1, surf_height, surf_width, 1},
                                                                  tensor_params);
        tensor_params[ov::intel_gpu::va_plane.name()] = uint32_t(1);
        ov::RemoteTensor uv_tensor =
            _remote_context.create_tensor(ov::element::u8, {1, surf_height / 2, surf_width / 2, 2}, tensor_params);

        _request.set_tensor(_y_port, y_tensor);
        _request.set_tensor(_uv_port, uv_tensor);

        ov::Tensor calib_tensor(ov::element::f32, {1, 3, 4}, _p2.data());
        _request.set_tensor(_calib_input_name, calib_tensor);

        float img_size[2] = {orig_width, orig_height};
        ov::Tensor imgsize_tensor(ov::element::f32, {1, 2}, img_size);
        _request.set_tensor(_imgsize_input_name, imgsize_tensor);

        _request.infer();
        return read_outputs();
    }

    size_t net_width() const {
        return _net_width;
    }
    size_t net_height() const {
        return _net_height;
    }
    const std::string &model_type() const {
        return _model_type;
    }
    const std::vector<std::string> &labels() const {
        return _labels;
    }
    float default_threshold() const {
        return _default_threshold;
    }
    const std::array<float, 12> &p2() const {
        return _p2;
    }
    const std::vector<float> &mean() const {
        return _mean;
    }
    const std::vector<float> &scale() const {
        return _scale;
    }
    bool reverse_input_channels() const {
        return _reverse_input_channels;
    }

  private:
    MonoDetrOutputs read_outputs() {
        MonoDetrOutputs out;
        const ov::Tensor logits = _request.get_output_tensor(0);
        const ov::Shape ls = logits.get_shape();
        out.num_queries = ls.size() >= 2 ? ls[1] : 0;
        out.num_classes = ls.size() >= 3 ? ls[2] : 0;
        out.logits = to_f32(logits);
        out.boxes = to_f32(_request.get_output_tensor(1));
        out.dim3d = to_f32(_request.get_output_tensor(2));
        out.depth = to_f32(_request.get_output_tensor(3));
        out.angle = to_f32(_request.get_output_tensor(4));
        return out;
    }

    void configure_image_preproc(std::shared_ptr<ov::Model> &model) {
        ov::preprocess::PrePostProcessor ppp(model);
        ov::preprocess::InputInfo &in = ppp.input(_image_input_name);

        /* The element accepts packed BGR; OpenVINO does color convert, resize to
         * the network size, element-type convert and mean/scale on device. */
        in.tensor()
            .set_element_type(ov::element::u8)
            .set_layout("NHWC")
            .set_color_format(ov::preprocess::ColorFormat::BGR)
            .set_spatial_dynamic_shape();

        ov::preprocess::PreProcessSteps &pre = in.preprocess();
        if (_reverse_input_channels)
            pre.convert_color(ov::preprocess::ColorFormat::RGB);
        pre.resize(ov::preprocess::ResizeAlgorithm::RESIZE_LINEAR);
        pre.convert_element_type(ov::element::f32);
        if (_mean.size() == 1)
            pre.mean(_mean[0]);
        else if (_mean.size() > 1)
            pre.mean(_mean);
        if (_scale.size() == 1)
            pre.scale(_scale[0]);
        else if (_scale.size() > 1)
            pre.scale(_scale);

        in.model().set_layout("NCHW");

        // The calib (P2) and image-size inputs feed the f16 head; declare them as f32 user
        // tensors so the PPP inserts a conversion to whatever precision the model expects.
        ppp.input(_calib_input_name).tensor().set_element_type(ov::element::f32);
        ppp.input(_imgsize_input_name).tensor().set_element_type(ov::element::f32);

        model = ppp.build();
    }

    /* NV12 surface-sharing variant of the preprocessing: the input is a two-plane VAAPI
     * surface at native resolution; OpenVINO converts colour, resizes to the network size and
     * normalises entirely on the GPU (zero host copy). */
    void configure_image_preproc_surface(std::shared_ptr<ov::Model> &model, size_t surf_width, size_t surf_height) {
        ov::preprocess::PrePostProcessor ppp(model);
        ov::preprocess::InputInfo &in = ppp.input(_image_input_name);

        in.tensor()
            .set_element_type(ov::element::u8)
            .set_color_format(ov::preprocess::ColorFormat::NV12_TWO_PLANES, {"y", "uv"})
            .set_memory_type(ov::intel_gpu::memory_type::surface)
            .set_layout("NHWC")
            .set_spatial_static_shape(surf_height, surf_width);

        ov::preprocess::PreProcessSteps &pre = in.preprocess();
        // NV12 carries the original RGB image; convert to whatever the model consumes.
        pre.convert_color(_reverse_input_channels ? ov::preprocess::ColorFormat::RGB
                                                  : ov::preprocess::ColorFormat::BGR);
        pre.resize(ov::preprocess::ResizeAlgorithm::RESIZE_LINEAR);
        pre.convert_element_type(ov::element::f32);
        if (_mean.size() == 1)
            pre.mean(_mean[0]);
        else if (_mean.size() > 1)
            pre.mean(_mean);
        if (_scale.size() == 1)
            pre.scale(_scale[0]);
        else if (_scale.size() > 1)
            pre.scale(_scale);

        in.model().set_layout("NCHW");

        ppp.input(_calib_input_name).tensor().set_element_type(ov::element::f32);
        ppp.input(_imgsize_input_name).tensor().set_element_type(ov::element::f32);

        model = ppp.build();
    }

    static std::vector<float> to_f32(const ov::Tensor &tensor) {
        const size_t n = tensor.get_size();
        std::vector<float> values(n);
        const ov::element::Type et = tensor.get_element_type();
        if (et == ov::element::f32) {
            const float *data = tensor.data<const float>();
            std::copy(data, data + n, values.begin());
        } else if (et == ov::element::f16) {
            const ov::float16 *data = tensor.data<const ov::float16>();
            for (size_t i = 0; i < n; ++i)
                values[i] = static_cast<float>(data[i]);
        } else {
            throw std::runtime_error("Unexpected output tensor element type");
        }
        return values;
    }

    static std::array<float, 12> load_p2(const std::string &path) {
        if (path.empty())
            throw std::runtime_error("Property 'calibration-file' is required");

        std::array<float, 12> p2 = {};
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".json") == 0) {
            std::ifstream stream(path);
            if (!stream)
                throw std::runtime_error("Failed to open calibration file: " + path);
            json data;
            stream >> data;
            const json &k = data.at("intrinsic_matrix");
            /* JSON gives a 3x3 intrinsic; the camera is the origin of its own
             * rectified frame, so P2 = [K | 0]. */
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    p2[r * 4 + c] = k.at(r).at(c).get<float>();
            return p2;
        }

        std::ifstream stream(path);
        if (!stream)
            throw std::runtime_error("Failed to open calibration file: " + path);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("P2:", 0) == 0) {
                std::istringstream values(line.substr(3));
                for (int i = 0; i < 12; ++i)
                    if (!(values >> p2[i]))
                        throw std::runtime_error("Malformed P2 row in calibration file: " + path);
                return p2;
            }
        }
        throw std::runtime_error("P2 matrix not found in calibration file: " + path);
    }

    void read_model_info(const std::shared_ptr<ov::Model> &model) {
        ov::AnyMap model_info;
        if (model->has_rt_info({"model_info"}))
            model_info = model->get_rt_info<ov::AnyMap>("model_info");

        auto get = [&model_info](const char *key) -> std::string {
            auto it = model_info.find(key);
            return it == model_info.end() ? std::string() : any_to_string(it->second);
        };

        _model_type = get("model_type");
        _labels = split_tokens(get("labels"));
        if (_labels.empty())
            _labels = FALLBACK_LABELS;
        // OpenVINO model_info uses *_values keys; fall back to the short names.
        _mean = parse_floats(get("mean_values"));
        if (_mean.empty())
            _mean = parse_floats(get("mean"));
        _scale = parse_floats(get("scale_values"));
        if (_scale.empty())
            _scale = parse_floats(get("scale"));
        const std::string reverse = get("reverse_input_channels");
        _reverse_input_channels = (reverse == "True" || reverse == "true" || reverse == "1");
        const std::vector<float> thr = parse_floats(get("confidence_threshold"));
        _default_threshold = thr.empty() ? FALLBACK_THRESHOLD : thr.front();
    }

    ov::Core _core;
    std::shared_ptr<ov::Model> _model;
    ov::CompiledModel _compiled;
    ov::InferRequest _request;
    ov::RemoteContext _remote_context;
    ov::Output<const ov::Node> _y_port;
    ov::Output<const ov::Node> _uv_port;
    bool _surface_mode = false;
    std::vector<uint8_t> _dense_bgr;
    std::string _device;

    std::string _image_input_name;
    std::string _calib_input_name;
    std::string _imgsize_input_name;
    size_t _image_input_index = 0;
    std::array<float, 12> _p2 = {};

    size_t _net_width = 0;
    size_t _net_height = 0;
    std::string _model_type;
    std::vector<std::string> _labels;
    std::vector<float> _mean;
    std::vector<float> _scale;
    bool _reverse_input_channels = false;
    float _default_threshold = FALLBACK_THRESHOLD;
};

/* One decoded detection in the rectified camera frame (bottom-centre anchor). */
struct Detection {
    int class_id = 0;
    float alpha = 0.0f;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f; // amodal 2D box, pixels
    float h = 0.0f, w = 0.0f, l = 0.0f;               // dimensions, metres
    float x = 0.0f, y = 0.0f, z = 0.0f;               // location, metres
    float rotation_y = 0.0f;
    float score = 0.0f;
};

constexpr float PI_F = 3.14159265358979323846f;
constexpr int NUM_HEADING_BINS = 12;

inline float sigmoidf(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

/* bin index + residual -> angle in [-pi, pi] (MonoDETR class2angle, label format). */
float class2angle(int bin, float residual) {
    const float per_class = 2.0f * PI_F / NUM_HEADING_BINS;
    float angle = bin * per_class + residual;
    if (angle > PI_F)
        angle -= 2.0f * PI_F;
    return angle;
}

/* 24 raw angle values = 12 bin logits + 12 residuals -> observation angle alpha. */
float heading_to_alpha(const float *angle24) {
    int bin = 0;
    float best = angle24[0];
    for (int i = 1; i < NUM_HEADING_BINS; ++i) {
        if (angle24[i] > best) {
            best = angle24[i];
            bin = i;
        }
    }
    return class2angle(bin, angle24[NUM_HEADING_BINS + bin]);
}

float alpha_to_rotation_y(float alpha, float u, float cu, float fu) {
    float ry = alpha + std::atan2(u - cu, fu);
    if (ry > PI_F)
        ry -= 2.0f * PI_F;
    if (ry < -PI_F)
        ry += 2.0f * PI_F;
    return ry;
}

/* Port of extract_dets_from_outputs + decode_detections (lib/helpers/decode_helper.py).
 * Selects the top-k queries by class probability, assembles the amodal 2D box, back-projects
 * the 3D centre through P2 (bottom-centre anchor), and decodes heading. img_w/img_h are the
 * original frame size (the model's img_sizes input). */
std::vector<Detection> decode_detections(const MonoDetrOutputs &o, const std::array<float, 12> &p2, float img_w,
                                         float img_h, float threshold, size_t topk) {
    std::vector<Detection> result;
    const size_t queries = o.num_queries;
    const size_t classes = o.num_classes;
    if (queries == 0 || classes == 0)
        return result;

    /* KITTI P2 (3x4, row-major) intrinsics. */
    const float fu = p2[0], cu = p2[2], tx = p2[3] / (-fu);
    const float fv = p2[5], cv = p2[6], ty = p2[7] / (-fv);

    const size_t total = queries * classes;
    std::vector<float> probs(total);
    for (size_t i = 0; i < total; ++i)
        probs[i] = sigmoidf(o.logits[i]);

    std::vector<size_t> order(total);
    std::iota(order.begin(), order.end(), size_t(0));
    const size_t k = std::min(topk, total);
    std::partial_sort(order.begin(), order.begin() + k, order.end(),
                      [&probs](size_t a, size_t b) { return probs[a] > probs[b]; });

    for (size_t n = 0; n < k; ++n) {
        const size_t id = order[n];
        const float prob = probs[id];
        if (prob < threshold)
            continue;

        const size_t q = id / classes;
        const int label = static_cast<int>(id % classes);

        /* boxes: (cx, cy, l, r, t, b) normalized -> xyxy -> centre + size. */
        const float *box = &o.boxes[q * 6];
        const float cx = box[0], cy = box[1], l = box[2], r = box[3], t = box[4], b = box[5];
        const float x1n = cx - l, y1n = cy - t, x2n = cx + r, y2n = cy + b;
        const float xs2d = (x1n + x2n) * 0.5f, ys2d = (y1n + y2n) * 0.5f;
        const float w2d = x2n - x1n, h2d = y2n - y1n;

        const float cx_px = xs2d * img_w, cy_px = ys2d * img_h;
        const float w_px = w2d * img_w, h_px = h2d * img_h;

        Detection d;
        d.class_id = label;
        d.x1 = cx_px - w_px * 0.5f;
        d.y1 = cy_px - h_px * 0.5f;
        d.x2 = cx_px + w_px * 0.5f;
        d.y2 = cy_px + h_px * 0.5f;

        const float depth = o.depth[q * 2 + 0];
        const float sigma = std::exp(-o.depth[q * 2 + 1]);
        d.h = o.dim3d[q * 3 + 0];
        d.w = o.dim3d[q * 3 + 1];
        d.l = o.dim3d[q * 3 + 2];

        /* 3D centre uses the raw box centre (cx, cy), back-projected at the predicted depth. */
        const float x3d = cx * img_w, y3d = cy * img_h;
        d.x = (x3d - cu) * depth / fu + tx;
        d.y = (y3d - cv) * depth / fv + ty + d.h * 0.5f; // rect -> bottom centre
        d.z = depth;

        d.alpha = heading_to_alpha(&o.angle[q * 24]);
        d.rotation_y = alpha_to_rotation_y(d.alpha, cx_px, cu, fu);
        d.score = prob * sigma;
        result.push_back(d);
    }
    return result;
}

MonoDetrRuntime *get_runtime(GstGvaMono3d *self) {
    return reinterpret_cast<MonoDetrRuntime *>(self->runtime);
}

} // namespace

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw(memory:VAMemory), format=(string)NV12; "
                                            "video/x-raw, format=(string)BGR"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw(memory:VAMemory), format=(string)NV12; "
                                            "video/x-raw, format=(string)BGR"));

static void gst_gva_mono3d_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_mono3d_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_mono3d_finalize(GObject *object);
static void gst_gva_mono3d_set_context(GstElement *element, GstContext *context);
static gboolean gst_gva_mono3d_query(GstBaseTransform *trans, GstPadDirection direction, GstQuery *query);
static gboolean gst_gva_mono3d_start(GstBaseTransform *trans);
static gboolean gst_gva_mono3d_stop(GstBaseTransform *trans);
static gboolean gst_gva_mono3d_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_gva_mono3d_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);

G_DEFINE_TYPE(GstGvaMono3d, gst_gva_mono3d, GST_TYPE_BASE_TRANSFORM);

static void gst_gva_mono3d_class_init(GstGvaMono3dClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_mono3d_debug, "gvamono3d", 0, "Monocular 3D detection element");

    gobject_class->set_property = gst_gva_mono3d_set_property;
    gobject_class->get_property = gst_gva_mono3d_get_property;
    gobject_class->finalize = gst_gva_mono3d_finalize;

    element_class->set_context = GST_DEBUG_FUNCPTR(gst_gva_mono3d_set_context);

    g_object_class_install_property(gobject_class, PROP_MODEL,
                                    g_param_spec_string("model", "Model", "Path to the model's OpenVINO IR (.xml)", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "OpenVINO device (e.g. CPU, GPU, GPU.<id>)", DEFAULT_DEVICE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CALIBRATION_FILE,
        g_param_spec_string("calibration-file", "Calibration File",
                            "Camera calibration: KITTI .txt (P2) or .json with intrinsic_matrix (3x3)", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_THRESHOLD,
        g_param_spec_float("threshold", "Threshold",
                           "Drop detections below this score; negative uses the model's rt_info default", -1.0, 1.0,
                           DEFAULT_THRESHOLD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_TOPK,
                                    g_param_spec_uint("topk", "Top-K", "Number of top scoring detections to decode", 1,
                                                      1000, DEFAULT_TOPK,
                                                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "Monocular 3D Object Detection", "Filter/Analyzer",
                                          "Runs monocular 3D object detection (e.g. MonoDETR) and attaches "
                                          "camera-frame 3D detection metadata",
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_mono3d_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_mono3d_stop);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_mono3d_set_caps);
    base_transform_class->query = GST_DEBUG_FUNCPTR(gst_gva_mono3d_query);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_mono3d_transform_ip);
}

static void gst_gva_mono3d_init(GstGvaMono3d *self) {
    self->model = NULL;
    self->device = g_strdup(DEFAULT_DEVICE);
    self->calibration_file = NULL;
    self->threshold = DEFAULT_THRESHOLD;
    self->topk = DEFAULT_TOPK;
    self->initialized = FALSE;
    self->calibration_sent = FALSE;
    self->use_va_surface = FALSE;
    self->compiled = FALSE;
    self->va_display = NULL;
    self->runtime = NULL;

    gst_video_info_init(&self->video_info);
    g_mutex_init(&self->mutex);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}

static void gst_gva_mono3d_finalize(GObject *object) {
    GstGvaMono3d *self = GST_GVA_MONO3D(object);

    delete get_runtime(self);
    self->runtime = NULL;

    if (self->va_display)
        gst_object_unref(GST_OBJECT(self->va_display));
    self->va_display = NULL;

    g_clear_pointer(&self->model, g_free);
    g_clear_pointer(&self->device, g_free);
    g_clear_pointer(&self->calibration_file, g_free);
    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(gst_gva_mono3d_parent_class)->finalize(object);
}

/* Receive a shared VADisplay from upstream VA elements so surfaces can be imported zero-copy. */
static void gst_gva_mono3d_set_context(GstElement *element, GstContext *context) {
    GstGvaMono3d *self = GST_GVA_MONO3D(element);
    GstVaDisplay *display = static_cast<GstVaDisplay *>(self->va_display);
    if (gst_va_handle_set_context(element, context, nullptr, &display))
        self->va_display = display;
    GST_ELEMENT_CLASS(gst_gva_mono3d_parent_class)->set_context(element, context);
}

static gboolean gst_gva_mono3d_query(GstBaseTransform *trans, GstPadDirection direction, GstQuery *query) {
    GstGvaMono3d *self = GST_GVA_MONO3D(trans);
    if (GST_QUERY_TYPE(query) == GST_QUERY_CONTEXT &&
        gst_va_handle_context_query(GST_ELEMENT(trans), query, static_cast<GstVaDisplay *>(self->va_display)))
        return TRUE;
    return GST_BASE_TRANSFORM_CLASS(gst_gva_mono3d_parent_class)->query(trans, direction, query);
}

static void gst_gva_mono3d_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaMono3d *self = GST_GVA_MONO3D(object);

    switch (prop_id) {
    case PROP_MODEL:
        g_free(self->model);
        self->model = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(self->device);
        self->device = g_value_dup_string(value);
        break;
    case PROP_CALIBRATION_FILE:
        g_free(self->calibration_file);
        self->calibration_file = g_value_dup_string(value);
        break;
    case PROP_THRESHOLD:
        self->threshold = g_value_get_float(value);
        break;
    case PROP_TOPK:
        self->topk = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_mono3d_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaMono3d *self = GST_GVA_MONO3D(object);

    switch (prop_id) {
    case PROP_MODEL:
        g_value_set_string(value, self->model);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, self->device);
        break;
    case PROP_CALIBRATION_FILE:
        g_value_set_string(value, self->calibration_file);
        break;
    case PROP_THRESHOLD:
        g_value_set_float(value, self->threshold);
        break;
    case PROP_TOPK:
        g_value_set_uint(value, self->topk);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_gva_mono3d_start(GstBaseTransform *trans) {
    GstGvaMono3d *self = GST_GVA_MONO3D(trans);

    if (!self->model || !*self->model) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("Property 'model' is required"), (nullptr));
        return FALSE;
    }

    if (!self->calibration_file || !*self->calibration_file) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("Property 'calibration-file' is required"), (nullptr));
        return FALSE;
    }

    try {
        auto *runtime = new MonoDetrRuntime();
        runtime->load(self->model, self->calibration_file);
        delete get_runtime(self);
        self->runtime = runtime;
        self->initialized = TRUE;
        self->compiled = FALSE;
        GST_INFO_OBJECT(self,
                        "Loaded monocular 3D model=%s device=%s type=%s input=%zux%zu labels=%zu threshold=%.3f "
                        "reverse=%d mean=%zu scale=%zu",
                        self->model, self->device ? self->device : DEFAULT_DEVICE, runtime->model_type().c_str(),
                        runtime->net_width(), runtime->net_height(), runtime->labels().size(),
                        runtime->default_threshold(), runtime->reverse_input_channels() ? 1 : 0,
                        runtime->mean().size(), runtime->scale().size());
        return TRUE;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, LIBRARY, INIT, ("Failed to initialize monocular 3D runtime"), ("%s", e.what()));
        delete get_runtime(self);
        self->runtime = NULL;
        self->initialized = FALSE;
        return FALSE;
    }
}

static gboolean gst_gva_mono3d_stop(GstBaseTransform *trans) {
    GstGvaMono3d *self = GST_GVA_MONO3D(trans);
    delete get_runtime(self);
    self->runtime = NULL;
    self->initialized = FALSE;
    self->compiled = FALSE;
    self->calibration_sent = FALSE;
    return TRUE;
}

static gboolean gst_gva_mono3d_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstGvaMono3d *self = GST_GVA_MONO3D(trans);
    (void)outcaps;

    if (!gst_video_info_from_caps(&self->video_info, incaps)) {
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Failed to parse video info from caps"), (nullptr));
        return FALSE;
    }

    GstCapsFeatures *features = gst_caps_get_features(incaps, 0);
    self->use_va_surface = features && gst_caps_features_contains(features, "memory:VAMemory");
    self->compiled = FALSE; // re-compile lazily for the (possibly changed) memory path
    return TRUE;
}

static GstFlowReturn gst_gva_mono3d_transform_ip(GstBaseTransform *trans, GstBuffer *buffer) {
    GstGvaMono3d *self = GST_GVA_MONO3D(trans);
    GMutexLockGuard lock(&self->mutex);

    if (!self->initialized || !get_runtime(self)) {
        GST_ELEMENT_ERROR(self, LIBRARY, INIT, ("Runtime is not initialized"), (nullptr));
        return GST_FLOW_ERROR;
    }

    MonoDetrRuntime *runtime = get_runtime(self);
    const size_t orig_width = GST_VIDEO_INFO_WIDTH(&self->video_info);
    const size_t orig_height = GST_VIDEO_INFO_HEIGHT(&self->video_info);

    // Compile lazily: the path (system BGR vs VAAPI NV12 surface sharing) depends on the
    // negotiated caps and, for the surface path, on a VADisplay shared by upstream VA elements.
    if (!self->compiled) {
        try {
            const char *device = self->device ? self->device : DEFAULT_DEVICE;
            if (self->use_va_surface) {
                if (!self->va_display) {
                    // The surface is bound to the decoder's display; reuse it for zero-copy import.
                    GstVaDisplay *display = gst_va_buffer_peek_display(buffer);
                    if (display)
                        self->va_display = gst_object_ref(display);
                }
                if (!self->va_display)
                    throw std::runtime_error("VAMemory input but no VADisplay available from upstream");
                if (!std::strstr(device, "GPU"))
                    throw std::runtime_error("VAMemory surface sharing requires device=GPU");
                void *dpy = gst_va_display_get_va_dpy(static_cast<GstVaDisplay *>(self->va_display));
                runtime->compile_surface(device, dpy, orig_width, orig_height);
                GST_INFO_OBJECT(self, "Compiled mono3d for VAAPI NV12 surface sharing on %s (%zux%zu)", device,
                                orig_width, orig_height);
            } else {
                runtime->compile_system(device);
                GST_INFO_OBJECT(self, "Compiled mono3d for system-memory BGR on %s", device);
            }
            self->compiled = TRUE;
        } catch (const std::exception &e) {
            GST_ELEMENT_ERROR(self, LIBRARY, INIT, ("Failed to compile monocular 3D model"), ("%s", e.what()));
            return GST_FLOW_ERROR;
        }
    }

    // Broadcast the calibration P2 downstream once so a renderer (gvawatermark3d) can
    // reuse it without the file being specified twice. Sticky so it reaches late pads.
    if (!self->calibration_sent) {
        const std::array<float, 12> &p2 = runtime->p2();
        std::ostringstream oss;
        oss.precision(9);
        for (size_t i = 0; i < p2.size(); ++i)
            oss << p2[i] << (i + 1 < p2.size() ? " " : "");
        GstStructure *s = gst_structure_new("gvamono3d-calibration", "p2", G_TYPE_STRING, oss.str().c_str(), nullptr);
        gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(GST_BASE_TRANSFORM(self)),
                           gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, s));
        self->calibration_sent = TRUE;
    }

    try {
        MonoDetrOutputs out;
        if (self->use_va_surface) {
            // Zero-copy: hand the GPU the decoder's NV12 surface directly (no host map/download).
            VASurfaceID surface = gst_va_buffer_get_surface(buffer);
            if (surface == VA_INVALID_SURFACE)
                throw std::runtime_error("Failed to obtain VA surface from input buffer");
            out = runtime->infer_surface(surface, orig_width, orig_height, static_cast<float>(orig_width),
                                         static_cast<float>(orig_height));
        } else {
            GstVideoFrame frame;
            if (!gst_video_frame_map(&frame, &self->video_info, buffer, GST_MAP_READ))
                throw std::runtime_error("Failed to map input video frame");
            const uint8_t *data = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
            const size_t stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            try {
                out = runtime->infer(data, orig_width, orig_height, stride, static_cast<float>(orig_width),
                                     static_cast<float>(orig_height));
            } catch (...) {
                gst_video_frame_unmap(&frame);
                throw;
            }
            gst_video_frame_unmap(&frame);
        }

        const float threshold = self->threshold < 0.0f ? runtime->default_threshold() : self->threshold;
        const std::vector<Detection> detections =
            decode_detections(out, runtime->p2(), static_cast<float>(orig_width), static_cast<float>(orig_height),
                              threshold, self->topk);

        GST_DEBUG_OBJECT(self, "mono3d: %zu detection(s) (queries=%zu, threshold=%.3f)", detections.size(),
                         out.num_queries, threshold);
        for (const Detection &d : detections) {
            const char *name = (d.class_id >= 0 && static_cast<size_t>(d.class_id) < runtime->labels().size())
                                   ? runtime->labels()[d.class_id].c_str()
                                   : "?";
            GST_DEBUG_OBJECT(self,
                             "  %s score=%.3f box=[%.1f,%.1f,%.1f,%.1f] hwl=[%.2f,%.2f,%.2f] xyz=[%.2f,%.2f,%.2f] "
                             "ry=%.2f alpha=%.2f",
                             name, d.score, d.x1, d.y1, d.x2, d.y2, d.h, d.w, d.l, d.x, d.y, d.z, d.rotation_y,
                             d.alpha);
        }

        /* Attach one camera-frame 3D detection per decoded box. */
        GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buffer);
        if (!rmeta)
            rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        if (!rmeta)
            throw std::runtime_error("Failed to allocate GstAnalyticsRelationMeta");

        for (const Detection &d : detections) {
            GstAnalyticsCamera3DODMtd mtd;
            if (!gst_analytics_relation_meta_add_camera_3d_od_mtd(rmeta, d.class_id, d.score, d.x1, d.y1, d.x2, d.y2,
                                                                  d.x, d.y, d.z, d.h, d.w, d.l, d.rotation_y, d.alpha,
                                                                  &mtd))
                GST_WARNING_OBJECT(self, "Failed to add camera-3d detection metadata");
        }
        return GST_FLOW_OK;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Monocular 3D inference failed"), ("%s", e.what()));
        return GST_FLOW_ERROR;
    }
}
