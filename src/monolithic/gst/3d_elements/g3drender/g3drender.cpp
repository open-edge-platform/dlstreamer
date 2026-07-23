/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3drender.h"

#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/g3d_od_mtd.h>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsbatchmeta.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_render_debug);
#define GST_CAT_DEFAULT gst_g3d_render_debug

GType gst_g3d_render_view_mode_get_type(void) {
    static GType type = 0;
    if (!type) {
        static const GEnumValue values[] = {{0, "Bird's Eye View", "bev"},
                                            {1, "Perspective 3D", "perspective"},
                                            {2, "Camera Projection", "cam-proj"},
                                            {0, nullptr, nullptr}};
        type = g_enum_register_static("GstG3DRenderViewMode", values);
    }
    return type;
}

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_POINT_RADIUS,
    PROP_POINT_STRIDE,
    PROP_ZOOM,
    PROP_VIEW_MODE,
    PROP_CAM_DISTANCE,
    PROP_CAM_ELEVATION,
    PROP_CAM_AZIMUTH,
    PROP_CAM_FOV,
    PROP_CAM_PROJ_INDEX,
    PROP_CAM_BG_GRAYSCALE,
    PROP_CAM_BG_DIM,
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("application/x-lidar; "
                                            "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw, format=BGR, "
                                            "width=[ 1, 32767 ], height=[ 1, 32767 ], "
                                            "framerate=(fraction)[ 0/1, 120/1 ]"));

static void gst_g3d_render_set_property(GObject *obj, guint prop_id, const GValue *val, GParamSpec *pspec);
static void gst_g3d_render_get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec);
static gboolean gst_g3d_render_start(GstBaseTransform *trans);
static gboolean gst_g3d_render_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstCaps *gst_g3d_render_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                              GstCaps *filter);
static gboolean gst_g3d_render_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_g3d_render_prepare_output_buffer(GstBaseTransform *trans, GstBuffer *inbuf,
                                                          GstBuffer **outbuf);
static GstFlowReturn gst_g3d_render_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf);

G_DEFINE_TYPE(GstG3DRender, gst_g3d_render, GST_TYPE_BASE_TRANSFORM);

