/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvaupscale.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gst/video/video.h>
#include <opencv2/imgproc.hpp>

#include "inference_backend/image.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/input_image_layer_descriptor.h"
#include "inference_backend/pre_proc.h"

using namespace InferenceBackend;

GST_DEBUG_CATEGORY_STATIC(gst_gvaupscale_debug_category);
#define GST_CAT_DEFAULT gst_gvaupscale_debug_category

enum {
    PROP_0,
    PROP_MODEL,
    PROP_DEVICE,
    PROP_SCALE,
};

#define DEFAULT_DEVICE "CPU"
#define DEFAULT_SCALE 2.0
#define DEFAULT_MIN_SCALE 1.0
#define DEFAULT_MAX_SCALE 8.0

// System-memory caps feature value (see gva_caps.h CapsFeature: SYSTEM_MEMORY_CAPS_FEATURE == 0).
#define GVAUPSCALE_SYSTEM_MEMORY_CAPS_FEATURE 0

// The DL Streamer OpenCV software pre-processor operates on BGR (packed 8-bit)
// system memory, consistent with the other inference elements. Color order
// required by the model is taken from model_info and applied during
// pre-processing, so only BGR is advertised on the pads.
#define GVAUPSCALE_CAPS GST_VIDEO_CAPS_MAKE("{ BGR }")

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVAUPSCALE_CAPS));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVAUPSCALE_CAPS));

G_DEFINE_TYPE(GstGvaUpscale, gst_gvaupscale, GST_TYPE_VIDEO_FILTER)

/* ------------------------------------------------------------------------- */
/* Inference backend integration                                             */
/* ------------------------------------------------------------------------- */

// Opaque per-instance state declared in the header.
struct _GvaUpscalePrivate {
    ImageInference::Ptr inference;
    std::map<std::string, InputLayerDesc::Ptr> input_preprocessors;
};

namespace {

// Frame wrapper handed to the inference backend. Besides the input image it
// carries everything the completion callback needs to write the upscaled
// result directly into the negotiated output video frame.
struct UpscaleFrame : public ImageInference::IFrameBase {
    ImagePtr image;

    uint8_t *out_data = nullptr;
    int out_width = 0;
    int out_height = 0;
    int out_stride = 0;
    bool out_is_bgr = false;

    std::exception_ptr error;

    void SetImage(ImagePtr image_) override {
        image = image_;
    }
    ImagePtr GetImage() const override {
        return image;
    }
};

// Serialize a "mean"/"std" GST_TYPE_ARRAY (or a single "scale" double) from the
// model_info pre-processing structure into a space-separated string suitable
// for the KEY_PIXEL_VALUE_* config entries.
std::string array_field_to_string(const GstStructure *s, const char *field) {
    const GValue *v = gst_structure_get_value(s, field);
    if (!v)
        return {};
    std::ostringstream out;
    if (GST_VALUE_HOLDS_ARRAY(v)) {
        const guint n = gst_value_array_get_size(v);
        for (guint i = 0; i < n; ++i) {
            if (i)
                out << ' ';
            out << g_value_get_double(gst_value_array_get_value(v, i));
        }
    } else if (G_VALUE_HOLDS_DOUBLE(v)) {
        out << g_value_get_double(v);
    }
    return out.str();
}

// Recursively flatten a (possibly std::throw_with_nested) exception chain into a
// single "cause: cause: ..." message so the real root cause is reported.
std::string nested_what(const std::exception &e, int depth = 0) {
    std::string msg = e.what();
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        if (depth < 16)
            msg += ": " + nested_what(nested, depth + 1);
    } catch (...) {
    }
    return msg;
}

} // namespace

