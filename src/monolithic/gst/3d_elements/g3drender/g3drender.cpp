/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3drender.h"

#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/gva_tensor_meta.h>

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
        GST_STATIC_CAPS("application/x-lidar"));

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
        result = gst_caps_from_string("application/x-lidar");
    }
    if (filter) {
        GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = tmp;
    }
    return result;
}

static gboolean gst_g3d_render_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    (void)trans; (void)incaps; (void)outcaps;
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

static cv::Point world_to_pixel_bev(float x, float y, const GstG3DRender *self) {
    float x_range = (self->range_x_max - self->range_x_min) / self->zoom;
    float y_range = (self->range_y_max - self->range_y_min) / self->zoom;
    float x_mid   = (self->range_x_max + self->range_x_min) / 2.0f;
    float y_mid   = (self->range_y_max + self->range_y_min) / 2.0f;
    int px = (int)((y - (y_mid - y_range / 2.0f)) / y_range * self->width);
    int py = (int)((1.0f - (x - (x_mid - x_range / 2.0f)) / x_range) * self->height);
    return cv::Point(px, py);
}

static void draw_bev(cv::Mat &canvas, const float *points, guint count, const GstG3DRender *self) {
    const cv::Scalar grid_color(55, 55, 55);
    for (float d = 10.0f; d < self->range_x_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(-d, self->range_y_min, self),
                         world_to_pixel_bev(-d, self->range_y_max, self), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev( d, self->range_y_min, self),
                         world_to_pixel_bev( d, self->range_y_max, self), grid_color, 1);
    }
    for (float d = 10.0f; d < self->range_y_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel_bev(self->range_x_min, -d, self),
                         world_to_pixel_bev(self->range_x_max, -d, self), grid_color, 1);
        cv::line(canvas, world_to_pixel_bev(self->range_x_min,  d, self),
                         world_to_pixel_bev(self->range_x_max,  d, self), grid_color, 1);
    }
    cv::Point origin = world_to_pixel_bev(0.0f, 0.0f, self);
    int origin_scale = 18;
    cv::Scalar axis_color(240, 240, 240);
    cv::Point x_tip = world_to_pixel_bev(0.0f + (self->range_x_max - self->range_x_min) * origin_scale / self->width,
                                         0.0f, self);
    cv::Point y_tip = world_to_pixel_bev(0.0f,
                                         0.0f + (self->range_y_max - self->range_y_min) * origin_scale / self->height,
                                         self);
    x_tip = cv::Point(origin.x, origin.y - origin_scale);
    y_tip = cv::Point(origin.x - origin_scale, origin.y);
    cv::Point x_neg(origin.x, origin.y + origin_scale);
    cv::Point y_neg(origin.x + origin_scale, origin.y);
    cv::line(canvas, x_neg, x_tip, axis_color, 1, cv::LINE_AA);
    cv::line(canvas, y_neg, y_tip, axis_color, 1, cv::LINE_AA);

    int ind_cx = self->width - 70, ind_cy = self->height - 70, ind_scale = 45;
    cv::Scalar ind_color(220, 220, 220);

    auto draw_bev_axis = [&](cv::Point2f dir, const char *label) {
        cv::Point pos_tip(ind_cx + (int)( dir.x * ind_scale),       ind_cy + (int)( dir.y * ind_scale));
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

    for (guint i = 0; i < count; i += (guint)self->point_stride) {
        float x = points[i*4+0], y = points[i*4+1], intensity = points[i*4+3];
        if (x < self->range_x_min || x > self->range_x_max ||
            y < self->range_y_min || y > self->range_y_max) continue;
        uint8_t r = (uint8_t)(intensity * 255.0f);
        uint8_t b = (uint8_t)((1.0f - intensity) * 255.0f);
        cv::circle(canvas, world_to_pixel_bev(x, y, self), self->point_radius, cv::Scalar(b, 80, r), -1);
    }
}


static cv::Scalar height_to_color(float z) {
    float t = (z + 2.0f) / 6.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        float s = t * 2.0f;
        return cv::Scalar((1.0f - s) * 200, s * 180, s * 80);
    }
    float s = (t - 0.5f) * 2.0f;
    return cv::Scalar(0, (1.0f - s) * 180, s * 220 + 80);
}