static void gst_g3d_render_class_init(GstG3DRenderClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_g3d_render_debug, "g3drender", 0, "LiDAR Renderer");

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_g3d_render_set_property;
    gobject_class->get_property = gst_g3d_render_get_property;

    g_object_class_install_property(gobject_class, PROP_WIDTH,
                                    g_param_spec_int("width", "Width", "Output image width in pixels", 1, 32767, 800,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_HEIGHT,
                                    g_param_spec_int("height", "Height", "Output image height in pixels", 1, 32767, 800,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_POINT_RADIUS,
                                    g_param_spec_int("point-radius", "Point Radius",
                                                     "Radius of each rendered point in pixels", 1, 20, 2,
                                                     (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_POINT_STRIDE,
        g_param_spec_int("point-stride", "Point Stride",
                         "Render every Nth point (1 = all points, 16 = every 16th point, etc.)", 1, 100, 16,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ZOOM,
        g_param_spec_float(
            "zoom", "Zoom",
            "BEV zoom factor: 1.0=default (50m range), 2.0=zoomed in (25m range), 0.5=zoomed out (100m range)", 0.1f,
            20.0f, 1.0f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_VIEW_MODE,
        g_param_spec_enum("view-mode", "View Mode",
                          "Rendering mode: bev (Bird's Eye View), perspective (3D perspective), "
                          "or cam-proj (project LiDAR onto camera image using calibration)",
                          GST_TYPE_G3D_RENDER_VIEW_MODE, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_DISTANCE,
                                    g_param_spec_float("cam-distance", "Camera Distance",
                                                       "Camera distance from origin in meters (perspective mode)", 1.0f,
                                                       500.0f, 35.0f,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_ELEVATION,
        g_param_spec_float("cam-elevation", "Camera Elevation",
                           "Camera elevation angle in degrees: 0=horizon 90=top-down (perspective mode)", 5.0f, 89.0f,
                           30.0f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_AZIMUTH,
        g_param_spec_float("cam-azimuth", "Camera Azimuth", "Camera horizontal angle in degrees (perspective mode)",
                           -360.0f, 360.0f, 180.0f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_FOV,
        g_param_spec_float("cam-fov", "Camera FOV", "Field of view in degrees (perspective mode)", 10.0f, 150.0f, 60.0f,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_PROJ_INDEX,
        g_param_spec_int("cam-proj-index", "Camera Projection Index",
                         "Index of the camera stream to use for LiDAR projection in cam-proj mode; "
                         "clamped to the number of available camera streams at runtime",
                         0, 255, 0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_BG_GRAYSCALE,
                                    g_param_spec_boolean("cam-bg-grayscale", "Camera Background Grayscale",
                                                         "Convert the camera background to grayscale in cam-proj mode",
                                                         TRUE,
                                                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CAM_BG_DIM,
        g_param_spec_float("cam-bg-dim", "Camera Background Dim Factor",
                           "Brightness multiplier for the camera background in cam-proj mode (0.0=black, 1.0=original)",
                           0.0f, 1.0f, 0.65f, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "G3D LiDAR Renderer", "Filter/Converter",
                                          "Renders LiDAR point cloud as BEV or perspective 3D video frame (g3drender)",
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    bt_class->start = GST_DEBUG_FUNCPTR(gst_g3d_render_start);
    bt_class->sink_event = GST_DEBUG_FUNCPTR(gst_g3d_render_sink_event);
    bt_class->transform_caps = GST_DEBUG_FUNCPTR(gst_g3d_render_transform_caps);
    bt_class->set_caps = GST_DEBUG_FUNCPTR(gst_g3d_render_set_caps);
    bt_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_g3d_render_prepare_output_buffer);
    bt_class->transform = GST_DEBUG_FUNCPTR(gst_g3d_render_transform);
    bt_class->passthrough_on_same_caps = FALSE;
}

static void gst_g3d_render_init(GstG3DRender *self) {
    self->width = 800;
    self->height = 800;
    self->range_x_min = -50.0f;
    self->range_x_max = 50.0f;
    self->range_y_min = -50.0f;
    self->range_y_max = 50.0f;
    self->point_radius = 2;
    self->point_stride = 16;
    self->zoom = 1.0f;
    self->view_mode = 0;
    self->cam_distance = 35.0f;
    self->cam_elevation = 30.0f;
    self->cam_azimuth = 180.0f;
    self->cam_fov = 60.0f;
    self->cam_proj_index = 0;
    self->cam_bg_grayscale = TRUE;
    self->cam_bg_dim = 0.65f;
    self->frame_duration = GST_SECOND / 10;
    self->frame_count = 0;
    self->input_is_batch = FALSE;
    self->has_calib = FALSE;
}

static gboolean gst_g3d_render_start(GstBaseTransform *trans) {
    GstG3DRender *self = GST_G3D_RENDER(trans);
    self->frame_count = 0;
    return TRUE;
}

namespace {

/* Reads a GST_TYPE_ARRAY field of floats from a GstStructure into a caller-supplied array.
 * Returns false if the field is missing, not an array, or has the wrong element count. */
bool read_gst_float_array(const GstStructure *s, const char *key, gfloat *out, int n) {
    const GValue *val = gst_structure_get_value(s, key);
    if (!val || !GST_VALUE_HOLDS_ARRAY(val))
        return false;
    if (static_cast<int>(gst_value_array_get_size(val)) != n)
        return false;
    for (int i = 0; i < n; ++i) {
        const GValue *elem = gst_value_array_get_value(val, i);
        if (!G_VALUE_HOLDS_FLOAT(elem))
            return false;
        out[i] = g_value_get_float(elem);
    }
    return true;
}

} // namespace

static gboolean gst_g3d_render_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstG3DRender *self = GST_G3D_RENDER(trans);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM_STICKY) {
        const GstStructure *s = gst_event_get_structure(event);
        if (s && gst_structure_has_name(s, "g3d/calibration")) {
            char cam_key[32];
            g_snprintf(cam_key, sizeof(cam_key), "camera-%d", self->cam_proj_index);
            const GValue *cam_val = gst_structure_get_value(s, cam_key);
            if (!cam_val)
                cam_val = gst_structure_get_value(s, "camera-0");
            if (cam_val && GST_VALUE_HOLDS_STRUCTURE(cam_val)) {
                const GstStructure *cam = gst_value_get_structure(cam_val);
                if (read_gst_float_array(cam, "tr_velo_to_cam", self->calib_tr, 16) &&
                    read_gst_float_array(cam, "r0_rect", self->calib_r0, 16) &&
                    read_gst_float_array(cam, "p2", self->calib_p2, 12)) {
                    self->has_calib = TRUE;
                    GST_INFO_OBJECT(self, "cam-proj calibration received from objectfuser");
                }
            }
        }
    }

    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
        const GstSegment *seg;
        gst_event_parse_segment(event, &seg);
        if (seg->format != GST_FORMAT_TIME) {
            GstSegment time_seg;
            gst_segment_init(&time_seg, GST_FORMAT_TIME);
            time_seg.start = 0;
            time_seg.stop = GST_CLOCK_TIME_NONE;
            time_seg.position = 0;
            time_seg.time = 0;
            GstEvent *time_event = gst_event_new_segment(&time_seg);
            gst_event_unref(event);
            event = time_event;
        }
    }
    return GST_BASE_TRANSFORM_CLASS(gst_g3d_render_parent_class)->sink_event(trans, event);
}

static void gst_g3d_render_set_property(GObject *obj, guint prop_id, const GValue *val, GParamSpec *pspec) {
    GstG3DRender *self = GST_G3D_RENDER(obj);
    switch (prop_id) {
    case PROP_WIDTH:
        self->width = g_value_get_int(val);
        break;
    case PROP_HEIGHT:
        self->height = g_value_get_int(val);
        break;
    case PROP_POINT_RADIUS:
        self->point_radius = g_value_get_int(val);
        break;
    case PROP_POINT_STRIDE:
        self->point_stride = g_value_get_int(val);
        break;
    case PROP_ZOOM:
        self->zoom = g_value_get_float(val);
        break;
    case PROP_VIEW_MODE:
        self->view_mode = g_value_get_enum(val);
        break;
    case PROP_CAM_DISTANCE:
        self->cam_distance = g_value_get_float(val);
        break;
    case PROP_CAM_ELEVATION:
        self->cam_elevation = g_value_get_float(val);
        break;
    case PROP_CAM_AZIMUTH:
        self->cam_azimuth = g_value_get_float(val);
        break;
    case PROP_CAM_FOV:
        self->cam_fov = g_value_get_float(val);
        break;
    case PROP_CAM_PROJ_INDEX:
        self->cam_proj_index = g_value_get_int(val);
        break;
    case PROP_CAM_BG_GRAYSCALE:
        self->cam_bg_grayscale = g_value_get_boolean(val);
        break;
    case PROP_CAM_BG_DIM:
        self->cam_bg_dim = g_value_get_float(val);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
        break;
    }
}

static void gst_g3d_render_get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec) {
    GstG3DRender *self = GST_G3D_RENDER(obj);
    switch (prop_id) {
    case PROP_WIDTH:
        g_value_set_int(val, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int(val, self->height);
        break;
    case PROP_POINT_RADIUS:
        g_value_set_int(val, self->point_radius);
        break;
    case PROP_POINT_STRIDE:
        g_value_set_int(val, self->point_stride);
        break;
    case PROP_ZOOM:
        g_value_set_float(val, self->zoom);
        break;
    case PROP_VIEW_MODE:
        g_value_set_enum(val, self->view_mode);
        break;
    case PROP_CAM_DISTANCE:
        g_value_set_float(val, self->cam_distance);
        break;
    case PROP_CAM_ELEVATION:
        g_value_set_float(val, self->cam_elevation);
        break;
    case PROP_CAM_AZIMUTH:
        g_value_set_float(val, self->cam_azimuth);
        break;
    case PROP_CAM_FOV:
        g_value_set_float(val, self->cam_fov);
        break;
    case PROP_CAM_PROJ_INDEX:
        g_value_set_int(val, self->cam_proj_index);
        break;
    case PROP_CAM_BG_GRAYSCALE:
        g_value_set_boolean(val, self->cam_bg_grayscale);
        break;
    case PROP_CAM_BG_DIM:
        g_value_set_float(val, self->cam_bg_dim);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
        break;
    }
}

static GstCaps *gst_g3d_render_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                              GstCaps *filter) {
    GstG3DRender *self = GST_G3D_RENDER(trans);
    GstCaps *result;
    if (direction == GST_PAD_SINK) {
        gint w = self->width;
        if (w == self->height && caps && gst_caps_get_size(caps) > 0) {
            if (g_strcmp0(gst_structure_get_name(gst_caps_get_structure(caps, 0)), "multistream/x-analytics-batch") ==
                0)
                w = self->height * 2;
        }
        result = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width", G_TYPE_INT, w, "height",
                                     G_TYPE_INT, self->height, nullptr);
        gst_caps_set_simple(result, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 120, 1, nullptr);
    } else {
        result = gst_caps_from_string("application/x-lidar; "
                                      "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)");
    }
    if (filter) {
        GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = tmp;
    }
    return result;
}

static gboolean gst_g3d_render_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstG3DRender *self = GST_G3D_RENDER(trans);
    GstStructure *s = gst_caps_get_structure(incaps, 0);
    self->input_is_batch = (g_strcmp0(gst_structure_get_name(s), "multistream/x-analytics-batch") == 0);
    gst_structure_get_int(gst_caps_get_structure(outcaps, 0), "width", &self->width);
    gst_structure_get_int(gst_caps_get_structure(outcaps, 0), "height", &self->height);
    gint fps_n = 0, fps_d = 1;
    gst_structure_get_fraction(gst_caps_get_structure(outcaps, 0), "framerate", &fps_n, &fps_d);
    self->frame_duration = (fps_n > 0) ? gst_util_uint64_scale(GST_SECOND, fps_d, fps_n) : GST_SECOND / 10;
    GST_INFO_OBJECT(self,
                    "input caps: %s (batch=%d) canvas=%dx%d framerate=%d/%d frame_duration=%" GST_TIME_FORMAT "%s",
                    gst_structure_get_name(s), self->input_is_batch, self->width, self->height, fps_n, fps_d,
                    GST_TIME_ARGS(self->frame_duration), (fps_n <= 0) ? " (fallback)" : "");
    return TRUE;
}

static GstFlowReturn gst_g3d_render_prepare_output_buffer(GstBaseTransform *trans, GstBuffer *inbuf,
                                                          GstBuffer **outbuf) {
    (void)inbuf;
    GstG3DRender *self = GST_G3D_RENDER(trans);

    gsize frame_size = static_cast<gsize>(self->width) * self->height * 3;
    *outbuf = gst_buffer_new_allocate(nullptr, frame_size, nullptr);
    if (!*outbuf) {
        GST_ERROR_OBJECT(self, "Failed to allocate output buffer (%zu bytes)", frame_size);
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

/* Rendering helpers */

namespace {

cv::Point world_to_pixel_bev(float x, float y, const GstG3DRender *self, int roi_w, int roi_h) {
    float x_range = (self->range_x_max - self->range_x_min) / self->zoom;
    float y_range = (self->range_y_max - self->range_y_min) / self->zoom;
    float x_mid = (self->range_x_max + self->range_x_min) / 2.0f;
    float y_mid = (self->range_y_max + self->range_y_min) / 2.0f;
    int px = static_cast<int>((1.0f - (y - (y_mid - y_range / 2.0f)) / y_range) * roi_w);
    int py = static_cast<int>((1.0f - (x - (x_mid - x_range / 2.0f)) / x_range) * roi_h);
    return cv::Point(px, py);
}

/* Renders LiDAR point cloud as a top-down bird's eye view onto canvas.
 * Also draws grid lines and an XY axis indicator in the corner. */
void draw_bev(cv::Mat &canvas, const float *points, guint count, const GstG3DRender *self, int roi_w, int roi_h) {
    const cv::Scalar grid_color(55, 55, 55);
    cv::line(canvas, world_to_pixel_bev(0.0f, self->range_y_min, self, roi_w, roi_h),
             world_to_pixel_bev(0.0f, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
    cv::line(canvas, world_to_pixel_bev(self->range_x_min, 0.0f, self, roi_w, roi_h),
             world_to_pixel_bev(self->range_x_max, 0.0f, self, roi_w, roi_h), grid_color, 1);
    for (float d = 10.0f; d < self->range_x_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(-d, self->range_y_min, self, roi_w, roi_h),
                 world_to_pixel_bev(-d, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev(d, self->range_y_min, self, roi_w, roi_h),
                 world_to_pixel_bev(d, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
    }
    for (float d = 10.0f; d < self->range_y_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(self->range_x_min, -d, self, roi_w, roi_h),
                 world_to_pixel_bev(self->range_x_max, -d, self, roi_w, roi_h), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev(self->range_x_min, d, self, roi_w, roi_h),
                 world_to_pixel_bev(self->range_x_max, d, self, roi_w, roi_h), grid_color, 1);
    }
    cv::Point origin = world_to_pixel_bev(0.0f, 0.0f, self, roi_w, roi_h);
    int origin_scale = 18;
    cv::Scalar axis_color(240, 240, 240);
    cv::Point x_tip(origin.x, origin.y - origin_scale);
    cv::Point y_tip(origin.x - origin_scale, origin.y);
    cv::Point x_neg(origin.x, origin.y + origin_scale);
    cv::Point y_neg(origin.x + origin_scale, origin.y);
    cv::line(canvas, x_neg, x_tip, axis_color, 1, cv::LINE_AA);
    cv::line(canvas, y_neg, y_tip, axis_color, 1, cv::LINE_AA);

    int ind_cx = roi_w - 70, ind_cy = roi_h - 70, ind_scale = 45;
    cv::Scalar ind_color(220, 220, 220);

    auto draw_bev_axis = [&](cv::Point2f dir, const char *label) {
        cv::Point pos_tip(ind_cx + static_cast<int>(dir.x * ind_scale), ind_cy + static_cast<int>(dir.y * ind_scale));
        cv::Point neg_tip(ind_cx + static_cast<int>(-dir.x * ind_scale / 2.0f),
                          ind_cy + static_cast<int>(-dir.y * ind_scale / 2.0f));
        float dx = static_cast<float>(pos_tip.x - neg_tip.x), dy = static_cast<float>(pos_tip.y - neg_tip.y);
        float d = std::sqrt(dx * dx + dy * dy);
        float ux = dx / d, uy = dy / d;
        int arrow_len = 10;
        float dash_end = d - arrow_len;
        float pos = 0.0f;
        bool on = true;
        while (pos < dash_end) {
            float end = std::min(pos + (on ? 6.0f : 4.0f), dash_end);
            if (on) {
                cv::Point a(neg_tip.x + static_cast<int>(ux * pos), neg_tip.y + static_cast<int>(uy * pos));
                cv::Point b(neg_tip.x + static_cast<int>(ux * end), neg_tip.y + static_cast<int>(uy * end));
                cv::line(canvas, a, b, ind_color, 1, cv::LINE_AA);
            }
            pos = end;
            on = !on;
        }
        cv::Point arrow_start(neg_tip.x + static_cast<int>(ux * dash_end), neg_tip.y + static_cast<int>(uy * dash_end));
        cv::arrowedLine(canvas, arrow_start, pos_tip, ind_color, 1, cv::LINE_AA, 0, 0.4);
        cv::Point2f label_pos(pos_tip.x + dir.x * 10, pos_tip.y + dir.y * 10);
        cv::putText(canvas, label, cv::Point(static_cast<int>(label_pos.x) - 5, static_cast<int>(label_pos.y) + 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, ind_color, 1, cv::LINE_AA);
    };

    draw_bev_axis(cv::Point2f(0.0f, -1.0f), "X");
    draw_bev_axis(cv::Point2f(-1.0f, 0.0f), "Y");

    float x_range = (self->range_x_max - self->range_x_min) / self->zoom;
    float y_range = (self->range_y_max - self->range_y_min) / self->zoom;
    float x_mid = (self->range_x_max + self->range_x_min) / 2.0f;
    float y_mid = (self->range_y_max + self->range_y_min) / 2.0f;
    float x_off = x_mid - x_range / 2.0f;
    float y_off = y_mid - y_range / 2.0f;
    float inv_y = roi_w / y_range;

    if (self->point_radius == 1) {
        for (guint i = 0; i < count; i += static_cast<guint>(self->point_stride)) {
            float x = points[i * 4 + 0], y = points[i * 4 + 1], intensity = points[i * 4 + 3];
            int px = roi_w - 1 - static_cast<int>((y - y_off) * inv_y);
            int py = static_cast<int>((1.0f - (x - x_off) / x_range) * roi_h);
            if (px < 0 || px >= roi_w || py < 0 || py >= roi_h)
                continue;
            uint8_t r = static_cast<uint8_t>(intensity * 255.0f);
            uint8_t b = static_cast<uint8_t>((1.0f - intensity) * 255.0f);
            canvas.at<cv::Vec3b>(py, px) = cv::Vec3b(b, 80, r);
        }
    } else {
        for (guint i = 0; i < count; i += static_cast<guint>(self->point_stride)) {
            float x = points[i * 4 + 0], y = points[i * 4 + 1], intensity = points[i * 4 + 3];
            int px = roi_w - 1 - static_cast<int>((y - y_off) * inv_y);
            int py = static_cast<int>((1.0f - (x - x_off) / x_range) * roi_h);
            if (px < 0 || px >= roi_w || py < 0 || py >= roi_h)
                continue;
            uint8_t r = static_cast<uint8_t>(intensity * 255.0f);
            uint8_t b = static_cast<uint8_t>((1.0f - intensity) * 255.0f);
            cv::circle(canvas, cv::Point(px, py), self->point_radius, cv::Scalar(b, 80, r), -1);
        }
    }
}

/* Perspective rendering */

cv::Vec3b height_to_color(float z) {
    float t = (z + 2.0f) / 6.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        float s = t * 2.0f;
        return cv::Vec3b(static_cast<uint8_t>((1.0f - s) * 200), static_cast<uint8_t>(s * 180),
                         static_cast<uint8_t>(s * 80));
    }
    float s = (t - 0.5f) * 2.0f;
    return cv::Vec3b(0, static_cast<uint8_t>((1.0f - s) * 180), static_cast<uint8_t>(s * 220 + 80));
}

const cv::Scalar kColorUnmatched(0, 255, 0);   // green — 3D unmatched
const cv::Scalar kColor2DUnmatched(0, 255, 0); // green — 2D unmatched
const cv::Scalar kColorMatched(255, 255, 255); // white — 3D matched (associated with a 2D det)

/* Projects 3D box corners through a synthetic perspective camera and draws the 12 box edges.
 * Boxes behind the camera (center_depth <= 0.5) are skipped. */
void draw_detection_boxes_perspective(cv::Mat &canvas, GstBuffer *inbuf, const cv::Mat &rvec, const cv::Mat &tvec,
                                      const cv::Mat &K, const cv::Mat &dist_coeffs, const cv::Mat &cam_pos,
                                      const cv::Mat &zaxis, int roi_w, int roi_h,
                                      const std::unordered_set<guint> &assoc_ids) {
    double zx = zaxis.at<double>(0), zy = zaxis.at<double>(1), zz = zaxis.at<double>(2);
    double cpx = cam_pos.at<double>(0), cpy = cam_pos.at<double>(1), cpz = cam_pos.at<double>(2);

    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(inbuf);
    if (!rmeta)
        return;

    GstAnalyticsMtdType type = gst_analytics_3d_od_mtd_get_mtd_type();
    GstAnalytics3DODMtd mtd;
    gpointer state = nullptr;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, type, reinterpret_cast<GstAnalyticsMtd *>(&mtd))) {
        gfloat bcx, bcy, bcz, length, width, height, yaw, pitch, roll;
        gst_analytics_3d_od_mtd_get_location(&mtd, &bcx, &bcy, &bcz, &length, &width, &height, &yaw, &pitch, &roll);

        float cos_t = std::cos(yaw), sin_t = -std::sin(yaw);
        float hx = width / 2.0f, hy = length / 2.0f, hz = height / 2.0f;

        float lx[4] = {-hx, +hx, +hx, -hx};
        float ly[4] = {-hy, -hy, +hy, +hy};

        std::vector<cv::Point3f> corners(8);
        for (int i = 0; i < 4; ++i) {
            float wx = bcx + lx[i] * cos_t - ly[i] * sin_t;
            float wy = bcy + lx[i] * sin_t + ly[i] * cos_t;
            corners[i] = cv::Point3f(wx, wy, bcz - hz);
            corners[i + 4] = cv::Point3f(wx, wy, bcz + hz);
        }

        double center_depth = zx * (bcx - cpx) + zy * (bcy - cpy) + zz * (bcz - cpz);
        if (center_depth <= 0.5)
            continue;

        std::vector<cv::Point2f> img_corners;
        cv::projectPoints(corners, rvec, tvec, K, dist_coeffs, img_corners);

        static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        bool matched = assoc_ids.count(mtd.id) > 0;
        cv::Scalar color = matched ? kColorMatched : kColorUnmatched;
        int thickness = 1;
        for (auto &e : edges) {
            int a = e[0], b = e[1];
            cv::line(canvas, cv::Point(static_cast<int>(img_corners[a].x), static_cast<int>(img_corners[a].y)),
                     cv::Point(static_cast<int>(img_corners[b].x), static_cast<int>(img_corners[b].y)), color,
                     thickness, cv::LINE_AA);
        }
    }
    (void)roi_w;
    (void)roi_h;
}

/* Renders the point cloud and 3D detection boxes from a configurable synthetic 3D camera viewpoint.
 * Camera position is derived from cam_distance, cam_elevation, and cam_azimuth. */
void draw_perspective(cv::Mat &canvas, const float *points, guint count, GstBuffer *inbuf, GstG3DRender *self,
                      int roi_w, int roi_h, const std::unordered_set<guint> &assoc_ids) {
    constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;
    float az = self->cam_azimuth * DEG2RAD;
    float el = self->cam_elevation * DEG2RAD;
    float dist = self->cam_distance;

    cv::Mat cam_pos = (cv::Mat_<double>(3, 1) << dist * std::cos(el) * std::cos(az), dist * std::cos(el) * std::sin(az),
                       dist * std::sin(el));

    cv::Mat zaxis = -cam_pos / cv::norm(cam_pos);
    cv::Mat world_up = (cv::Mat_<double>(3, 1) << 0, 0, 1);
    cv::Mat xaxis = zaxis.cross(world_up);
    if (cv::norm(xaxis) < 1e-6)
        xaxis = (cv::Mat_<double>(3, 1) << 1, 0, 0);
    else
        xaxis = xaxis / cv::norm(xaxis);
    cv::Mat yaxis = zaxis.cross(xaxis);

    cv::Mat R(3, 3, CV_64F);
    R.at<double>(0, 0) = xaxis.at<double>(0);
    R.at<double>(0, 1) = xaxis.at<double>(1);
    R.at<double>(0, 2) = xaxis.at<double>(2);
    R.at<double>(1, 0) = yaxis.at<double>(0);
    R.at<double>(1, 1) = yaxis.at<double>(1);
    R.at<double>(1, 2) = yaxis.at<double>(2);
    R.at<double>(2, 0) = zaxis.at<double>(0);
    R.at<double>(2, 1) = zaxis.at<double>(1);
    R.at<double>(2, 2) = zaxis.at<double>(2);

    cv::Mat rvec;
    cv::Rodrigues(R, rvec);
    cv::Mat tvec = -R * cam_pos;

    double fx = (roi_w / 2.0) / std::tan(self->cam_fov * DEG2RAD / 2.0);
    double fy = fx;
    cv::Mat K = (cv::Mat_<double>(3, 3) << fx, 0, roi_w / 2.0, 0, fy, roi_h / 2.0, 0, 0, 1);
    cv::Mat dist_coeffs = cv::Mat::zeros(4, 1, CV_64F);

    std::vector<cv::Point3f> obj_pts;
    std::vector<float> z_vals;
    obj_pts.reserve(count / static_cast<guint>(self->point_stride) + 1);
    z_vals.reserve(obj_pts.capacity());

    float clip = self->cam_distance + 20.0f;
    for (guint i = 0; i < count; i += static_cast<guint>(self->point_stride)) {
        float x = points[i * 4 + 0], y = points[i * 4 + 1], z = points[i * 4 + 2];
        if (x < -clip || x > clip || y < -clip || y > clip)
            continue;
        obj_pts.push_back(cv::Point3f(x, y, z));
        z_vals.push_back(z);
    }

    if (obj_pts.empty())
        return;

    std::vector<cv::Point2f> img_pts;
    cv::projectPoints(obj_pts, rvec, tvec, K, dist_coeffs, img_pts);

    struct ProjPt {
        cv::Point2f px;
        float depth;
        float z;
    };
    std::vector<ProjPt> visible;
    visible.reserve(img_pts.size());

    double zx = zaxis.at<double>(0), zy = zaxis.at<double>(1), zz = zaxis.at<double>(2);
    double cx = cam_pos.at<double>(0), cy = cam_pos.at<double>(1), cz = cam_pos.at<double>(2);

    for (size_t i = 0; i < img_pts.size(); ++i) {
        cv::Point2f &p = img_pts[i];
        if (p.x < 0 || p.x >= roi_w || p.y < 0 || p.y >= roi_h)
            continue;
        float depth =
            static_cast<float>(zx * (obj_pts[i].x - cx) + zy * (obj_pts[i].y - cy) + zz * (obj_pts[i].z - cz));
        if (depth <= 0.5f)
            continue;
        visible.push_back({p, depth, z_vals[i]});
    }

    std::sort(visible.begin(), visible.end(), [](const ProjPt &a, const ProjPt &b) { return a.depth > b.depth; });

    if (self->point_radius == 1) {
        for (auto &pp : visible)
            canvas.at<cv::Vec3b>(static_cast<int>(pp.px.y), static_cast<int>(pp.px.x)) = height_to_color(pp.z);
    } else {
        for (auto &pp : visible) {
            int r = std::max(1, static_cast<int>(self->point_radius * 20.0f / pp.depth));
            cv::Vec3b c = height_to_color(pp.z);
            cv::circle(canvas, cv::Point(static_cast<int>(pp.px.x), static_cast<int>(pp.px.y)), r,
                       cv::Scalar(c[0], c[1], c[2]), -1);
        }
    }

    {
        float step = 3.0f;
        std::vector<cv::Point3f> axis_pts = {
            {0, 0, 0},
            {step, 0, 0},
            {0, step, 0},
            {0, 0, step},
        };
        std::vector<cv::Point2f> axis_img;
        cv::projectPoints(axis_pts, rvec, tvec, K, dist_coeffs, axis_img);

        cv::Point2f o = axis_img[0];
        int ind_cx = roi_w - 70, ind_cy = roi_h - 70, ind_scale = 45;

        struct AxisDef {
            int idx;
            cv::Scalar color;
            const char *label;
        };
        AxisDef axes[3] = {
            {1, cv::Scalar(90, 90, 200), "X"},
            {2, cv::Scalar(90, 180, 90), "Y"},
            {3, cv::Scalar(180, 90, 90), "Z"},
        };

        if (o.x >= 0 && o.x < roi_w && o.y >= 0 && o.y < roi_h) {
            int origin_scale = 18;
            cv::Point origin_pt(static_cast<int>(o.x), static_cast<int>(o.y));
            cv::Scalar axis_color(240, 240, 240);
            for (int ai = 0; ai < 2; ++ai) {
                auto &ax = axes[ai];
                cv::Point2f dir = axis_img[ax.idx] - o;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len < 0.5f)
                    continue;
                dir /= len;
                cv::Point pos_tip(origin_pt.x + static_cast<int>(dir.x * origin_scale),
                                  origin_pt.y + static_cast<int>(dir.y * origin_scale));
                cv::Point neg_tip(origin_pt.x + static_cast<int>(-dir.x * origin_scale),
                                  origin_pt.y + static_cast<int>(-dir.y * origin_scale));
                cv::line(canvas, neg_tip, pos_tip, axis_color, 1, cv::LINE_AA);
            }
        }

        cv::Scalar ind_color(220, 220, 220);
        for (int ai = 0; ai < 3; ++ai) {
            auto &ax = axes[ai];
            cv::Point2f dir = axis_img[ax.idx] - o;
            if (ai == 1)
                dir = -dir;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 0.5f)
                continue;
            dir /= len;
            cv::Point pos_tip(ind_cx + static_cast<int>(dir.x * ind_scale),
                              ind_cy + static_cast<int>(dir.y * ind_scale));
            cv::Point neg_tip(ind_cx + static_cast<int>(-dir.x * ind_scale / 2.0f),
                              ind_cy + static_cast<int>(-dir.y * ind_scale / 2.0f));
            float dx = static_cast<float>(pos_tip.x - neg_tip.x), dy = static_cast<float>(pos_tip.y - neg_tip.y);
            float d = std::sqrt(dx * dx + dy * dy);
            float ux = dx / d, uy = dy / d;
            int arrow_len = 10;
            float dash_end = d - arrow_len;
            float pos = 0.0f;
            bool on = true;
            while (pos < dash_end) {
                float end = std::min(pos + (on ? 6.0f : 4.0f), dash_end);
                if (on) {
                    cv::Point a(neg_tip.x + static_cast<int>(ux * pos), neg_tip.y + static_cast<int>(uy * pos));
                    cv::Point b(neg_tip.x + static_cast<int>(ux * end), neg_tip.y + static_cast<int>(uy * end));
                    cv::line(canvas, a, b, ind_color, 1, cv::LINE_AA);
                }
                pos = end;
                on = !on;
            }
            cv::Point arrow_start(neg_tip.x + static_cast<int>(ux * dash_end),
                                  neg_tip.y + static_cast<int>(uy * dash_end));
            cv::arrowedLine(canvas, arrow_start, pos_tip, ind_color, 1, cv::LINE_AA, 0, 0.4);
            cv::Point2f perp(-dir.y, dir.x);
            cv::Point2f label_pos(pos_tip.x + dir.x * 8 + perp.x * 8, pos_tip.y + dir.y * 8 + perp.y * 8);
            cv::putText(canvas, ax.label,
                        cv::Point(static_cast<int>(label_pos.x) - 4, static_cast<int>(label_pos.y) + 4),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, ind_color, 1, cv::LINE_AA);
        }
    }

    draw_detection_boxes_perspective(canvas, inbuf, rvec, tvec, K, dist_coeffs, cam_pos, zaxis, roi_w, roi_h,
                                     assoc_ids);
}

/* Overlays 3D bounding box footprints onto the BEV canvas.
 * Matched boxes (associated with a camera detection) are drawn in white; unmatched in green. */
void draw_detection_boxes_bev(cv::Mat &canvas, GstBuffer *inbuf, const GstG3DRender *self, int roi_w, int roi_h,
                              const std::unordered_set<guint> &assoc_ids) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(inbuf);
    if (!rmeta)
        return;

    GstAnalyticsMtdType type = gst_analytics_3d_od_mtd_get_mtd_type();
    GstAnalytics3DODMtd mtd;
    gpointer state = nullptr;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, type, reinterpret_cast<GstAnalyticsMtd *>(&mtd))) {
        gfloat cx, cy, cz, length, width, height, yaw, pitch, roll;
        gst_analytics_3d_od_mtd_get_location(&mtd, &cx, &cy, &cz, &length, &width, &height, &yaw, &pitch, &roll);

        bool matched = assoc_ids.count(mtd.id) > 0;
        cv::Scalar color = matched ? kColorMatched : kColorUnmatched;
        int thickness = 1;

        float cos_t = std::cos(yaw), sin_t = -std::sin(yaw);
        float hx = width / 2.0f, hy = length / 2.0f;
        float wx[4] = {cx + cos_t * hx - sin_t * hy, cx - cos_t * hx - sin_t * hy, cx - cos_t * hx + sin_t * hy,
                       cx + cos_t * hx + sin_t * hy};
        float wy[4] = {cy + sin_t * hx + cos_t * hy, cy - sin_t * hx + cos_t * hy, cy - sin_t * hx - cos_t * hy,
                       cy + sin_t * hx - cos_t * hy};
        std::vector<cv::Point> poly(4);
        for (int k = 0; k < 4; ++k)
            poly[k] = world_to_pixel_bev(wx[k], wy[k], self, roi_w, roi_h);
        cv::polylines(canvas, poly, true, color, thickness);
        cv::circle(canvas, world_to_pixel_bev(cx, cy, self, roi_w, roi_h), 4, color, -1);
    }
}

/* Draws 2D detection boxes from camera buffer analytics meta onto cell, scaled to cell dimensions.
 * If skip_matched is true, boxes that have an IS_PART_OF association to a TrackingMtd are skipped
 * (used in cam-proj mode to avoid overdrawing the white 3D box already drawn for that object). */
void draw_2d_boxes(cv::Mat &cell, GstBuffer *cam_buf, int orig_w, int orig_h, bool skip_matched = false) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(cam_buf);
    if (!rmeta)
        return;

    float sx = static_cast<float>(cell.cols) / orig_w;
    float sy = static_cast<float>(cell.rows) / orig_h;

    GstAnalyticsODMtd mtd;
    gpointer state = nullptr;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(), &mtd)) {
        gint bx = 0, by = 0, bw = 0, bh = 0;
        gfloat conf = 0.f;
        if (!gst_analytics_od_mtd_get_location(&mtd, &bx, &by, &bw, &bh, &conf))
            continue;
        int x = static_cast<int>(bx * sx), y = static_cast<int>(by * sy);
        int w = static_cast<int>(bw * sx), h = static_cast<int>(bh * sy);
        if (w <= 0 || h <= 0)
            continue;
        x = std::max(0, x);
        y = std::max(0, y);
        w = std::min(w, cell.cols - x);
        h = std::min(h, cell.rows - y);

        if (skip_matched) {
            GstAnalyticsTrackingMtd tmtd;
            gpointer state2 = nullptr;
            bool matched =
                gst_analytics_relation_meta_get_direct_related(rmeta, mtd.id, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                               gst_analytics_tracking_mtd_get_mtd_type(), &state2,
                                                               reinterpret_cast<GstAnalyticsMtd *>(&tmtd)) == TRUE;
            if (matched)
                continue;
        }

        cv::rectangle(cell, cv::Rect(x, y, w, h), kColor2DUnmatched, 1);
    }
}

/* Extracts the LiDAR sub-buffer from a GstAnalyticsBatchMeta batch buffer.
 * Returns nullptr if no stream with caps "application/x-lidar" is found. */
GstBuffer *extract_lidar_from_batch(GstBuffer *batch_buf) {
    GstAnalyticsBatchMeta *batch = gst_buffer_get_analytics_batch_meta(batch_buf);
    if (!batch)
        return nullptr;
    for (gsize i = 0; i < batch->n_streams; ++i) {
        GstAnalyticsBatchStream *stream = &batch->streams[i];
        if (stream->n_objects == 0 || !GST_IS_BUFFER(stream->objects[0]))
            continue;
        GstCaps *caps = gst_analytics_batch_stream_get_caps(stream);
        if (!caps || gst_caps_get_size(caps) == 0)
            continue;
        const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
        if (g_strcmp0(name, "application/x-lidar") == 0)
            return GST_BUFFER_CAST(stream->objects[0]);
    }
    return nullptr;
}

struct CamStream {
    GstBuffer *buf;
    gint w;
    gint h;
    GstVideoInfo vinfo;
};

/* Scans camera buffers for IS_PART_OF TrackingMtd relations on OD detections and returns
 * the set of associated LiDAR 3D OD mtd ids, used to distinguish matched vs. unmatched boxes. */
std::unordered_set<guint> collect_associated_3d_ids(const std::vector<CamStream> &cams) {
    std::unordered_set<guint> ids;
    for (const auto &cs : cams) {
        GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(cs.buf);
        if (!rmeta)
            continue;
        GstAnalyticsODMtd od;
        gpointer state = nullptr;
        while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(),
                                                   reinterpret_cast<GstAnalyticsMtd *>(&od))) {
            GstAnalyticsTrackingMtd tmtd;
            gpointer state2 = nullptr;
            while (gst_analytics_relation_meta_get_direct_related(rmeta, od.id, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                                  gst_analytics_tracking_mtd_get_mtd_type(), &state2,
                                                                  reinterpret_cast<GstAnalyticsMtd *>(&tmtd))) {
                guint64 ref_id = 0;
                GstClockTime first_seen, last_seen;
                gboolean lost;
                if (gst_analytics_tracking_mtd_get_info(&tmtd, &ref_id, &first_seen, &last_seen, &lost))
                    ids.insert(static_cast<guint>(ref_id));
            }
        }
    }
    return ids;
}

/* Collects all video/x-raw sub-buffers and their metadata from a batch buffer.
 * Each returned CamStream holds the buffer, decoded dimensions, and GstVideoInfo. */
std::vector<CamStream> collect_camera_streams(GstBuffer *batch_buf) {
    std::vector<CamStream> cams;
    GstAnalyticsBatchMeta *batch = gst_buffer_get_analytics_batch_meta(batch_buf);
    if (!batch)
        return cams;
    for (gsize i = 0; i < batch->n_streams; ++i) {
        GstAnalyticsBatchStream *stream = &batch->streams[i];
        if (stream->n_objects == 0 || !GST_IS_BUFFER(stream->objects[0]))
            continue;
        GstCaps *caps = gst_analytics_batch_stream_get_caps(stream);
        if (!caps || gst_caps_get_size(caps) == 0)
            continue;
        const GstStructure *s = gst_caps_get_structure(caps, 0);
        if (g_strcmp0(gst_structure_get_name(s), "video/x-raw") != 0)
            continue;
        GstVideoInfo vinfo;
        if (!gst_video_info_from_caps(&vinfo, caps))
            continue;
        gint w = GST_VIDEO_INFO_WIDTH(&vinfo);
        gint h = GST_VIDEO_INFO_HEIGHT(&vinfo);
        if (w > 0 && h > 0)
            cams.push_back({GST_BUFFER_CAST(stream->objects[0]), w, h, vinfo});
    }
    return cams;
}

cv::Mat decode_cam_frame(const CamStream &cs); /* forward declaration */

/* Projects LiDAR point cloud and 3D detection boxes onto the camera image using stored
 * calibration matrices (P2 * R0 * Tr). Falls back to BEV rendering if calibration has not
 * been received or the camera image cannot be decoded. */

/* Jet-like colormap: near (t=0) → red, far (t=1) → blue (BGR). */
cv::Vec3b depth_to_color(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    uint8_t r, g, b;
    if (t < 0.25f) {
        float s = t * 4.0f;
        r = 255;
        g = static_cast<uint8_t>(s * 255);
        b = 0;
    } else if (t < 0.5f) {
        float s = (t - 0.25f) * 4.0f;
        r = static_cast<uint8_t>((1.0f - s) * 255);
        g = 255;
        b = 0;
    } else if (t < 0.75f) {
        float s = (t - 0.5f) * 4.0f;
        r = 0;
        g = 255;
        b = static_cast<uint8_t>(s * 255);
    } else {
        float s = (t - 0.75f) * 4.0f;
        r = 0;
        g = static_cast<uint8_t>((1.0f - s) * 255);
        b = 255;
    }
    return cv::Vec3b(b, g, r);
}

/* Project LiDAR point cloud and 3D detection boxes onto a selected camera image.
 * The camera is chosen by self->cam_proj_index; clamped to the last available stream
 * if the index exceeds the number of cameras in the batch.
 * Requires calibration received via the g3d/calibration sticky event.
 * Falls back to BEV if calibration or camera image is unavailable. */
void draw_cam_projection(cv::Mat &canvas, const float *points, guint count, GstBuffer *lidar_buf, GstG3DRender *self,
                         const std::vector<CamStream> &cams, int roi_w, int roi_h,
                         const std::unordered_set<guint> &assoc_ids) {
    if (!self->has_calib || cams.empty()) {
        draw_bev(canvas, points, count, self, roi_w, roi_h);
        draw_detection_boxes_bev(canvas, lidar_buf, self, roi_w, roi_h, assoc_ids);
        return;
    }

    /* Select the requested camera, clamped to the last available stream. */
    int cam_idx = std::min(self->cam_proj_index, static_cast<gint>(cams.size()) - 1);
    const CamStream &proj_cam = cams[cam_idx];

    /* --- Background: selected camera image letterboxed into the canvas --- */
    cv::Mat cam_bgr = decode_cam_frame(proj_cam);
    if (cam_bgr.empty()) {
        draw_bev(canvas, points, count, self, roi_w, roi_h);
        draw_detection_boxes_bev(canvas, lidar_buf, self, roi_w, roi_h, assoc_ids);
        return;
    }

    float scale_lb = std::min(static_cast<float>(roi_w) / cam_bgr.cols, static_cast<float>(roi_h) / cam_bgr.rows);
    int sw = static_cast<int>(cam_bgr.cols * scale_lb);
    int sh = static_cast<int>(cam_bgr.rows * scale_lb);
    int xp = (roi_w - sw) / 2;
    int yp = (roi_h - sh) / 2;

    canvas.setTo(cv::Scalar(0, 0, 0));
    cv::Mat scaled_cam;
    cv::resize(cam_bgr, scaled_cam, cv::Size(sw, sh));
    if (self->cam_bg_grayscale) {
        cv::Mat gray;
        cv::cvtColor(scaled_cam, gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(gray, scaled_cam, cv::COLOR_GRAY2BGR);
    }
    if (self->cam_bg_dim < 1.0f)
        scaled_cam.convertTo(scaled_cam, -1, self->cam_bg_dim, 0);
    scaled_cam.copyTo(canvas(cv::Rect(xp, yp, sw, sh)));

    /* --- Build projection matrices from stored calibration --- */
    cv::Mat Tr(4, 4, CV_32FC1);
    cv::Mat R0(4, 4, CV_32FC1);
    cv::Mat P2(3, 4, CV_32FC1);
    std::memcpy(Tr.data, self->calib_tr, 16 * sizeof(float));
    std::memcpy(R0.data, self->calib_r0, 16 * sizeof(float));
    std::memcpy(P2.data, self->calib_p2, 12 * sizeof(float));

    /* proj (3×4) = P2 * R0 * Tr; maps LiDAR [x,y,z,1]^T → homogeneous image [U,V,W]^T */
    cv::Mat R0Tr = R0 * Tr;   /* 4×4 */
    cv::Mat proj = P2 * R0Tr; /* 3×4 */

    /* Extract matrix rows for fast per-point scalar math. */
    float p0 = proj.at<float>(0, 0), p1 = proj.at<float>(0, 1), p2f = proj.at<float>(0, 2), p3 = proj.at<float>(0, 3);
    float q0 = proj.at<float>(1, 0), q1 = proj.at<float>(1, 1), q2f = proj.at<float>(1, 2), q3 = proj.at<float>(1, 3);
    float w0 = proj.at<float>(2, 0), w1 = proj.at<float>(2, 1), w2f = proj.at<float>(2, 2), w3 = proj.at<float>(2, 3);
    /* Third row of R0Tr gives the depth (z in rectified camera frame). */
    float d0 = R0Tr.at<float>(2, 0), d1 = R0Tr.at<float>(2, 1), d2 = R0Tr.at<float>(2, 2), d3 = R0Tr.at<float>(2, 3);

    constexpr float kMaxDepth = 60.0f;

    /* --- Project LiDAR point cloud --- */
    struct ProjPt {
        cv::Point2i px;
        float depth;
    };
    std::vector<ProjPt> visible;
    visible.reserve(count / static_cast<guint>(self->point_stride) + 1);

    for (guint i = 0; i < count; i += static_cast<guint>(self->point_stride)) {
        float x = points[i * 4 + 0], y = points[i * 4 + 1], z = points[i * 4 + 2];

        float depth = d0 * x + d1 * y + d2 * z + d3;
        if (depth <= 0.5f)
            continue;

        float W = w0 * x + w1 * y + w2f * z + w3;
        if (W <= 0.f)
            continue;

        float u = (p0 * x + p1 * y + p2f * z + p3) / W;
        float v = (q0 * x + q1 * y + q2f * z + q3) / W;

        int cx = static_cast<int>(u * scale_lb) + xp;
        int cy = static_cast<int>(v * scale_lb) + yp;
        if (cx < xp || cx >= xp + sw || cy < yp || cy >= yp + sh)
            continue;

        visible.push_back({cv::Point2i(cx, cy), depth});
    }

    /* Sort far-to-near so near points overdraw far ones. */
    std::sort(visible.begin(), visible.end(), [](const ProjPt &a, const ProjPt &b) { return a.depth > b.depth; });

    for (const auto &pp : visible) {
        cv::Vec3b c = depth_to_color(pp.depth / kMaxDepth);
        if (self->point_radius <= 1) {
            canvas.at<cv::Vec3b>(pp.px.y, pp.px.x) = c;
        } else {
            int r = std::max(1, static_cast<int>(self->point_radius * 15.0f / pp.depth));
            cv::circle(canvas, pp.px, r, cv::Scalar(c[0], c[1], c[2]), -1);
        }
    }

    /* --- Project 3D detection boxes --- */
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(lidar_buf);
    if (!rmeta)
        return;

    GstAnalytics3DODMtd mtd;
    gpointer state = nullptr;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_3d_od_mtd_get_mtd_type(),
                                               reinterpret_cast<GstAnalyticsMtd *>(&mtd))) {
        gfloat bcx, bcy, bcz, length, width, height, yaw, pitch, roll;
        gst_analytics_3d_od_mtd_get_location(&mtd, &bcx, &bcy, &bcz, &length, &width, &height, &yaw, &pitch, &roll);

        float cos_t = std::cos(yaw), sin_t = -std::sin(yaw);
        float hx = width / 2.0f, hy = length / 2.0f, hz = height / 2.0f;
        float lx[4] = {-hx, +hx, +hx, -hx};
        float ly[4] = {-hy, -hy, +hy, +hy};

        /* Build 4×8 homogeneous corner matrix (columns = corners). */
        cv::Mat corners(4, 8, CV_32FC1);
        for (int k = 0; k < 4; ++k) {
            float wx = bcx + lx[k] * cos_t - ly[k] * sin_t;
            float wy = bcy + lx[k] * sin_t + ly[k] * cos_t;
            for (int top = 0; top < 2; ++top) {
                int col = k + top * 4;
                corners.at<float>(0, col) = wx;
                corners.at<float>(1, col) = wy;
                corners.at<float>(2, col) = bcz + (top == 0 ? -hz : hz);
                corners.at<float>(3, col) = 1.0f;
            }
        }

        /* Check center depth; skip boxes behind camera. */
        float center_depth = d0 * bcx + d1 * bcy + d2 * bcz + d3;
        if (center_depth <= 0.5f)
            continue;

        cv::Mat img_hom = proj * corners; /* 3×8 */
        std::vector<cv::Point2i> pts2d(8);
        bool all_valid = true;
        for (int k = 0; k < 8; ++k) {
            float W_k = img_hom.at<float>(2, k);
            if (W_k <= 0.f) {
                all_valid = false;
                break;
            }
            float u_k = img_hom.at<float>(0, k) / W_k;
            float v_k = img_hom.at<float>(1, k) / W_k;
            pts2d[k] = cv::Point2i(static_cast<int>(u_k * scale_lb) + xp, static_cast<int>(v_k * scale_lb) + yp);
        }
        if (!all_valid)
            continue;

        bool matched = assoc_ids.count(mtd.id) > 0;
        cv::Scalar color = matched ? kColorMatched : kColorUnmatched;
        int thickness = 1;

        static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto &e : edges)
            cv::line(canvas, pts2d[e[0]], pts2d[e[1]], color, thickness, cv::LINE_AA);
    }

    /* --- Draw 2D detection boxes on top of the letterboxed region --- */
    /* Only draw unmatched 2D boxes; matched objects are shown as white 3D boxes above. */
    cv::Mat cam_region = canvas(cv::Rect(xp, yp, sw, sh));
    draw_2d_boxes(cam_region, proj_cam.buf, proj_cam.w, proj_cam.h, /*skip_matched=*/true);
}

