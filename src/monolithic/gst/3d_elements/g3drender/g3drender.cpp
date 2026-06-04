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

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_render_debug);
#define GST_CAT_DEFAULT gst_g3d_render_debug

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
    GST_DEBUG_CATEGORY_INIT(gst_g3d_render_debug, "g3drender", 0, "LiDAR BEV Renderer");

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

    gst_element_class_set_static_metadata(element_class,
        "G3D LiDAR BEV Renderer", "Filter/Converter",
        "Renders LiDAR point cloud and PointPillars detections as a Bird's Eye View video frame (g3drender)",
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
    self->width        = 800;
    self->height       = 800;
    self->range_x_min  = -50.0f;
    self->range_x_max  =  50.0f;
    self->range_y_min  = -50.0f;
    self->range_y_max  =  50.0f;
    self->point_radius = 2;
    self->point_stride = 1;
    self->frame_count  = 0;
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
    case PROP_WIDTH:        self->width        = g_value_get_int(val);   break;
    case PROP_HEIGHT:       self->height       = g_value_get_int(val);   break;
    case PROP_RANGE_X_MIN:  self->range_x_min  = g_value_get_float(val); break;
    case PROP_RANGE_X_MAX:  self->range_x_max  = g_value_get_float(val); break;
    case PROP_RANGE_Y_MIN:  self->range_y_min  = g_value_get_float(val); break;
    case PROP_RANGE_Y_MAX:  self->range_y_max  = g_value_get_float(val); break;
    case PROP_POINT_RADIUS: self->point_radius = g_value_get_int(val);   break;
    case PROP_POINT_STRIDE: self->point_stride = g_value_get_int(val);   break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
    }
}

