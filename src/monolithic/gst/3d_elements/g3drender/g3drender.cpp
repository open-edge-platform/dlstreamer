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
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_render_debug);
#define GST_CAT_DEFAULT gst_g3d_render_debug

GType gst_g3d_render_view_mode_get_type(void) {
    static GType type = 0;
    if (!type) {
        static const GEnumValue values[] = {
            {0, "Bird's Eye View", "bev"},
            {1, "Perspective 3D",  "perspective"},
            {0, NULL, NULL}
        };
        type = g_enum_register_static("GstG3DRenderViewMode", values);
    }
    return type;
}

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_RANGE_X_MIN,
    PROP_RANGE_X_MAX,
    PROP_RANGE_Y_MIN,
    PROP_RANGE_Y_MAX,
    PROP_POINT_RADIUS,
    PROP_POINT_STRIDE,
    PROP_ZOOM,
    PROP_VIEW_MODE,
    PROP_CAM_DISTANCE,
    PROP_CAM_ELEVATION,
    PROP_CAM_AZIMUTH,
    PROP_CAM_AZIMUTH_STEP,
    PROP_CAM_FOV,
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "application/x-lidar; "
            "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
        GST_STATIC_CAPS("video/x-raw, format=BGR, "
                        "width=[ 1, 32767 ], height=[ 1, 32767 ], "
                        "framerate=(fraction)[ 0/1, 120/1 ]"));

static void          gst_g3d_render_set_property(GObject *obj, guint prop_id, const GValue *val, GParamSpec *pspec);
static void          gst_g3d_render_get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec);
static gboolean      gst_g3d_render_start(GstBaseTransform *trans);
static gboolean      gst_g3d_render_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstCaps      *gst_g3d_render_transform_caps(GstBaseTransform *trans, GstPadDirection direction,
                                                    GstCaps *caps, GstCaps *filter);