/* Maps a camera sub-buffer into a BGR cv::Mat, handling BGR, BGRx/BGRA, and NV12 formats.
 * Returns an empty Mat if the buffer cannot be mapped or the format is unsupported. */
cv::Mat decode_cam_frame(const CamStream &cs) {
    GstVideoFrame vframe;
    if (!gst_video_frame_map(&vframe, const_cast<GstVideoInfo *>(&cs.vinfo), cs.buf, GST_MAP_READ))
        return {};

    cv::Mat cam_bgr;
    GstVideoFormat fmt = GST_VIDEO_FRAME_FORMAT(&vframe);

    if (fmt == GST_VIDEO_FORMAT_BGR) {
        cv::Mat src(cs.h, cs.w, CV_8UC3, GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0),
                    GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0));
        src.copyTo(cam_bgr);
    } else if (fmt == GST_VIDEO_FORMAT_BGRx || fmt == GST_VIDEO_FORMAT_BGRA) {
        cv::Mat src(cs.h, cs.w, CV_8UC4, GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0),
                    GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0));
        cv::cvtColor(src, cam_bgr, cv::COLOR_BGRA2BGR);
    } else if (fmt == GST_VIDEO_FORMAT_NV12) {
        int y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
        int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 1);
        const guint8 *y_data = (const guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0);
        const guint8 *uv_data = (const guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 1);
        int h2 = (cs.h / 2) * 2, w2 = (cs.w / 2) * 2;
        if (h2 > 0 && w2 > 0) {
            std::vector<guint8> packed((size_t)w2 * h2 * 3 / 2);
            for (int r = 0; r < h2; r++)
                std::memcpy(packed.data() + (size_t)r * w2, y_data + (size_t)r * y_stride, w2);
            for (int r = 0; r < h2 / 2; r++)
                std::memcpy(packed.data() + (size_t)w2 * h2 + (size_t)r * w2, uv_data + (size_t)r * uv_stride, w2);
            cv::Mat yuv(h2 * 3 / 2, w2, CV_8UC1, packed.data());
            cv::cvtColor(yuv, cam_bgr, cv::COLOR_YUV2BGR_NV12);
        }
    }

    gst_video_frame_unmap(&vframe);
    return cam_bgr;
}

} // namespace