static void draw_perspective(cv::Mat &canvas, const float *points, guint count, GstG3DRender *self) {
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

    double fx = (self->width  / 2.0) / std::tan(self->cam_fov * DEG2RAD / 2.0);
    double fy = fx;
    cv::Mat K = (cv::Mat_<double>(3, 3)
        << fx, 0, self->width  / 2.0,
            0, fy, self->height / 2.0,
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
        if (p.x < 0 || p.x >= self->width || p.y < 0 || p.y >= self->height) continue;
        float depth = (float)(zx * (obj_pts[i].x - cx) +
                               zy * (obj_pts[i].y - cy) +
                               zz * (obj_pts[i].z - cz));
        if (depth <= 0.5f) continue;
        visible.push_back({p, depth, z_vals[i]});
    }

    std::sort(visible.begin(), visible.end(),
        [](const ProjPt &a, const ProjPt &b){ return a.depth > b.depth; });

    for (auto &pp : visible) {
        int r = std::max(1, (int)(self->point_radius * 20.0f / pp.depth));
        cv::circle(canvas, cv::Point((int)pp.px.x, (int)pp.px.y), r, height_to_color(pp.z), -1);
    }

    {
        float step = 3.0f;
        std::vector<cv::Point3f> axis_pts = {
            {0, 0, 0},
            {step, 0, 0}, {0, step, 0}, {0, 0, step},
        };
        std::vector<cv::Point2f> axis_img;
        cv::projectPoints(axis_pts, rvec, tvec, K, dist_coeffs, axis_img);

        cv::Point2f o = axis_img[0];
        int W = self->width, H = self->height;

        int ind_cx = W - 70, ind_cy = H - 70, ind_scale = 45;

        struct AxisDef { int idx; cv::Scalar color; const char *label; };
        AxisDef axes[3] = {
            {1, cv::Scalar(90, 90, 200), "X"},
            {2, cv::Scalar(90, 180, 90), "Y"},
            {3, cv::Scalar(180, 90, 90), "Z"},
        };

        if (o.x >= 0 && o.x < W && o.y >= 0 && o.y < H) {
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
            cv::Point pos_tip(ind_cx + (int)( dir.x * ind_scale),       ind_cy + (int)( dir.y * ind_scale));
            cv::Point neg_tip(ind_cx + (int)(-dir.x * ind_scale / 2.0f), ind_cy + (int)(-dir.y * ind_scale / 2.0f));
            float dx = (float)(pos_tip.x - neg_tip.x);
            float dy = (float)(pos_tip.y - neg_tip.y);
            float d  = std::sqrt(dx * dx + dy * dy);
            float ux = dx / d, uy = dy / d;
            int arrow_len = 10;
            float dash_end = d - arrow_len;
            float pos = 0.0f;
            bool on = true;
            while (pos < dash_end) {
                float end = std::min(pos + (on ? 6.0f : 4.0f), dash_end);
                if (on) {
                    cv::Point a(neg_tip.x + (int)(ux * pos), neg_tip.y + (int)(uy * pos));
                    cv::Point b(neg_tip.x + (int)(ux * end), neg_tip.y + (int)(uy * end));
                    cv::line(canvas, a, b, ind_color, 1, cv::LINE_AA);
                }
                pos = end;
                on = !on;
            }
            cv::Point arrow_start(neg_tip.x + (int)(ux * dash_end), neg_tip.y + (int)(uy * dash_end));
            cv::arrowedLine(canvas, arrow_start, pos_tip, ind_color, 1, cv::LINE_AA, 0, 0.4);
            cv::Point2f label_pos(pos_tip.x + dir.x * 10, pos_tip.y + dir.y * 10);
            cv::putText(canvas, ax.label, cv::Point((int)label_pos.x - 5, (int)label_pos.y + 5),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, ind_color, 1, cv::LINE_AA);
        }

    }

    self->cam_azimuth += self->cam_azimuth_step;
    if (self->cam_azimuth >  360.0f) self->cam_azimuth -= 360.0f;
    if (self->cam_azimuth < -360.0f) self->cam_azimuth += 360.0f;
}