static void gst_g3d_render_get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec) {
    GstG3DRender *self = GST_G3D_RENDER(obj);
    switch (prop_id) {
    case PROP_WIDTH:        g_value_set_int(val,   self->width);        break;
    case PROP_HEIGHT:       g_value_set_int(val,   self->height);       break;
    case PROP_RANGE_X_MIN:  g_value_set_float(val, self->range_x_min);  break;
    case PROP_RANGE_X_MAX:  g_value_set_float(val, self->range_x_max);  break;
    case PROP_RANGE_Y_MIN:  g_value_set_float(val, self->range_y_min);  break;
    case PROP_RANGE_Y_MAX:  g_value_set_float(val, self->range_y_max);  break;
    case PROP_POINT_RADIUS: g_value_set_int(val,   self->point_radius); break;
    case PROP_POINT_STRIDE: g_value_set_int(val,   self->point_stride); break;
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

static cv::Point world_to_pixel(float x, float y, const GstG3DRender *self) {
    int px = (int)((x - self->range_x_min) / (self->range_x_max - self->range_x_min) * self->width);
    int py = (int)((1.0f - (y - self->range_y_min) / (self->range_y_max - self->range_y_min)) * self->height);
    return cv::Point(px, py);
}

static GstFlowReturn gst_g3d_render_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstG3DRender *self = GST_G3D_RENDER(trans);

    GstMapInfo out_map;
    if (!gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT(self, "Failed to map output buffer for writing");
        return GST_FLOW_ERROR;
    }

    cv::Mat canvas(self->height, self->width, CV_8UC3, out_map.data);
    canvas.setTo(cv::Scalar(20, 20, 20));

    const cv::Scalar grid_color(55, 55, 55);
    for (float d = 10.0f; d < self->range_x_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel(-d, self->range_y_min, self),
                         world_to_pixel(-d, self->range_y_max, self), grid_color, 1);
        cv::line(canvas, world_to_pixel( d, self->range_y_min, self),
                         world_to_pixel( d, self->range_y_max, self), grid_color, 1);
    }
    for (float d = 10.0f; d < self->range_y_max; d += 10.0f) {
        cv::line(canvas, world_to_pixel(self->range_x_min, -d, self),
                         world_to_pixel(self->range_x_max, -d, self), grid_color, 1);
        cv::line(canvas, world_to_pixel(self->range_x_min,  d, self),
                         world_to_pixel(self->range_x_max,  d, self), grid_color, 1);
    }

    cv::drawMarker(canvas, world_to_pixel(0.0f, 0.0f, self),
                   cv::Scalar(0, 255, 255), cv::MARKER_CROSS, 16, 2);

    LidarMeta *lidar_meta = reinterpret_cast<LidarMeta *>(
        gst_buffer_get_meta(inbuf, LIDAR_META_API_TYPE));

    if (!lidar_meta) {
        GST_WARNING_OBJECT(self, "No LidarMeta on buffer, outputting blank frame");
        gst_buffer_unmap(outbuf, &out_map);
        goto set_pts;
    }

    {
        GstMapInfo in_map;
        if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ)) {
            GST_ERROR_OBJECT(self, "Failed to map input buffer for reading");
            gst_buffer_unmap(outbuf, &out_map);
            return GST_FLOW_ERROR;
        }

        const float *points = reinterpret_cast<const float *>(in_map.data);
        const guint  count  = lidar_meta->lidar_point_count;

        for (guint i = 0; i < count; i += (guint)self->point_stride) {
            float x         = points[i * 4 + 0];
            float y         = points[i * 4 + 1];
            float intensity = points[i * 4 + 3];
            if (x < self->range_x_min || x > self->range_x_max ||
                y < self->range_y_min || y > self->range_y_max)
                continue;
            uint8_t r = (uint8_t)(intensity * 255.0f);
            uint8_t b = (uint8_t)((1.0f - intensity) * 255.0f);
            cv::circle(canvas, world_to_pixel(x, y, self), self->point_radius, cv::Scalar(b, 80, r), -1);
        }

        gst_buffer_unmap(inbuf, &in_map);

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
                size_t off  = d * 9;
                float cx    = data[off + 0];
                float cy    = data[off + 1];
                float dx    = data[off + 3];
                float dy    = data[off + 4];
                float theta = data[off + 6];
                float cos_t = std::cos(theta), sin_t = std::sin(theta);
                float hx = dx / 2.0f, hy = dy / 2.0f;
                float wx[4] = { cx+cos_t*hx-sin_t*hy, cx-cos_t*hx-sin_t*hy,
                                cx-cos_t*hx+sin_t*hy, cx+cos_t*hx+sin_t*hy };
                float wy[4] = { cy+sin_t*hx+cos_t*hy, cy-sin_t*hx+cos_t*hy,
                                cy-sin_t*hx-cos_t*hy, cy+sin_t*hx-cos_t*hy };
                std::vector<cv::Point> poly(4);
                for (int k = 0; k < 4; ++k)
                    poly[k] = world_to_pixel(wx[k], wy[k], self);
                cv::polylines(canvas, poly, true, cv::Scalar(0, 255, 0), 2);
                cv::circle(canvas, world_to_pixel(cx, cy, self), 4, cv::Scalar(0, 255, 0), -1);
            }
        }
    }

    gst_buffer_unmap(outbuf, &out_map);

set_pts:
    {
        const GstClockTime frame_duration = GST_SECOND / 10;
        GST_BUFFER_PTS(outbuf) = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(inbuf))
                                 ? GST_BUFFER_PTS(inbuf)
                                 : self->frame_count * frame_duration;
        GST_BUFFER_DURATION(outbuf) = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(inbuf))
                                      ? GST_BUFFER_DURATION(inbuf)
                                      : frame_duration;
        self->frame_count++;
    }

    GST_DEBUG_OBJECT(self, "rendered frame %lu pts=%" GST_TIME_FORMAT,
                     (unsigned long)self->frame_count,
                     GST_TIME_ARGS(GST_BUFFER_PTS(outbuf)));
    return GST_FLOW_OK;
}