static GstFlowReturn gst_g3d_render_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstG3DRender *self = GST_G3D_RENDER(trans);
    // Extract streams from batch (or treat inbuf as direct lidar)
    GstBuffer *lidar_buf = inbuf;
    std::vector<CamStream> cams;
    if (self->input_is_batch) {
        lidar_buf = extract_lidar_from_batch(inbuf);
        cams = collect_camera_streams(inbuf);
        if (!lidar_buf)
            GST_WARNING_OBJECT(self, "No lidar stream in batch, outputting blank frame");
    }

    GstMapInfo out_map;
    if (!gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT(self, "Failed to map output buffer for writing");
        return GST_FLOW_ERROR;
    }

    cv::Mat canvas(self->height, self->width, CV_8UC3, out_map.data);
    canvas.setTo(cv::Scalar(15, 15, 15));

    // ── Layout ──────────────────────────────────────────────────────────────
    // cam-proj mode: projection fills the full canvas; no separate camera panel.
    // bev/perspective:  LiDAR renders in a height×height square on the right,
    //                   camera(s) fill the remaining width on the left.
    int n_cams = static_cast<int>(cams.size());
    int lidar_x, lidar_w, lidar_h, cam_area_w;
    if (self->view_mode == 2) {
        lidar_x = 0;
        lidar_w = self->width;
        lidar_h = self->height;
        cam_area_w = 0;
    } else {
        int lidar_size = self->height;
        lidar_x = (n_cams > 0) ? std::max(0, self->width - lidar_size) : 0;
        lidar_w = std::min(lidar_size, self->width - lidar_x);
        lidar_h = lidar_size;
        cam_area_w = lidar_x;
    }

    // ── Render LiDAR ────────────────────────────────────────────────────────
    if (lidar_buf) {
        LidarMeta *lidar_meta = reinterpret_cast<LidarMeta *>(gst_buffer_get_meta(lidar_buf, LIDAR_META_API_TYPE));

        if (lidar_meta) {
            GstMapInfo in_map;
            if (!gst_buffer_map(lidar_buf, &in_map, GST_MAP_READ)) {
                GST_ERROR_OBJECT(self, "Failed to map lidar input buffer");
                gst_buffer_unmap(outbuf, &out_map);
                return GST_FLOW_ERROR;
            }

            const float *points = reinterpret_cast<const float *>(in_map.data);
            const guint count = lidar_meta->lidar_point_count;

            cv::Mat lidar_roi = canvas(cv::Rect(lidar_x, 0, lidar_w, lidar_h));

            auto assoc_ids = collect_associated_3d_ids(cams);
            if (self->view_mode == 0) {
                draw_bev(lidar_roi, points, count, self, lidar_w, lidar_h);
                draw_detection_boxes_bev(lidar_roi, lidar_buf, self, lidar_w, lidar_h, assoc_ids);
            } else if (self->view_mode == 1) {
                draw_perspective(lidar_roi, points, count, lidar_buf, self, lidar_w, lidar_h, assoc_ids);
            } else {
                draw_cam_projection(lidar_roi, points, count, lidar_buf, self, cams, lidar_w, lidar_h, assoc_ids);
            }

            gst_buffer_unmap(lidar_buf, &in_map);
        } else {
            GST_WARNING_OBJECT(self, "No LidarMeta on lidar buffer");
        }
    }

    // ── Render camera streams ────────────────────────────────────────────────
    if (n_cams > 0 && cam_area_w > 0) {
        int cols = (n_cams >= 4) ? 2 : 1;
        int rows = (n_cams + cols - 1) / cols;
        int cell_w = cam_area_w / cols;
        int cell_h = self->height / rows;

        for (int i = 0; i < n_cams; ++i) {
            CamStream &cs = cams[i];
            int col = i % cols;
            int row = i / cols;
            int x_off = col * cell_w;
            int y_off = row * cell_h;

            cv::Mat cam_bgr = decode_cam_frame(cs);
            if (cam_bgr.empty()) {
                GST_WARNING_OBJECT(self, "Failed to decode camera buffer %d", i);
                continue;
            }

            // Letterbox: scale to fit cell while preserving aspect ratio
            float scale =
                std::min(static_cast<float>(cell_w) / cam_bgr.cols, static_cast<float>(cell_h) / cam_bgr.rows);
            int sw = static_cast<int>(cam_bgr.cols * scale);
            int sh = static_cast<int>(cam_bgr.rows * scale);
            int xp = (cell_w - sw) / 2;
            int yp = (cell_h - sh) / 2;

            cv::Mat cell(cell_h, cell_w, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::Mat scaled;
            cv::resize(cam_bgr, scaled, cv::Size(sw, sh));
            draw_2d_boxes(scaled, cs.buf, cs.w, cs.h);
            scaled.copyTo(cell(cv::Rect(xp, yp, sw, sh)));
            cell.copyTo(canvas(cv::Rect(x_off, y_off, cell_w, cell_h)));
            cv::putText(canvas, "Cam " + std::to_string(i), cv::Point(x_off + 4, y_off + 16), cv::FONT_HERSHEY_SIMPLEX,
                        0.45, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
        }

        cv::line(canvas, cv::Point(lidar_x, 0), cv::Point(lidar_x, self->height), cv::Scalar(80, 80, 80), 1);
    }

    gst_buffer_unmap(outbuf, &out_map);

    // ── PTS / duration ──────────────────────────────────────────────────────
    const GstClockTime frame_duration = self->frame_duration;
    GstClockTime src_pts = GST_CLOCK_TIME_NONE;
    if (lidar_buf && GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(lidar_buf)))
        src_pts = GST_BUFFER_PTS(lidar_buf);
    else if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(inbuf)))
        src_pts = GST_BUFFER_PTS(inbuf);

    GstClockTime src_dur = GST_CLOCK_TIME_NONE;
    if (lidar_buf && GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(lidar_buf)))
        src_dur = GST_BUFFER_DURATION(lidar_buf);
    else if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(inbuf)))
        src_dur = GST_BUFFER_DURATION(inbuf);

    GST_BUFFER_PTS(outbuf) = GST_CLOCK_TIME_IS_VALID(src_pts) ? src_pts : self->frame_count * frame_duration;
    GST_BUFFER_DURATION(outbuf) = GST_CLOCK_TIME_IS_VALID(src_dur) ? src_dur : frame_duration;
    self->frame_count++;

    GST_DEBUG_OBJECT(self, "rendered frame %lu pts=%" GST_TIME_FORMAT, (unsigned long)self->frame_count,
                     GST_TIME_ARGS(GST_BUFFER_PTS(outbuf)));
    return GST_FLOW_OK;
}