static void draw_detection_boxes_bev(cv::Mat &canvas, GstBuffer *inbuf, const GstG3DRender *self) {
    gpointer state = NULL;
    GstGVATensorMeta *tensor_meta = NULL;
    while ((tensor_meta = GST_GVA_TENSOR_META_ITERATE(inbuf, &state))) {
        GstStructure *s = tensor_meta->data;
        const gchar *fmt   = gst_structure_get_string(s, "format");
        const gchar *lname = gst_structure_get_string(s, "layer_name");
        if (!fmt || !lname) continue;
        if (g_strcmp0(fmt, "pointpillars_3d") != 0 ||
            g_strcmp0(lname, "pointpillars_3d_detection") != 0) continue;

        GValueArray *dims_array = NULL;
        gst_structure_get_array(s, "dims", &dims_array);
        if (!dims_array || dims_array->n_values != 2) {
            if (dims_array) g_value_array_free(dims_array);
            continue;
        }
        guint dim0 = g_value_get_uint(g_value_array_get_nth(dims_array, 0));
        guint dim1 = g_value_get_uint(g_value_array_get_nth(dims_array, 1));
        g_value_array_free(dims_array);
        if (dim1 != 9 || dim0 == 0) continue;

        const GValue *buf_val = gst_structure_get_value(s, "data_buffer");
        if (!buf_val || !G_VALUE_HOLDS(buf_val, G_TYPE_VARIANT)) continue;
        GVariant *variant = g_value_get_variant(buf_val);
        gsize nbytes = 0;
        const float *data = reinterpret_cast<const float *>(
            g_variant_get_fixed_array(variant, &nbytes, 1));
        if (!data || nbytes < dim0 * dim1 * sizeof(float)) continue;

        for (guint d = 0; d < dim0; ++d) {
            size_t off = d * 9;
            float cx = data[off+0], cy = data[off+1];
            float dx = data[off+3], dy = data[off+4], theta = data[off+6];
            float cos_t = std::cos(theta), sin_t = std::sin(theta);
            float hx = dx / 2.0f, hy = dy / 2.0f;
            float wx[4] = { cx+cos_t*hx-sin_t*hy, cx-cos_t*hx-sin_t*hy,
                            cx-cos_t*hx+sin_t*hy, cx+cos_t*hx+sin_t*hy };
            float wy[4] = { cy+sin_t*hx+cos_t*hy, cy-sin_t*hx+cos_t*hy,
                            cy-sin_t*hx-cos_t*hy, cy+sin_t*hx-cos_t*hy };
            std::vector<cv::Point> poly(4);
            for (int k = 0; k < 4; ++k)
                poly[k] = world_to_pixel_bev(wx[k], wy[k], self);
            cv::polylines(canvas, poly, true, cv::Scalar(0, 255, 0), 2);
            cv::circle(canvas, world_to_pixel_bev(cx, cy, self), 4, cv::Scalar(0, 255, 0), -1);
        }
    }
}

static GstFlowReturn gst_g3d_render_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstG3DRender *self = GST_G3D_RENDER(trans);

    GstMapInfo out_map;
    if (!gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT(self, "Failed to map output buffer for writing");
        return GST_FLOW_ERROR;
    }

    cv::Mat canvas(self->height, self->width, CV_8UC3, out_map.data);
    canvas.setTo(cv::Scalar(15, 15, 15));

    LidarMeta *lidar_meta = reinterpret_cast<LidarMeta *>(
        gst_buffer_get_meta(inbuf, LIDAR_META_API_TYPE));

    if (lidar_meta) {
        GstMapInfo in_map;
        if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ)) {
            GST_ERROR_OBJECT(self, "Failed to map input buffer for reading");
            gst_buffer_unmap(outbuf, &out_map);
            return GST_FLOW_ERROR;
        }

        const float *points = reinterpret_cast<const float *>(in_map.data);
        const guint  count  = lidar_meta->lidar_point_count;

        if (self->view_mode == 0) {
            draw_bev(canvas, points, count, self);
            draw_detection_boxes_bev(canvas, inbuf, self);
        } else {
            draw_perspective(canvas, points, count, self);
        }

        gst_buffer_unmap(inbuf, &in_map);
    } else {
        GST_WARNING_OBJECT(self, "No LidarMeta on buffer, outputting blank frame");
    }

    gst_buffer_unmap(outbuf, &out_map);

    const GstClockTime frame_duration = GST_SECOND / 10;
    GST_BUFFER_PTS(outbuf) = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(inbuf))
                             ? GST_BUFFER_PTS(inbuf)
                             : self->frame_count * frame_duration;
    GST_BUFFER_DURATION(outbuf) = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(inbuf))
                                  ? GST_BUFFER_DURATION(inbuf)
                                  : frame_duration;
    self->frame_count++;

    GST_DEBUG_OBJECT(self, "rendered frame %lu pts=%" GST_TIME_FORMAT,
                     (unsigned long)self->frame_count,
                     GST_TIME_ARGS(GST_BUFFER_PTS(outbuf)));
    return GST_FLOW_OK;
}
