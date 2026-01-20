#include "g3dlidarvalidate.h"
#include <dlstreamer/gst/metadata/g3dlidarmeta.h>

#include <gst/gst.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_lidar_validate_debug);
#define GST_CAT_DEFAULT gst_g3d_lidar_validate_debug

enum {
    PROP_0,
    PROP_EXPECTED_POINTS,
    PROP_PREVIEW_COUNT,
    PROP_FAIL_ON_MISMATCH,
    PROP_SILENT
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("application/x-lidar")
);

static void gst_g3d_lidar_validate_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_lidar_validate_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstFlowReturn gst_g3d_lidar_validate_render(GstBaseSink *sink, GstBuffer *buffer);
static gboolean gst_g3d_lidar_validate_start(GstBaseSink *sink);
static gboolean gst_g3d_lidar_validate_stop(GstBaseSink *sink);
static gboolean plugin_init(GstPlugin *plugin);

G_DEFINE_TYPE(GstG3DLidarValidate, gst_g3d_lidar_validate, GST_TYPE_BASE_SINK);

static void gst_g3d_lidar_validate_class_init(GstG3DLidarValidateClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_g3d_lidar_validate_debug, "g3dlidarvalidate", 0, "G3D Lidar Meta Validator");

    gobject_class->set_property = gst_g3d_lidar_validate_set_property;
    gobject_class->get_property = gst_g3d_lidar_validate_get_property;

    g_object_class_install_property(gobject_class, PROP_EXPECTED_POINTS,
        g_param_spec_uint("expected-point-count", "Expected Point Count",
                          "If greater than zero, fail when lidar_point_count differs from this value.",
                          0, G_MAXUINT, 0,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_PREVIEW_COUNT,
        g_param_spec_uint("preview-count", "Preview Count",
                          "Number of float values to log from the lidar_data preview.",
                          0, G_MAXUINT, 8,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_FAIL_ON_MISMATCH,
        g_param_spec_boolean("fail-on-mismatch", "Fail On Mismatch",
                             "Return FLOW_ERROR when metadata is missing or inconsistent.",
                             TRUE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SILENT,
        g_param_spec_boolean("silent", "Silent",
                             "Reduce logging. Only errors and mismatches are reported.",
                             FALSE,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class,
        "G3D Lidar Meta Validator",
        "Sink/Debug",
        "Validates presence and consistency of LidarMeta coming from g3dlidarparse",
        "Open Edge Platform");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);

    basesink_class->render = GST_DEBUG_FUNCPTR(gst_g3d_lidar_validate_render);
    basesink_class->start = GST_DEBUG_FUNCPTR(gst_g3d_lidar_validate_start);
    basesink_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_lidar_validate_stop);
}

static void gst_g3d_lidar_validate_init(GstG3DLidarValidate *self) {
    self->expected_point_count = 0;
    self->preview_count = 8;
    self->fail_on_mismatch = TRUE;
    self->silent = FALSE;
    self->frames_seen = 0;
    self->frames_with_meta = 0;
}

static gboolean gst_g3d_lidar_validate_start(GstBaseSink *sink) {
    GstG3DLidarValidate *self = GST_G3D_LIDAR_VALIDATE(sink);
    self->frames_seen = 0;
    self->frames_with_meta = 0;
    GST_INFO_OBJECT(self, "[START] g3dlidarvalidate ready");
    return TRUE;
}

static gboolean gst_g3d_lidar_validate_stop(GstBaseSink *sink) {
    GstG3DLidarValidate *self = GST_G3D_LIDAR_VALIDATE(sink);
    GST_INFO_OBJECT(self,
                    "[STOP] frames_seen=%" G_GUINT64_FORMAT " frames_with_meta=%" G_GUINT64_FORMAT,
                    self->frames_seen, self->frames_with_meta);
    return TRUE;
}

static void gst_g3d_lidar_validate_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DLidarValidate *self = GST_G3D_LIDAR_VALIDATE(object);

    switch (prop_id) {
        case PROP_EXPECTED_POINTS:
            self->expected_point_count = g_value_get_uint(value);
            break;
        case PROP_PREVIEW_COUNT:
            self->preview_count = g_value_get_uint(value);
            break;
        case PROP_FAIL_ON_MISMATCH:
            self->fail_on_mismatch = g_value_get_boolean(value);
            break;
        case PROP_SILENT:
            self->silent = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_g3d_lidar_validate_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DLidarValidate *self = GST_G3D_LIDAR_VALIDATE(object);

    switch (prop_id) {
        case PROP_EXPECTED_POINTS:
            g_value_set_uint(value, self->expected_point_count);
            break;
        case PROP_PREVIEW_COUNT:
            g_value_set_uint(value, self->preview_count);
            break;
        case PROP_FAIL_ON_MISMATCH:
            g_value_set_boolean(value, self->fail_on_mismatch);
            break;
        case PROP_SILENT:
            g_value_set_boolean(value, self->silent);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static GstFlowReturn gst_g3d_lidar_validate_render(GstBaseSink *sink, GstBuffer *buffer) {
    GstG3DLidarValidate *self = GST_G3D_LIDAR_VALIDATE(sink);
    self->frames_seen++;

    LidarMeta *meta = (LidarMeta *)gst_buffer_get_meta(buffer, LIDAR_META_API_TYPE);
    if (!meta) {
        GST_WARNING_OBJECT(self, "Missing LidarMeta on buffer #%" G_GUINT64_FORMAT, self->frames_seen - 1);
        return self->fail_on_mismatch ? GST_FLOW_ERROR : GST_FLOW_OK;
    }

    self->frames_with_meta++;
    gboolean mismatch = FALSE;

    if (meta->lidar_point_count * 4 != meta->lidar_data.size()) {
        GST_WARNING_OBJECT(self,
                           "lidar_point_count (%u) does not match lidar_data size (%zu floats)",
                           meta->lidar_point_count, meta->lidar_data.size());
        mismatch = TRUE;
    }

    if (self->expected_point_count > 0 && meta->lidar_point_count != self->expected_point_count) {
        GST_WARNING_OBJECT(self,
                           "lidar_point_count (%u) != expected (%u)",
                           meta->lidar_point_count, self->expected_point_count);
        mismatch = TRUE;
    }

    if (!self->silent) {
        const guint preview_len = std::min(self->preview_count, static_cast<guint>(meta->lidar_data.size()));
        std::ostringstream oss;
        oss << "frame_id=" << meta->frame_id
            << " stream_id=" << meta->stream_id
            << " lidar_point_count=" << meta->lidar_point_count
            << " data_floats=" << meta->lidar_data.size();

        if (meta->exit_lidarparse_timestamp != GST_CLOCK_TIME_NONE) {
            oss << " exit_ts=" << meta->exit_lidarparse_timestamp << "ns";
        } else {
            oss << " exit_ts=<none>";
        }

        if (preview_len > 0 && !meta->lidar_data.empty()) {
            oss << " preview(" << preview_len << "/" << meta->lidar_data.size() << "):";
            for (guint i = 0; i < preview_len; ++i) {
                oss << " " << std::fixed << std::setprecision(6) << meta->lidar_data[i];
            }
        }

        GST_INFO_OBJECT(self, "%s", oss.str().c_str());
    }

    if (mismatch && self->fail_on_mismatch) {
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "g3dlidarvalidate", GST_RANK_NONE, GST_TYPE_G3D_LIDAR_VALIDATE);
}

#ifndef PACKAGE
#define PACKAGE "g3dlidarvalidate"
#endif

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    g3dlidarvalidate,
    "G3D Lidar Meta Validator",
    plugin_init,
    "1.0",
    "LGPL",
    "dlstreamer",
    "https://github.com/dlstreamer/dlstreamer"
)