// Completion callback: writes the model output (planar float, in the [0,1]
// range for Real-ESRGAN-style models) into the output video frame,
// denormalizing to 8-bit [0,255] and converting to the caps color format.
static void gvaupscale_on_inference_completed(std::map<std::string, OutputBlob::Ptr> blobs,
                                              std::vector<ImageInference::IFrameBase::Ptr> frames) {
    for (auto &frame_base : frames) {
        auto frame = std::static_pointer_cast<UpscaleFrame>(frame_base);
        try {
            if (blobs.empty())
                throw std::runtime_error("inference produced no output tensor");
            const OutputBlob::Ptr blob = blobs.begin()->second;
            const auto &dims = blob->GetDims();
            if (dims.size() != 4 || dims[1] != 3)
                throw std::runtime_error("expected a model output of shape [1, 3, H, W]");

            const int h = static_cast<int>(dims[2]);
            const int w = static_cast<int>(dims[3]);
            const size_t plane = static_cast<size_t>(h) * static_cast<size_t>(w);
            const float *data = reinterpret_cast<const float *>(blob->GetData());

            std::vector<cv::Mat> planes(3);
            for (int c = 0; c < 3; ++c)
                planes[c] = cv::Mat(h, w, CV_32F, const_cast<float *>(data + c * plane));
            cv::Mat rgb_f;
            cv::merge(planes, rgb_f);

            cv::min(rgb_f, 1.0, rgb_f);
            cv::max(rgb_f, 0.0, rgb_f);
            cv::Mat rgb8;
            rgb_f.convertTo(rgb8, CV_8UC3, 255.0);

            if (rgb8.cols != frame->out_width || rgb8.rows != frame->out_height)
                cv::resize(rgb8, rgb8, cv::Size(frame->out_width, frame->out_height), 0, 0, cv::INTER_LINEAR);

            cv::Mat out_packed;
            if (frame->out_is_bgr)
                cv::cvtColor(rgb8, out_packed, cv::COLOR_RGB2BGR);
            else
                out_packed = rgb8;

            cv::Mat out_wrap(frame->out_height, frame->out_width, CV_8UC3, frame->out_data, frame->out_stride);
            out_packed.copyTo(out_wrap);
        } catch (...) {
            frame->error = std::current_exception();
        }
    }
}

static void gvaupscale_on_inference_error(std::vector<ImageInference::IFrameBase::Ptr> frames) {
    for (auto &frame_base : frames) {
        auto frame = std::static_pointer_cast<UpscaleFrame>(frame_base);
        frame->error = std::make_exception_ptr(std::runtime_error("inference backend reported a failure"));
    }
}

// Lazily create the inference backend once the input frame geometry is known.
// The (dynamic, fully-convolutional) super-resolution model is reshaped to the
// exact input resolution so it runs at native resolution with no downscaling.
static bool gvaupscale_ensure_inference(GstGvaUpscale *self, int in_w, int in_h, bool in_is_bgr) {
    GvaUpscalePrivate *priv = self->priv;
    if (priv->inference)
        return true;

    if (!self->model_path || std::strlen(self->model_path) == 0) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("'model' property is not set"), (nullptr));
        return false;
    }

    try {
        // Pre-processing metadata (color order, normalization) is taken from the
        // model's model_info rt_info, following the OpenVINO Model API convention.
        // Defaults follow Real-ESRGAN behaviour: RGB input scaled by 1/255.
        std::string color_space = "RGB";
        std::string scale_values = "255.0";
        std::string mean_values;
        try {
            auto preproc = ImageInference::GetModelInfoPreproc(self->model_path, "", "");
            for (auto &entry : preproc) {
                GstStructure *s = entry.second;
                if (!s)
                    continue;
                if (gst_structure_has_field(s, "color_space")) {
                    const char *cs = gst_structure_get_string(s, "color_space");
                    if (cs && cs[0])
                        color_space = cs;
                }
                const std::string std_str = array_field_to_string(s, "std");
                const std::string scale_str = array_field_to_string(s, "scale");
                if (!std_str.empty())
                    scale_values = std_str;
                else if (!scale_str.empty())
                    scale_values = scale_str;
                const std::string mean_str = array_field_to_string(s, "mean");
                if (!mean_str.empty())
                    mean_values = mean_str;
            }
            for (auto &entry : preproc)
                if (entry.second)
                    gst_structure_free(entry.second);
        } catch (const std::exception &e) {
            GST_WARNING_OBJECT(self, "Could not read model_info pre-processing (%s); using Real-ESRGAN defaults",
                               e.what());
        }

        InferenceConfig config;
        std::map<std::string, std::string> base;
        base[KEY_MODEL] = self->model_path;
        base[KEY_DEVICE] = self->device ? self->device : DEFAULT_DEVICE;
        base[KEY_CUSTOM_PREPROC_LIB] = "";
        base[KEY_OV_EXTENSION_LIB] = "";
        base[KEY_NIREQ] = "1";
        base[KEY_BATCH_SIZE] = "1";
        base[KEY_PRE_PROCESSOR_TYPE] = std::to_string(static_cast<int>(ImagePreprocessorType::OPENCV));
        base[KEY_IMAGE_FORMAT] = in_is_bgr ? "BGR" : "RGB";
        base[KEY_CAPS_FEATURE] = std::to_string(GVAUPSCALE_SYSTEM_MEMORY_CAPS_FEATURE);
        base[KEY_RESHAPE] = "1";
        base[KEY_RESHAPE_STATIC] = "1";
        base[KEY_RESHAPE_WIDTH] = std::to_string(in_w);
        base[KEY_RESHAPE_HEIGHT] = std::to_string(in_h);
        base[KEY_PIXEL_VALUE_SCALE] = scale_values;
        if (!mean_values.empty())
            base[KEY_PIXEL_VALUE_MEAN] = mean_values;

        config[KEY_BASE] = base;
        config[KEY_INFERENCE] = {};
        config[KEY_PRE_PROCESSOR] = {};
        config[KEY_INPUT_LAYER_PRECISION] = {};
        config[KEY_FORMAT] = {};

        priv->inference = ImageInference::createImageInferenceInstance(
            MemoryType::SYSTEM, config, nullptr, gvaupscale_on_inference_completed, gvaupscale_on_inference_error,
            nullptr);
        if (!priv->inference)
            throw std::runtime_error("failed to create inference instance");

        // The software pre-processor performs the color conversion to the model
        // color order; normalization is embedded in the model via PIXEL_VALUE_SCALE.
        const auto desc_color =
            (color_space == "BGR") ? InputImageLayerDesc::ColorSpace::BGR : InputImageLayerDesc::ColorSpace::RGB;
        auto image_desc = std::make_shared<InputImageLayerDesc>(InputImageLayerDesc::Resize::NO,
                                                                InputImageLayerDesc::Crop::NO, desc_color);

        auto layer = std::make_shared<InputLayerDesc>();
        const auto inputs_info = priv->inference->GetModelInputsInfo();
        layer->name = inputs_info.empty() ? std::string() : inputs_info.begin()->first;
        layer->preprocessor = [](const InputBlob::Ptr &) {};
        layer->input_image_preroc_params = image_desc;
        priv->input_preprocessors["image"] = layer;

        GST_INFO_OBJECT(self, "Loaded upscaling model '%s' on '%s' (input %dx%d, color=%s, scale=[%s])",
                        self->model_path, base[KEY_DEVICE].c_str(), in_w, in_h, color_space.c_str(),
                        scale_values.c_str());
        return true;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, LIBRARY, INIT, ("Failed to initialize upscaling inference: %s", nested_what(e).c_str()),
                          (nullptr));
        return false;
    }
}

