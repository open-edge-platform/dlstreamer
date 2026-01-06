#include "gstlidarmeta.h" 
#include "gstlidarparse.h"
#include <string.h>
#include <vector>
#include <fstream>
#include <unistd.h> 
#include <iomanip>
#include <sstream>
#include <gst/gstinfo.h> 

GST_DEBUG_CATEGORY_STATIC(gst_lidar_parse_debug);
#define GST_CAT_DEFAULT gst_lidar_parse_debug

enum {
    PROP_0,
    PROP_STRIDE,
    PROP_FRAME_RATE,
    PROP_FILE_TYPE // New property for file type
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(LIDAR_META_CAPS) 
);

static void gst_lidar_parse_set_property(GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec);
static void gst_lidar_parse_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec);
static void gst_lidar_parse_finalize(GObject *object);

static gboolean gst_lidar_parse_start(GstBaseTransform *trans);
static gboolean gst_lidar_parse_stop(GstBaseTransform *trans);
static GstFlowReturn gst_lidar_parse_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_lidar_parse_sink_event(GstBaseTransform *trans, GstEvent *event);

static void gst_lidar_parse_class_init(GstLidarParseClass *klass);
static void gst_lidar_parse_init(GstLidarParse *filter);

G_DEFINE_TYPE(GstLidarParse, gst_lidar_parse, GST_TYPE_BASE_TRANSFORM);

GType file_type_get_type(void) {
    static GType file_type = 0;
    if (!file_type) {
        static const GEnumValue values[] = {
            {FILE_TYPE_BIN, "BIN", "bin"},
            {FILE_TYPE_PCD, "PCD", "pcd"},
            {0, NULL, NULL}
        };
        file_type = g_enum_register_static("FileType", values);
    }
    return file_type;
}