static gboolean      gst_g3d_render_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
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
        g_param_spec_int("width", "Width", "Output image width in pixels",
            1, 32767, 800,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_HEIGHT,
        g_param_spec_int("height", "Height", "Output image height in pixels",
            1, 32767, 800,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_RANGE_X_MIN,
        g_param_spec_float("range-x-min", "Range X Min",
            "Minimum X coordinate of point cloud range (meters)",
            -G_MAXFLOAT, 0.0f, -50.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_RANGE_X_MAX,
        g_param_spec_float("range-x-max", "Range X Max",
            "Maximum X coordinate of point cloud range (meters)",
            0.0f, G_MAXFLOAT, 50.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_RANGE_Y_MIN,
        g_param_spec_float("range-y-min", "Range Y Min",
            "Minimum Y coordinate of point cloud range (meters)",
            -G_MAXFLOAT, 0.0f, -50.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_RANGE_Y_MAX,
        g_param_spec_float("range-y-max", "Range Y Max",
            "Maximum Y coordinate of point cloud range (meters)",
            0.0f, G_MAXFLOAT, 50.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_POINT_RADIUS,
        g_param_spec_int("point-radius", "Point Radius",
            "Radius of each rendered point in pixels",
            1, 20, 2,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_POINT_STRIDE,
        g_param_spec_int("point-stride", "Point Stride",
            "Render every Nth point (1 = all points, 4 = every 4th point, etc.)",
            1, 100, 1,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_ZOOM,
        g_param_spec_float("zoom", "Zoom",
            "BEV zoom factor: 1.0=default (50m range), 2.0=zoomed in (25m range), 0.5=zoomed out (100m range)",
            0.1f, 20.0f, 1.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_VIEW_MODE,
        g_param_spec_enum("view-mode", "View Mode",
            "Rendering mode: bev (Bird's Eye View) or perspective (3D perspective)",
            GST_TYPE_G3D_RENDER_VIEW_MODE, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_DISTANCE,
        g_param_spec_float("cam-distance", "Camera Distance",
            "Camera distance from origin in meters (perspective mode)",
            1.0f, 500.0f, 40.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_ELEVATION,
        g_param_spec_float("cam-elevation", "Camera Elevation",
            "Camera elevation angle in degrees: 0=horizon 90=top-down (perspective mode)",
            5.0f, 89.0f, 30.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_AZIMUTH,
        g_param_spec_float("cam-azimuth", "Camera Azimuth",
            "Camera horizontal angle in degrees (perspective mode)",
            -360.0f, 360.0f, 45.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_AZIMUTH_STEP,
        g_param_spec_float("cam-azimuth-step", "Camera Azimuth Step",
            "Azimuth increment per frame in degrees for rotation animation; 0=static (perspective mode)",
            -10.0f, 10.0f, 0.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_CAM_FOV,
        g_param_spec_float("cam-fov", "Camera FOV",
            "Field of view in degrees (perspective mode)",
            10.0f, 150.0f, 60.0f,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class,
        "G3D LiDAR Renderer", "Filter/Converter",
        "Renders LiDAR point cloud as BEV or perspective 3D video frame (g3drender)",
        "Intel Corporation");

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    bt_class->start                 = GST_DEBUG_FUNCPTR(gst_g3d_render_start);
    bt_class->sink_event            = GST_DEBUG_FUNCPTR(gst_g3d_render_sink_event);
    bt_class->transform_caps        = GST_DEBUG_FUNCPTR(gst_g3d_render_transform_caps);
    bt_class->set_caps              = GST_DEBUG_FUNCPTR(gst_g3d_render_set_caps);
    bt_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_g3d_render_prepare_output_buffer);
    bt_class->transform             = GST_DEBUG_FUNCPTR(gst_g3d_render_transform);
    bt_class->passthrough_on_same_caps = FALSE;
}

static void gst_g3d_render_init(GstG3DRender *self) {
    self->width            = 800;
    self->height           = 800;
    self->range_x_min      = -50.0f;
    self->range_x_max      =  50.0f;
    self->range_y_min      = -50.0f;
    self->range_y_max      =  50.0f;
    self->point_radius     = 16;
    self->point_stride     = 1;
    self->zoom             = 1.0f;
    self->view_mode        = 0;
    self->cam_distance     = 40.0f;
    self->cam_elevation    = 30.0f;
    self->cam_azimuth      = 45.0f;
    self->cam_azimuth_step = 0.0f;
    self->cam_fov          = 60.0f;
    self->frame_count      = 0;
    self->input_is_batch   = FALSE;
}

static gboolean gst_g3d_render_start(GstBaseTransform *trans) {
    GstG3DRender *self = GST_G3D_RENDER(trans);
    self->frame_count = 0;
    return TRUE;
}

static gboolean gst_g3d_render_sink_event(GstBaseTransform *trans, GstEvent *event) {
    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
        const GstSegment *seg;
        gst_event_parse_segment(event, &seg);
        if (seg->format != GST_FORMAT_TIME) {
            GstSegment time_seg;
            gst_segment_init(&time_seg, GST_FORMAT_TIME);
            time_seg.start = 0; time_seg.stop = GST_CLOCK_TIME_NONE;
            time_seg.position = 0; time_seg.time = 0;
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
    case PROP_WIDTH:           self->width            = g_value_get_int(val);   break;
    case PROP_HEIGHT:          self->height           = g_value_get_int(val);   break;
    case PROP_RANGE_X_MIN:     self->range_x_min      = g_value_get_float(val); break;
    case PROP_RANGE_X_MAX:     self->range_x_max      = g_value_get_float(val); break;
    case PROP_RANGE_Y_MIN:     self->range_y_min      = g_value_get_float(val); break;
    case PROP_RANGE_Y_MAX:     self->range_y_max      = g_value_get_float(val); break;
    case PROP_POINT_RADIUS:    self->point_radius     = g_value_get_int(val);   break;
    case PROP_POINT_STRIDE:    self->point_stride     = g_value_get_int(val);   break;
    case PROP_ZOOM:            self->zoom             = g_value_get_float(val); break;
    case PROP_VIEW_MODE:       self->view_mode        = g_value_get_enum(val);  break;
    case PROP_CAM_DISTANCE:    self->cam_distance     = g_value_get_float(val); break;
    case PROP_CAM_ELEVATION:   self->cam_elevation    = g_value_get_float(val); break;
    case PROP_CAM_AZIMUTH:     self->cam_azimuth      = g_value_get_float(val); break;
    case PROP_CAM_AZIMUTH_STEP:self->cam_azimuth_step = g_value_get_float(val); break;
    case PROP_CAM_FOV:         self->cam_fov          = g_value_get_float(val); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
    }
}

static void gst_g3d_render_get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec) {
    GstG3DRender *self = GST_G3D_RENDER(obj);
    switch (prop_id) {
    case PROP_WIDTH:           g_value_set_int(val,   self->width);            break;
    case PROP_HEIGHT:          g_value_set_int(val,   self->height);           break;
    case PROP_RANGE_X_MIN:     g_value_set_float(val, self->range_x_min);      break;
    case PROP_RANGE_X_MAX:     g_value_set_float(val, self->range_x_max);      break;
    case PROP_RANGE_Y_MIN:     g_value_set_float(val, self->range_y_min);      break;
    case PROP_RANGE_Y_MAX:     g_value_set_float(val, self->range_y_max);      break;
    case PROP_POINT_RADIUS:    g_value_set_int(val,   self->point_radius);     break;
    case PROP_POINT_STRIDE:    g_value_set_int(val,   self->point_stride);     break;
    case PROP_ZOOM:            g_value_set_float(val, self->zoom);             break;
    case PROP_VIEW_MODE:       g_value_set_enum(val,  self->view_mode);        break;
    case PROP_CAM_DISTANCE:    g_value_set_float(val, self->cam_distance);     break;
    case PROP_CAM_ELEVATION:   g_value_set_float(val, self->cam_elevation);    break;
    case PROP_CAM_AZIMUTH:     g_value_set_float(val, self->cam_azimuth);      break;
    case PROP_CAM_AZIMUTH_STEP:g_value_set_float(val, self->cam_azimuth_step); break;
    case PROP_CAM_FOV:         g_value_set_float(val, self->cam_fov);          break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
    }
}

static GstCaps *gst_g3d_render_transform_caps(GstBaseTransform *trans, GstPadDirection direction,
                                               GstCaps *caps, GstCaps *filter) {
    (void)trans; (void)caps;
    GstG3DRender *self = GST_G3D_RENDER(trans);
    GstCaps *result;
    if (direction == GST_PAD_SINK) {
        result = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGR",
            "width",  G_TYPE_INT,    self->width,
            "height", G_TYPE_INT,    self->height,
            NULL);
        gst_caps_set_simple(result, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 120, 1, NULL);
    } else {
        result = gst_caps_from_string(
            "application/x-lidar; "
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
    (void)outcaps;
    GstG3DRender *self = GST_G3D_RENDER(trans);
    GstStructure *s = gst_caps_get_structure(incaps, 0);
    self->input_is_batch = (g_strcmp0(gst_structure_get_name(s),
                                      "multistream/x-analytics-batch") == 0);
    GST_INFO_OBJECT(self, "input caps: %s (batch=%d)",
                    gst_structure_get_name(s), self->input_is_batch);
    return TRUE;
}

static GstFlowReturn gst_g3d_render_prepare_output_buffer(GstBaseTransform *trans, GstBuffer *inbuf,
                                                           GstBuffer **outbuf) {
    (void)inbuf;
    GstG3DRender *self = GST_G3D_RENDER(trans);
    gsize frame_size = (gsize)self->width * self->height * 3;
    *outbuf = gst_buffer_new_allocate(NULL, frame_size, NULL);
    if (!*outbuf) {
        GST_ERROR_OBJECT(self, "Failed to allocate output buffer (%zu bytes)", frame_size);
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

// ── BEV rendering ────────────────────────────────────────────────────────────

static cv::Point world_to_pixel_bev(float x, float y, const GstG3DRender *self,
                                     int roi_w, int roi_h) {
    float x_range = (self->range_x_max - self->range_x_min) / self->zoom;
    float y_range = (self->range_y_max - self->range_y_min) / self->zoom;
    float x_mid   = (self->range_x_max + self->range_x_min) / 2.0f;
    float y_mid   = (self->range_y_max + self->range_y_min) / 2.0f;
    int px = (int)((y - (y_mid - y_range / 2.0f)) / y_range * roi_w);
    int py = (int)((1.0f - (x - (x_mid - x_range / 2.0f)) / x_range) * roi_h);
    return cv::Point(px, py);
}

static void draw_bev(cv::Mat &canvas, const float *points, guint count,
                     const GstG3DRender *self, int roi_w, int roi_h) {
    const cv::Scalar grid_color(55, 55, 55);
    cv::line(canvas, world_to_pixel_bev(0.0f, self->range_y_min, self, roi_w, roi_h),
                     world_to_pixel_bev(0.0f, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
    cv::line(canvas, world_to_pixel_bev(self->range_x_min, 0.0f, self, roi_w, roi_h),
                     world_to_pixel_bev(self->range_x_max, 0.0f, self, roi_w, roi_h), grid_color, 1);
    for (float d = 10.0f; d < self->range_x_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(-d, self->range_y_min, self, roi_w, roi_h),
                         world_to_pixel_bev(-d, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev( d, self->range_y_min, self, roi_w, roi_h),
                         world_to_pixel_bev( d, self->range_y_max, self, roi_w, roi_h), grid_color, 1);
    }
    for (float d = 10.0f; d < self->range_y_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(self->range_x_min, -d, self, roi_w, roi_h),
                         world_to_pixel_bev(self->range_x_max, -d, self, roi_w, roi_h), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev(self->range_x_min,  d, self, roi_w, roi_h),
                         world_to_pixel_bev(self->range_x_max,  d, self, roi_w, roi_h), grid_color, 1);
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
        cv::Point pos_tip(ind_cx + (int)( dir.x * ind_scale),        ind_cy + (int)( dir.y * ind_scale));
        cv::Point neg_tip(ind_cx + (int)(-dir.x * ind_scale / 2.0f), ind_cy + (int)(-dir.y * ind_scale / 2.0f));
        float dx = (float)(pos_tip.x - neg_tip.x), dy = (float)(pos_tip.y - neg_tip.y);
        float d = std::sqrt(dx * dx + dy * dy);
        float ux = dx / d, uy = dy / d;
        int arrow_len = 10;
        float dash_end = d - arrow_len;
        float pos = 0.0f; bool on = true;
        while (pos < dash_end) {
            float end = std::min(pos + (on ? 6.0f : 4.0f), dash_end);
            if (on) {
                cv::Point a(neg_tip.x + (int)(ux * pos), neg_tip.y + (int)(uy * pos));
                cv::Point b(neg_tip.x + (int)(ux * end), neg_tip.y + (int)(uy * end));
                cv::line(canvas, a, b, ind_color, 1, cv::LINE_AA);
            }
            pos = end; on = !on;
        }
        cv::Point arrow_start(neg_tip.x + (int)(ux * dash_end), neg_tip.y + (int)(uy * dash_end));
        cv::arrowedLine(canvas, arrow_start, pos_tip, ind_color, 1, cv::LINE_AA, 0, 0.4);
        cv::Point2f label_pos(pos_tip.x + dir.x * 10, pos_tip.y + dir.y * 10);
        cv::putText(canvas, label, cv::Point((int)label_pos.x - 5, (int)label_pos.y + 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, ind_color, 1, cv::LINE_AA);
    };

    draw_bev_axis(cv::Point2f(0.0f, -1.0f), "X");
    draw_bev_axis(cv::Point2f(-1.0f, 0.0f), "Y");

    float x_range = (self->range_x_max - self->range_x_min) / self->zoom;
    float y_range = (self->range_y_max - self->range_y_min) / self->zoom;
    float x_mid   = (self->range_x_max + self->range_x_min) / 2.0f;
    float y_mid   = (self->range_y_max + self->range_y_min) / 2.0f;
    float x_off   = x_mid - x_range / 2.0f;
    float y_off   = y_mid - y_range / 2.0f;
    float inv_y   = roi_w / y_range;

    if (self->point_radius == 1) {
        for (guint i = 0; i < count; i += (guint)self->point_stride) {
            float x = points[i*4+0], y = points[i*4+1], intensity = points[i*4+3];
            int px = (int)((y - y_off) * inv_y);
            int py = (int)((1.0f - (x - x_off) / x_range) * roi_h);
            if (px < 0 || px >= roi_w || py < 0 || py >= roi_h) continue;
            uint8_t r = (uint8_t)(intensity * 255.0f);
            uint8_t b = (uint8_t)((1.0f - intensity) * 255.0f);
            canvas.at<cv::Vec3b>(py, px) = cv::Vec3b(b, 80, r);
        }
    } else {
        for (guint i = 0; i < count; i += (guint)self->point_stride) {
            float x = points[i*4+0], y = points[i*4+1], intensity = points[i*4+3];
            int px = (int)((y - y_off) * inv_y);
            int py = (int)((1.0f - (x - x_off) / x_range) * roi_h);
            if (px < 0 || px >= roi_w || py < 0 || py >= roi_h) continue;
            uint8_t r = (uint8_t)(intensity * 255.0f);
            uint8_t b = (uint8_t)((1.0f - intensity) * 255.0f);
            cv::circle(canvas, cv::Point(px, py), self->point_radius, cv::Scalar(b, 80, r), -1);
        }
    }
}

// ── Perspective rendering ─────────────────────────────────────────────────────

static cv::Vec3b height_to_color(float z) {
    float t = (z + 2.0f) / 6.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        float s = t * 2.0f;
        return cv::Vec3b((uint8_t)((1.0f - s) * 200), (uint8_t)(s * 180), (uint8_t)(s * 80));
    }
    float s = (t - 0.5f) * 2.0f;
    return cv::Vec3b(0, (uint8_t)((1.0f - s) * 180), (uint8_t)(s * 220 + 80));
}

static void draw_detection_boxes_perspective(cv::Mat &canvas, GstBuffer *inbuf,
                                              const cv::Mat &rvec, const cv::Mat &tvec,
                                              const cv::Mat &K, const cv::Mat &dist_coeffs,
                                              const cv::Mat &cam_pos, const cv::Mat &zaxis,
                                              int roi_w, int roi_h) {
    double zx = zaxis.at<double>(0), zy = zaxis.at<double>(1), zz = zaxis.at<double>(2);
    double cpx = cam_pos.at<double>(0), cpy = cam_pos.at<double>(1), cpz = cam_pos.at<double>(2);

    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(inbuf);
    if (!rmeta) return;

    GstAnalyticsMtdType type = gst_analytics_3d_od_mtd_get_mtd_type();
    GstAnalytics3DODMtd mtd;
    gpointer state = NULL;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, type,
                                               reinterpret_cast<GstAnalyticsMtd *>(&mtd))) {
        gfloat bcx, bcy, bcz, length, width, height, yaw, pitch, roll;
        gst_analytics_3d_od_mtd_get_location(&mtd, &bcx, &bcy, &bcz,
                                              &length, &width, &height,
                                              &yaw, &pitch, &roll);

        float cos_t = std::cos(yaw), sin_t = -std::sin(yaw);
        float hx = width / 2.0f, hy = length / 2.0f, hz = height / 2.0f;

        float lx[4] = {-hx, +hx, +hx, -hx};
        float ly[4] = {-hy, -hy, +hy, +hy};

        std::vector<cv::Point3f> corners(8);
        for (int i = 0; i < 4; ++i) {
            float wx = bcx + lx[i] * cos_t - ly[i] * sin_t;
            float wy = bcy + lx[i] * sin_t + ly[i] * cos_t;
            corners[i]   = cv::Point3f(wx, wy, bcz - hz);
            corners[i+4] = cv::Point3f(wx, wy, bcz + hz);
        }

        double center_depth = zx*(bcx-cpx) + zy*(bcy-cpy) + zz*(bcz-cpz);
        if (center_depth <= 0.5) continue;

        std::vector<cv::Point2f> img_corners;
        cv::projectPoints(corners, rvec, tvec, K, dist_coeffs, img_corners);

        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}
        };
        for (auto &e : edges) {
            int a = e[0], b = e[1];
            cv::line(canvas,
                     cv::Point((int)img_corners[a].x, (int)img_corners[a].y),
                     cv::Point((int)img_corners[b].x, (int)img_corners[b].y),
                     cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        }
    }
    (void)roi_w; (void)roi_h;
}

static void draw_perspective(cv::Mat &canvas, const float *points, guint count,
                              GstBuffer *inbuf, GstG3DRender *self, int roi_w, int roi_h) {
    const float DEG2RAD = (float)(M_PI / 180.0);
    float az   = self->cam_azimuth   * DEG2RAD;
    float el   = self->cam_elevation * DEG2RAD;
    float dist = self->cam_distance;

    cv::Mat cam_pos = (cv::Mat_<double>(3, 1)
        << dist * std::cos(el) * std::cos(az),
           dist * std::cos(el) * std::sin(az),
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
    R.at<double>(0,0) = xaxis.at<double>(0); R.at<double>(0,1) = xaxis.at<double>(1); R.at<double>(0,2) = xaxis.at<double>(2);
    R.at<double>(1,0) = yaxis.at<double>(0); R.at<double>(1,1) = yaxis.at<double>(1); R.at<double>(1,2) = yaxis.at<double>(2);
    R.at<double>(2,0) = zaxis.at<double>(0); R.at<double>(2,1) = zaxis.at<double>(1); R.at<double>(2,2) = zaxis.at<double>(2);

    cv::Mat rvec;
    cv::Rodrigues(R, rvec);
    cv::Mat tvec = -R * cam_pos;

    double fx = (roi_w / 2.0) / std::tan(self->cam_fov * DEG2RAD / 2.0);
    double fy = fx;
    cv::Mat K = (cv::Mat_<double>(3, 3)
        << fx, 0, roi_w / 2.0,
            0, fy, roi_h / 2.0,
            0,  0, 1);
    cv::Mat dist_coeffs = cv::Mat::zeros(4, 1, CV_64F);

    std::vector<cv::Point3f> obj_pts;
    std::vector<float> z_vals;
    obj_pts.reserve(count / (guint)self->point_stride + 1);
    z_vals.reserve(obj_pts.capacity());

    float clip = self->cam_distance + 20.0f;
    for (guint i = 0; i < count; i += (guint)self->point_stride) {
        float x = points[i*4+0], y = points[i*4+1], z = points[i*4+2];
        if (x < -clip || x > clip || y < -clip || y > clip) continue;
        obj_pts.push_back(cv::Point3f(x, y, z));
        z_vals.push_back(z);
    }

    if (obj_pts.empty()) return;

    std::vector<cv::Point2f> img_pts;
    cv::projectPoints(obj_pts, rvec, tvec, K, dist_coeffs, img_pts);

    struct ProjPt { cv::Point2f px; float depth; float z; };
    std::vector<ProjPt> visible;
    visible.reserve(img_pts.size());

    double zx = zaxis.at<double>(0), zy = zaxis.at<double>(1), zz = zaxis.at<double>(2);
    double cx = cam_pos.at<double>(0), cy = cam_pos.at<double>(1), cz = cam_pos.at<double>(2);

    for (size_t i = 0; i < img_pts.size(); ++i) {
        cv::Point2f &p = img_pts[i];
        if (p.x < 0 || p.x >= roi_w || p.y < 0 || p.y >= roi_h) continue;
        float depth = (float)(zx * (obj_pts[i].x - cx) +
                               zy * (obj_pts[i].y - cy) +
                               zz * (obj_pts[i].z - cz));
        if (depth <= 0.5f) continue;
        visible.push_back({p, depth, z_vals[i]});
    }

    std::sort(visible.begin(), visible.end(),
        [](const ProjPt &a, const ProjPt &b){ return a.depth > b.depth; });

    if (self->point_radius == 1) {
        for (auto &pp : visible)
            canvas.at<cv::Vec3b>((int)pp.px.y, (int)pp.px.x) = height_to_color(pp.z);
    } else {
        for (auto &pp : visible) {
            int r = std::max(1, (int)(self->point_radius * 20.0f / pp.depth));
            cv::Vec3b c = height_to_color(pp.z);
            cv::circle(canvas, cv::Point((int)pp.px.x, (int)pp.px.y), r,
                       cv::Scalar(c[0], c[1], c[2]), -1);
        }
    }

    {
        float step = 3.0f;
        std::vector<cv::Point3f> axis_pts = {
            {0, 0, 0}, {step, 0, 0}, {0, step, 0}, {0, 0, step},
        };
        std::vector<cv::Point2f> axis_img;
        cv::projectPoints(axis_pts, rvec, tvec, K, dist_coeffs, axis_img);

        cv::Point2f o = axis_img[0];
        int ind_cx = roi_w - 70, ind_cy = roi_h - 70, ind_scale = 45;

        struct AxisDef { int idx; cv::Scalar color; const char *label; };
        AxisDef axes[3] = {
            {1, cv::Scalar(90, 90, 200), "X"},
            {2, cv::Scalar(90, 180, 90), "Y"},
            {3, cv::Scalar(180, 90, 90), "Z"},
        };

        if (o.x >= 0 && o.x < roi_w && o.y >= 0 && o.y < roi_h) {
            int origin_scale = 18;
            cv::Point origin_pt((int)o.x, (int)o.y);
            cv::Scalar axis_color(240, 240, 240);
            for (int ai = 0; ai < 2; ++ai) {
                auto &ax = axes[ai];
                cv::Point2f dir = axis_img[ax.idx] - o;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len < 0.5f) continue;
                dir /= len;
                cv::Point pos_tip(origin_pt.x + (int)( dir.x * origin_scale),
                                  origin_pt.y + (int)( dir.y * origin_scale));
                cv::Point neg_tip(origin_pt.x + (int)(-dir.x * origin_scale),
                                  origin_pt.y + (int)(-dir.y * origin_scale));
                cv::line(canvas, neg_tip, pos_tip, axis_color, 1, cv::LINE_AA);
            }
        }

        cv::Scalar ind_color(220, 220, 220);
        for (int ai = 0; ai < 3; ++ai) {
            auto &ax = axes[ai];
            cv::Point2f dir = axis_img[ax.idx] - o;
            if (ai == 1) dir = -dir;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 0.5f) continue;
            dir /= len;
            cv::Point pos_tip(ind_cx + (int)( dir.x * ind_scale),        ind_cy + (int)( dir.y * ind_scale));
            cv::Point neg_tip(ind_cx + (int)(-dir.x * ind_scale / 2.0f), ind_cy + (int)(-dir.y * ind_scale / 2.0f));
            float dx = (float)(pos_tip.x - neg_tip.x), dy = (float)(pos_tip.y - neg_tip.y);
            float d  = std::sqrt(dx * dx + dy * dy);
            float ux = dx / d, uy = dy / d;
            int arrow_len = 10;
            float dash_end = d - arrow_len;
            float pos = 0.0f; bool on = true;
            while (pos < dash_end) {
                float end = std::min(pos + (on ? 6.0f : 4.0f), dash_end);
                if (on) {
                    cv::Point a(neg_tip.x + (int)(ux * pos), neg_tip.y + (int)(uy * pos));
                    cv::Point b(neg_tip.x + (int)(ux * end), neg_tip.y + (int)(uy * end));
                    cv::line(canvas, a, b, ind_color, 1, cv::LINE_AA);
                }
                pos = end; on = !on;
            }
            cv::Point arrow_start(neg_tip.x + (int)(ux * dash_end), neg_tip.y + (int)(uy * dash_end));
            cv::arrowedLine(canvas, arrow_start, pos_tip, ind_color, 1, cv::LINE_AA, 0, 0.4);
            cv::Point2f perp(-dir.y, dir.x);
            cv::Point2f label_pos(pos_tip.x + dir.x * 8 + perp.x * 8,
                                  pos_tip.y + dir.y * 8 + perp.y * 8);
            cv::putText(canvas, ax.label, cv::Point((int)label_pos.x - 4, (int)label_pos.y + 4),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, ind_color, 1, cv::LINE_AA);
        }
    }

    draw_detection_boxes_perspective(canvas, inbuf, rvec, tvec, K, dist_coeffs,
                                     cam_pos, zaxis, roi_w, roi_h);

    self->cam_azimuth += self->cam_azimuth_step;
    if (self->cam_azimuth >  360.0f) self->cam_azimuth -= 360.0f;
    if (self->cam_azimuth < -360.0f) self->cam_azimuth += 360.0f;
}

// ── BEV detection boxes ───────────────────────────────────────────────────────

static void draw_detection_boxes_bev(cv::Mat &canvas, GstBuffer *inbuf,
                                      const GstG3DRender *self, int roi_w, int roi_h) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(inbuf);
    if (!rmeta) return;

    GstAnalyticsMtdType type = gst_analytics_3d_od_mtd_get_mtd_type();
    GstAnalytics3DODMtd mtd;
    gpointer state = NULL;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, type,
                                               reinterpret_cast<GstAnalyticsMtd *>(&mtd))) {
        gfloat cx, cy, cz, length, width, height, yaw, pitch, roll;
        gst_analytics_3d_od_mtd_get_location(&mtd, &cx, &cy, &cz,
                                              &length, &width, &height,
                                              &yaw, &pitch, &roll);

        float cos_t = std::cos(yaw), sin_t = -std::sin(yaw);
        float hx = width / 2.0f, hy = length / 2.0f;
        float wx[4] = { cx+cos_t*hx-sin_t*hy, cx-cos_t*hx-sin_t*hy,
                        cx-cos_t*hx+sin_t*hy, cx+cos_t*hx+sin_t*hy };
        float wy[4] = { cy+sin_t*hx+cos_t*hy, cy-sin_t*hx+cos_t*hy,
                        cy-sin_t*hx-cos_t*hy, cy+sin_t*hx-cos_t*hy };
        std::vector<cv::Point> poly(4);
        for (int k = 0; k < 4; ++k)
            poly[k] = world_to_pixel_bev(wx[k], wy[k], self, roi_w, roi_h);
        cv::polylines(canvas, poly, true, cv::Scalar(0, 255, 0), 1);
        cv::circle(canvas, world_to_pixel_bev(cx, cy, self, roi_w, roi_h), 4,
                   cv::Scalar(0, 255, 0), -1);
    }
}

// ── Camera 2D detection boxes ─────────────────────────────────────────────────

static void draw_2d_boxes(cv::Mat &cell, GstBuffer *cam_buf, int orig_w, int orig_h) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(cam_buf);
    if (!rmeta) return;

    float sx = (float)cell.cols / orig_w;
    float sy = (float)cell.rows / orig_h;

    GstAnalyticsODMtd mtd;
    gpointer state = NULL;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(),
                                               &mtd)) {
        gint bx = 0, by = 0, bw = 0, bh = 0;
        gfloat conf = 0.f;
        if (!gst_analytics_od_mtd_get_location(&mtd, &bx, &by, &bw, &bh, &conf))
            continue;
        int x = (int)(bx * sx), y = (int)(by * sy);
        int w = (int)(bw * sx), h = (int)(bh * sy);
        if (w <= 0 || h <= 0) continue;
        x = std::max(0, x); y = std::max(0, y);
        w = std::min(w, cell.cols - x); h = std::min(h, cell.rows - y);
        cv::rectangle(cell, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 1);
    }
}

// ── Batch helpers ─────────────────────────────────────────────────────────────

static GstBuffer *extract_lidar_from_batch(GstBuffer *batch_buf) {
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

struct CamStream { GstBuffer *buf; gint w; gint h; GstVideoInfo vinfo; };

static std::vector<CamStream> collect_camera_streams(GstBuffer *batch_buf) {
    std::vector<CamStream> cams;
    GstAnalyticsBatchMeta *batch = gst_buffer_get_analytics_batch_meta(batch_buf);
    if (!batch) return cams;
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

// ── Main transform ────────────────────────────────────────────────────────────

static GstFlowReturn gst_g3d_render_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstG3DRender *self = GST_G3D_RENDER(trans);

    // Extract streams from batch (or treat inbuf as direct lidar)
    GstBuffer *lidar_buf = inbuf;
    std::vector<CamStream> cams;
    if (self->input_is_batch) {
        lidar_buf = extract_lidar_from_batch(inbuf);
        cams      = collect_camera_streams(inbuf);
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
    // LiDAR always renders in a height×height square on the right.
    // Camera(s) fill the remaining width on the left.
    // Use width > height to make room for cameras (e.g. width=1600 height=800).
    int n_cams     = (int)cams.size();
    int lidar_size = self->height;
    int lidar_x    = (n_cams > 0) ? std::max(0, self->width - lidar_size) : 0;
    int lidar_w    = std::min(lidar_size, self->width - lidar_x);
    int lidar_h    = lidar_size;
    int cam_area_w = lidar_x;

    // ── Render LiDAR ────────────────────────────────────────────────────────
    if (lidar_buf) {
        LidarMeta *lidar_meta = reinterpret_cast<LidarMeta *>(
            gst_buffer_get_meta(lidar_buf, LIDAR_META_API_TYPE));

        if (lidar_meta) {
            GstMapInfo in_map;
            if (!gst_buffer_map(lidar_buf, &in_map, GST_MAP_READ)) {
                GST_ERROR_OBJECT(self, "Failed to map lidar input buffer");
                gst_buffer_unmap(outbuf, &out_map);
                return GST_FLOW_ERROR;
            }

            const float *points = reinterpret_cast<const float *>(in_map.data);
            const guint  count  = lidar_meta->lidar_point_count;

            cv::Mat lidar_roi = canvas(cv::Rect(lidar_x, 0, lidar_w, lidar_h));

            if (self->view_mode == 0) {
                draw_bev(lidar_roi, points, count, self, lidar_w, lidar_h);
                draw_detection_boxes_bev(lidar_roi, lidar_buf, self, lidar_w, lidar_h);
            } else {
                draw_perspective(lidar_roi, points, count, lidar_buf, self, lidar_w, lidar_h);
            }

            gst_buffer_unmap(lidar_buf, &in_map);
        } else {
            GST_WARNING_OBJECT(self, "No LidarMeta on lidar buffer");
        }
    }

    // ── Render camera streams ────────────────────────────────────────────────
    if (n_cams > 0) {
        int cols   = (n_cams >= 4) ? 2 : 1;
        int rows   = (n_cams + cols - 1) / cols;
        int cell_w = cam_area_w / cols;
        int cell_h = self->height / rows;

        for (int i = 0; i < n_cams; ++i) {
            CamStream &cs = cams[i];
            int col   = i % cols;
            int row   = i / cols;
            int x_off = col * cell_w;
            int y_off = row * cell_h;

            // Use gst_video_frame_map so the VA allocator can map GPU memory to CPU
            GstVideoFrame vframe;
            if (!gst_video_frame_map(&vframe, &cs.vinfo, cs.buf, GST_MAP_READ)) {
                GST_WARNING_OBJECT(self, "Failed to map camera buffer %d", i);
                continue;
            }

            GstVideoFormat fmt = GST_VIDEO_FRAME_FORMAT(&vframe);
            cv::Mat cam_bgr;

            if (fmt == GST_VIDEO_FORMAT_BGR) {
                cv::Mat src(cs.h, cs.w, CV_8UC3,
                            GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0),
                            GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0));
                src.copyTo(cam_bgr);
            } else if (fmt == GST_VIDEO_FORMAT_BGRx || fmt == GST_VIDEO_FORMAT_BGRA) {
                cv::Mat src(cs.h, cs.w, CV_8UC4,
                            GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0),
                            GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0));
                cv::cvtColor(src, cam_bgr, cv::COLOR_BGRA2BGR);
            } else if (fmt == GST_VIDEO_FORMAT_NV12) {
                // OpenCV COLOR_YUV2BGR_NV12 requires even width and height
                int y_stride  = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
                int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 1);
                const guint8 *y_data  = (const guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0);
                const guint8 *uv_data = (const guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&vframe, 1);
                int h2 = (cs.h / 2) * 2;  // round down to even
                int w2 = (cs.w / 2) * 2;
                if (h2 > 0 && w2 > 0) {
                    std::vector<guint8> packed((size_t)w2 * h2 * 3 / 2);
                    for (int r = 0; r < h2; r++)
                        std::memcpy(packed.data() + (size_t)r * w2, y_data + (size_t)r * y_stride, w2);
                    for (int r = 0; r < h2 / 2; r++)
                        std::memcpy(packed.data() + (size_t)w2 * h2 + (size_t)r * w2,
                                    uv_data + (size_t)r * uv_stride, w2);
                    cv::Mat yuv(h2 * 3 / 2, w2, CV_8UC1, packed.data());
                    cv::cvtColor(yuv, cam_bgr, cv::COLOR_YUV2BGR_NV12);
                }
            } else {
                GST_WARNING_OBJECT(self, "Unsupported camera format: %s",
                                   gst_video_format_to_string(fmt));
            }

            gst_video_frame_unmap(&vframe);

            if (cam_bgr.empty()) continue;

            // Letterbox: scale to fit cell while preserving aspect ratio
            float scale = std::min((float)cell_w / cam_bgr.cols,
                                   (float)cell_h / cam_bgr.rows);
            int sw = (int)(cam_bgr.cols * scale);
            int sh = (int)(cam_bgr.rows * scale);
            int xp = (cell_w - sw) / 2;
            int yp = (cell_h - sh) / 2;

            cv::Mat cell(cell_h, cell_w, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::Mat scaled;
            cv::resize(cam_bgr, scaled, cv::Size(sw, sh));
            draw_2d_boxes(scaled, cs.buf, cs.w, cs.h);
            scaled.copyTo(cell(cv::Rect(xp, yp, sw, sh)));
            cell.copyTo(canvas(cv::Rect(x_off, y_off, cell_w, cell_h)));
            cv::putText(canvas, "Cam " + std::to_string(i),
                        cv::Point(x_off + 4, y_off + 16),
                        cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
        }

        cv::line(canvas, cv::Point(lidar_x, 0),
                          cv::Point(lidar_x, self->height),
                          cv::Scalar(80, 80, 80), 1);
    }

    gst_buffer_unmap(outbuf, &out_map);

    // ── PTS ─────────────────────────────────────────────────────────────────
    const GstClockTime frame_duration = GST_SECOND / 10;
    GstClockTime src_pts = GST_CLOCK_TIME_NONE;
    if (lidar_buf && GST_BUFFER_PTS(lidar_buf) > 0)
        src_pts = GST_BUFFER_PTS(lidar_buf);
    else if (GST_BUFFER_PTS(inbuf) > 0)
        src_pts = GST_BUFFER_PTS(inbuf);

    GST_BUFFER_PTS(outbuf)      = GST_CLOCK_TIME_IS_VALID(src_pts) ? src_pts
                                                                    : self->frame_count * frame_duration;
    GST_BUFFER_DURATION(outbuf) = frame_duration;
    self->frame_count++;

    GST_DEBUG_OBJECT(self, "rendered frame %lu pts=%" GST_TIME_FORMAT,
                     (unsigned long)self->frame_count,
                     GST_TIME_ARGS(GST_BUFFER_PTS(outbuf)));
    return GST_FLOW_OK;
}