/* ------------------------------------------------------------------------- */
/* Caps negotiation                                                          */
/* ------------------------------------------------------------------------- */

static void gvaupscale_scale_dimension(GstStructure *st, const char *field, double factor) {
    const GValue *v = gst_structure_get_value(st, field);
    if (!v)
        return;
    if (G_VALUE_HOLDS_INT(v)) {
        const int val = g_value_get_int(v);
        gst_structure_set(st, field, G_TYPE_INT, std::max(1, static_cast<int>(std::lround(val * factor))), nullptr);
    } else if (GST_VALUE_HOLDS_INT_RANGE(v)) {
        int mn = gst_value_get_int_range_min(v);
        int mx = gst_value_get_int_range_max(v);
        mn = std::max(1, static_cast<int>(std::lround(mn * factor)));
        mx = (mx == G_MAXINT) ? G_MAXINT : std::max(mn, static_cast<int>(std::lround(mx * factor)));
        gst_structure_set(st, field, GST_TYPE_INT_RANGE, mn, mx, nullptr);
    }
}

// Sink->src multiplies the frame size by 'scale'; src->sink divides it.
static GstCaps *gst_gvaupscale_transform_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps,
                                              GstCaps *filter) {
    GstGvaUpscale *self = GST_GVAUPSCALE(base);
    const double factor = (direction == GST_PAD_SINK) ? self->scale : 1.0 / self->scale;

    GstCaps *result = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        GstStructure *st = gst_structure_copy(gst_caps_get_structure(caps, i));
        gvaupscale_scale_dimension(st, "width", factor);
        gvaupscale_scale_dimension(st, "height", factor);
        gst_caps_append_structure(result, st);
    }

    if (filter) {
        GstCaps *intersection = gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = intersection;
    }
    return result;
}

/* ------------------------------------------------------------------------- */
/* Frame processing                                                          */
/* ------------------------------------------------------------------------- */