static void gst_lidar_parse_class_init(GstLidarParseClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_lidar_parse_set_property;
    gobject_class->get_property = gst_lidar_parse_get_property;
    gobject_class->finalize = gst_lidar_parse_finalize;


    g_object_class_install_property(gobject_class, PROP_STRIDE,
        g_param_spec_int("stride", "Stride",
                        "Specifies the interval of frames to process, controls processing granularity. 1 means every frame is processed, 2 means every second frame is processed.",
                        1, G_MAXINT, 1,
                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_FRAME_RATE,
        g_param_spec_float("frame-rate", "Frame Rate",
                          "Desired output frame rate in frames per second. A value of 0 means no frame rate control.",
                          0.0, G_MAXFLOAT, 0.0,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_FILE_TYPE,
        g_param_spec_enum("file-type", "File Type",
                      "Specifies the type of input file: BIN for binary files, PCD for point cloud data files.",
                      file_type_get_type(), FILE_TYPE_BIN,
                      (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class,
        "Lidar Binary Parser",
        "Filter/Converter",
        "Parses binary lidar data to vector float format with stride and frame rate control",
        "Your Name <your.email@example.com>");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_lidar_parse_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_lidar_parse_stop);
    base_transform_class->transform = GST_DEBUG_FUNCPTR(gst_lidar_parse_transform);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_lidar_parse_sink_event);
    base_transform_class->passthrough_on_same_caps = FALSE;
}

static void gst_lidar_parse_init(GstLidarParse *filter) {
    filter->stride = 1;
    filter->frame_rate = 0.0;
    g_mutex_init(&filter->mutex);

    filter->current_index = 0;
    filter->is_single_file = FALSE;
    filter->file_type = FILE_TYPE_BIN; // Default to BIN
}

static void gst_lidar_parse_finalize(GObject *object) {
    GstLidarParse *filter = GST_LIDAR_PARSE(object);

    g_mutex_clear(&filter->mutex);

    filter->current_index = 0;
    filter->is_single_file = FALSE;

    G_OBJECT_CLASS(gst_lidar_parse_parent_class)->finalize(object);
}

static void gst_lidar_parse_set_property(GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec) {
    GstLidarParse *filter = GST_LIDAR_PARSE(object);

    switch (prop_id) {
        case PROP_STRIDE:
            filter->stride = g_value_get_int(value);
            break;
        case PROP_FRAME_RATE:
            filter->frame_rate = g_value_get_float(value);
            break;
        case PROP_FILE_TYPE:
            filter->file_type = (FileType)g_value_get_enum(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_lidar_parse_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec) {
    GstLidarParse *filter = GST_LIDAR_PARSE(object);

    switch (prop_id) {
        case PROP_STRIDE:
            g_value_set_int(value, filter->stride);
            break;
        case PROP_FRAME_RATE:  
            g_value_set_float(value, filter->frame_rate);
            break;
        case PROP_FILE_TYPE:
            g_value_set_enum(value, filter->file_type);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static gboolean gst_lidar_parse_start(GstBaseTransform *trans) {
    GstLidarParse *filter = GST_LIDAR_PARSE(trans);

    GST_DEBUG_OBJECT(filter, "Starting lidar parser");
    GST_INFO_OBJECT(filter, "[START] lidarparse");

    GstPad *sink_pad = GST_BASE_TRANSFORM_SINK_PAD(trans);
    GstPad *peer_pad = gst_pad_get_peer(sink_pad);

    if (!peer_pad) {
        GST_ERROR_OBJECT(filter, "No upstream element connected");
        GST_INFO_OBJECT(filter, "[START] Failed: No upstream element");
        return FALSE;
    }

    GstElement *upstream_element = gst_pad_get_parent_element(peer_pad);
    if (!upstream_element) {
        GST_ERROR_OBJECT(filter, "Failed to get upstream element");
        gst_object_unref(peer_pad);
        GST_INFO_OBJECT(filter, "[START] Failed: Cannot get upstream element");
        return FALSE;
    }

    // Get location from upstream
    gchar *upstream_location = NULL;
    g_object_get(upstream_element, "location", &upstream_location, NULL);

    if (!upstream_location) {
        GST_ERROR_OBJECT(filter, "Upstream element does not have a 'location' property");
        gst_object_unref(upstream_element);
        gst_object_unref(peer_pad);
        GST_INFO_OBJECT(filter, "[START] Failed: No location property in upstream");
        return FALSE;
    }

    GST_INFO_OBJECT(filter, "Inherited location from upstream: %s", upstream_location);

    if (g_file_test(upstream_location, G_FILE_TEST_IS_REGULAR)) {
        filter->is_single_file = TRUE; 
        GST_INFO_OBJECT(filter, "Location is a single file. is_single_file set to TRUE.");
    } 

    gst_object_unref(upstream_element);
    gst_object_unref(peer_pad);

    return TRUE;
}

static gboolean gst_lidar_parse_stop(GstBaseTransform *trans) {
    GstLidarParse *filter = GST_LIDAR_PARSE(trans);

    GST_INFO_OBJECT(filter, "[STOP] Stopping lidar parser");
    filter->current_index = 0;
    GST_INFO_OBJECT(filter, "[STOP] Data cleared");

    return TRUE;
}


static gboolean gst_lidar_parse_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstLidarParse *filter = GST_LIDAR_PARSE(trans);
    
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            GST_INFO_OBJECT(filter, "Received EOS event, resetting counters and stopping processing");
            filter->current_index = 0;
            break;
        case GST_EVENT_SEGMENT:
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_FLUSH_STOP:
            filter->current_index = 0;
            GST_INFO_OBJECT(filter, "Reset counters for event: %s", GST_EVENT_TYPE_NAME(event));
            break;
        default:
            break;
    }
    
    return GST_BASE_TRANSFORM_CLASS(gst_lidar_parse_parent_class)->sink_event(trans, event);
}

static GstFlowReturn gst_lidar_parse_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstLidarParse *filter = GST_LIDAR_PARSE(trans);
    g_mutex_lock(&filter->mutex);

    // Stride control
    if (filter->current_index % filter->stride != 0) {
        GST_DEBUG_OBJECT(filter, "Skipping file #%lu (stride=%d, remainder=%lu)", 
                        filter->current_index, filter->stride, filter->current_index % filter->stride);
        filter->current_index++;
        g_mutex_unlock(&filter->mutex);
        return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }

    if (filter->is_single_file == TRUE && filter->current_index >= 1) {
        GST_INFO_OBJECT(filter, "All files processed. Sending EOS.");
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_EOS;
    }

    // Frame rate control variables
    static GstClockTime last_frame_time = GST_CLOCK_TIME_NONE;
    GstClockTime current_time = gst_clock_get_time(gst_system_clock_obtain());
    GstClockTime frame_interval = GST_CLOCK_TIME_NONE;

    if (filter->frame_rate > 0) {
        frame_interval = (GstClockTime)(GST_SECOND / filter->frame_rate);
    }

    // Debug information for rate control
    GST_DEBUG_OBJECT(filter, "Current time: %" GST_TIME_FORMAT, GST_TIME_ARGS(current_time));
    GST_DEBUG_OBJECT(filter, "Last frame time: %" GST_TIME_FORMAT, GST_TIME_ARGS(last_frame_time));
    GST_DEBUG_OBJECT(filter, "Frame interval: %" GST_TIME_FORMAT, GST_TIME_ARGS(frame_interval));

    // If this is not the first frame, ensure the frame interval is respected
    if (last_frame_time != GST_CLOCK_TIME_NONE && frame_interval != GST_CLOCK_TIME_NONE) {
        GstClockTime elapsed_time = current_time - last_frame_time;
        GST_DEBUG_OBJECT(filter, "Elapsed time since last frame: %" GST_TIME_FORMAT, GST_TIME_ARGS(elapsed_time));
        if (elapsed_time < frame_interval) {
            GstClockTime sleep_time = frame_interval - elapsed_time;
            GST_DEBUG_OBJECT(filter, "Sleeping for %" GST_TIME_FORMAT, GST_TIME_ARGS(sleep_time));
            g_usleep(sleep_time / 1000);
        }
    }

    last_frame_time = gst_clock_get_time(gst_system_clock_obtain());

    GST_INFO_OBJECT(filter, "Processing file #%lu (stride=%d)", filter->current_index, filter->stride);
    filter->current_index++;

    size_t num_floats = 0;
    size_t point_count = 0;
    std::vector<float> float_data;

    if (filter->file_type == FILE_TYPE_BIN) {
        // Process BIN file
        GstMapInfo in_map;
        if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ)) {
            GST_ERROR_OBJECT(filter, "Failed to map input buffer for reading");
            g_mutex_unlock(&filter->mutex);
            return GST_FLOW_ERROR;
        }

        if (in_map.size % sizeof(float) != 0) {
            GST_ERROR_OBJECT(filter, "Buffer size (%lu) is not a multiple of float size (%lu)",
                             in_map.size, sizeof(float));
            gst_buffer_unmap(inbuf, &in_map);
            g_mutex_unlock(&filter->mutex);
            return GST_FLOW_ERROR;
        }

        num_floats = in_map.size / sizeof(float);
        point_count = num_floats / 4;
        const float *data = reinterpret_cast<const float *>(in_map.data);
        float_data.assign(data, data + num_floats);
        gst_buffer_unmap(inbuf, &in_map);
    } else if (filter->file_type == FILE_TYPE_PCD) {
        // Process PCD file (example logic, adjust as needed)
        GST_INFO_OBJECT(filter, "Processing PCD file");
        // Add PCD-specific parsing logic here
    }

    gst_buffer_remove_all_memory(outbuf);
    if (!gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_ALL, 0, -1)) {
        GST_ERROR_OBJECT(filter, "Failed to copy input buffer to output buffer");
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_ERROR;
    }

    LidarMeta *lidar_meta = add_lidar_meta(outbuf, point_count, float_data);
    if (!lidar_meta) {
        GST_ERROR_OBJECT(filter, "Failed to add lidar meta to buffer");
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_ERROR;
    }

    // Debug dump: print lidar_point_count and first few floats
    GstMapInfo out_map;
    if (gst_buffer_map(outbuf, &out_map, GST_MAP_READ)) {
        const float *f = reinterpret_cast<const float *>(out_map.data);
        gsize n = out_map.size / sizeof(float);
        gsize dump = MIN(n, 5); 
        std::ostringstream oss;
        oss << "lidar_point_count=" << lidar_meta->lidar_point_count << " dump(" << dump << "/" << n << "): ";
        for (gsize i = 0; i < dump; ++i) {
            oss << std::fixed << std::setprecision(6) << f[i] << " ";
        }
        GST_INFO_OBJECT(filter, "%s", oss.str().c_str());
        gst_buffer_unmap(outbuf, &out_map);
    } else {
        GST_WARNING_OBJECT(filter, "Failed to map outbuf for dump");
    }

    GST_INFO_OBJECT(filter, "Successfully processed lidar buffer with %u floats", lidar_meta->lidar_point_count);

    g_mutex_unlock(&filter->mutex);

    return GST_FLOW_OK;
}


static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_lidar_parse_debug, "lidarparse", 0, "Lidar Binary Parser");

    return gst_element_register(plugin, "lidarparse", GST_RANK_NONE, GST_TYPE_LIDAR_PARSE);
}

#ifndef PACKAGE
#define PACKAGE "lidarparse"
#endif

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    lidarparse,
    "Lidar Binary Parser",
    plugin_init,
    "1.0",
    "LGPL",
    "dlstreamer",
    "https://github.com/dlstreamer/dlstreamer"
)