static GstFlowReturn gst_gvaupscale_transform_frame(GstVideoFilter *filter, GstVideoFrame *inframe,
                                                    GstVideoFrame *outframe) {
    GstGvaUpscale *self = GST_GVAUPSCALE(filter);

    const int in_w = GST_VIDEO_FRAME_WIDTH(inframe);
    const int in_h = GST_VIDEO_FRAME_HEIGHT(inframe);
    const bool in_is_bgr = GST_VIDEO_FRAME_FORMAT(inframe) == GST_VIDEO_FORMAT_BGR;

    if (!gvaupscale_ensure_inference(self, in_w, in_h, in_is_bgr))
        return GST_FLOW_ERROR;

    try {
        // Wrap the input frame as a system-memory image for the inference backend.
        auto image = std::make_shared<Image>();
        image->type = MemoryType::SYSTEM;
        image->format = in_is_bgr ? FOURCC_BGR : FOURCC_RGB;
        image->width = static_cast<uint32_t>(in_w);
        image->height = static_cast<uint32_t>(in_h);
        image->stride[0] = static_cast<uint32_t>(GST_VIDEO_FRAME_PLANE_STRIDE(inframe, 0));
        image->planes[0] = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(inframe, 0));
        image->size = image->stride[0] * image->height;
        // Leave rect zero-initialized: a non-empty rect triggers the backend's
        // crop path, which does not support packed RGB. Full-frame processing
        // uses width/height directly.
        image->rect = {};

        auto frame = std::make_shared<UpscaleFrame>();
        frame->SetImage(image);
        frame->out_data = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(outframe, 0));
        frame->out_width = GST_VIDEO_FRAME_WIDTH(outframe);
        frame->out_height = GST_VIDEO_FRAME_HEIGHT(outframe);
        frame->out_stride = GST_VIDEO_FRAME_PLANE_STRIDE(outframe, 0);
        frame->out_is_bgr = GST_VIDEO_FRAME_FORMAT(outframe) == GST_VIDEO_FORMAT_BGR;

        // Submit and wait: the completion callback writes the upscaled result
        // into the output frame before Flush() returns.
        self->priv->inference->SubmitImage(frame, self->priv->input_preprocessors);
        self->priv->inference->Flush();

        if (frame->error)
            std::rethrow_exception(frame->error);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Upscaling inference failed: %s", nested_what(e).c_str()), (nullptr));
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

/* ------------------------------------------------------------------------- */
/* GObject boilerplate                                                       */
/* ------------------------------------------------------------------------- */

static void gst_gvaupscale_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaUpscale *self = GST_GVAUPSCALE(object);
    switch (prop_id) {
    case PROP_MODEL:
        g_free(self->model_path);
        self->model_path = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(self->device);
        self->device = g_value_dup_string(value);
        break;
    case PROP_SCALE:
        self->scale = g_value_get_double(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvaupscale_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaUpscale *self = GST_GVAUPSCALE(object);
    switch (prop_id) {
    case PROP_MODEL:
        g_value_set_string(value, self->model_path);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, self->device);
        break;
    case PROP_SCALE:
        g_value_set_double(value, self->scale);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvaupscale_finalize(GObject *object) {
    GstGvaUpscale *self = GST_GVAUPSCALE(object);
    if (self->priv) {
        if (self->priv->inference)
            self->priv->inference->Close();
        delete self->priv;
        self->priv = nullptr;
    }
    g_free(self->model_path);
    g_free(self->device);
    G_OBJECT_CLASS(gst_gvaupscale_parent_class)->finalize(object);
}

static void gst_gvaupscale_class_init(GstGvaUpscaleClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_gvaupscale_debug_category, "gvaupscale", 0, "Neural image upscaling element");

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

    gobject_class->set_property = gst_gvaupscale_set_property;
    gobject_class->get_property = gst_gvaupscale_get_property;
    gobject_class->finalize = gst_gvaupscale_finalize;

    base_transform_class->transform_caps = gst_gvaupscale_transform_caps;
    video_filter_class->transform_frame = gst_gvaupscale_transform_frame;

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(
        element_class, "Neural image upscaling", "Filter/Effect/Video",
        "Upscales (super-resolves) video frames using an OpenVINO™ toolkit model and emits larger frames",
        "Intel® Corporation");

    g_object_class_install_property(
        gobject_class, PROP_MODEL,
        g_param_spec_string("model", "Model", "Path to the OpenVINO™ toolkit super-resolution model (.xml)", nullptr,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "Target inference device (CPU, GPU, NPU, AUTO, ...)", DEFAULT_DEVICE,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SCALE,
        g_param_spec_double("scale", "Scale factor", "Output/input resolution ratio produced by the model",
                            DEFAULT_MIN_SCALE, DEFAULT_MAX_SCALE, DEFAULT_SCALE,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gvaupscale_init(GstGvaUpscale *self) {
    self->model_path = nullptr;
    self->device = g_strdup(DEFAULT_DEVICE);
    self->scale = DEFAULT_SCALE;
    self->priv = new _GvaUpscalePrivate();
}
