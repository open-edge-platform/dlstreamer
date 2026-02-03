/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dlidarparse.h"
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <fstream>
#include <gst/gstinfo.h>
#include <iomanip>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_lidar_parse_debug);
#define GST_CAT_DEFAULT gst_g3d_lidar_parse_debug

enum { PROP_0, PROP_STRIDE, PROP_FRAME_RATE };

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/octet-stream"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

static void gst_g3d_lidar_parse_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_lidar_parse_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_g3d_lidar_parse_finalize(GObject *object);

static gboolean gst_g3d_lidar_parse_start(GstBaseTransform *trans);
static gboolean gst_g3d_lidar_parse_stop(GstBaseTransform *trans);
static GstFlowReturn gst_g3d_lidar_parse_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_g3d_lidar_parse_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstCaps *gst_g3d_lidar_parse_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                   GstCaps *filter);
static gboolean gst_g3d_lidar_parse_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);

static void gst_g3d_lidar_parse_class_init(GstG3DLidarParseClass *klass);
static void gst_g3d_lidar_parse_init(GstG3DLidarParse *filter);

G_DEFINE_TYPE(GstG3DLidarParse, gst_g3d_lidar_parse, GST_TYPE_BASE_TRANSFORM);

GType file_type_get_type(void) {
    static GType file_type = 0;
    if (!file_type) {
        static const GEnumValue values[] = {
            {FILE_TYPE_BIN, "BIN", "bin"}, {FILE_TYPE_PCD, "PCD", "pcd"}, {0, NULL, NULL}};
        file_type = g_enum_register_static("FileType", values);
    }
    return file_type;
}

static void gst_g3d_lidar_parse_class_init(GstG3DLidarParseClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_g3d_lidar_parse_debug, "g3dlidarparse", 0, "Lidar Binary Parser");
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_g3d_lidar_parse_set_property;
    gobject_class->get_property = gst_g3d_lidar_parse_get_property;
    gobject_class->finalize = gst_g3d_lidar_parse_finalize;

    g_object_class_install_property(
        gobject_class, PROP_STRIDE,
        g_param_spec_int("stride", "Stride",
                         "Specifies the interval of frames to process, controls processing granularity. 1 means every "
                         "frame is processed, 2 means every second frame is processed.",
                         1, G_MAXINT, 1, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAME_RATE,
        g_param_spec_float("frame-rate", "Frame Rate",
                           "Desired output frame rate in frames per second. A value of 0 means no frame rate control.",
                           0.0, G_MAXFLOAT, 0.0, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        gstelement_class, "G3D Lidar Parser", "Filter/Converter",
        "Parses binary lidar data to vector float format with stride and frame rate control (g3dlidarparse)",
        "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_stop);
    base_transform_class->transform = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_transform);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_sink_event);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_transform_caps);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_g3d_lidar_parse_set_caps);
    base_transform_class->passthrough_on_same_caps = FALSE;
}

static void gst_g3d_lidar_parse_init(GstG3DLidarParse *filter) {
    filter->stride = 1;
    filter->frame_rate = 0.0;
    g_mutex_init(&filter->mutex);

    filter->current_index = 0;
    filter->is_single_file = FALSE;
    filter->file_type = FILE_TYPE_BIN; // Default to BIN
    filter->stream_id = 0;
}

static void gst_g3d_lidar_parse_finalize(GObject *object) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(object);

    g_mutex_clear(&filter->mutex);

    filter->current_index = 0;

    G_OBJECT_CLASS(gst_g3d_lidar_parse_parent_class)->finalize(object);
}

static void gst_g3d_lidar_parse_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(object);

    switch (prop_id) {
    case PROP_STRIDE:
        filter->stride = g_value_get_int(value);
        break;
    case PROP_FRAME_RATE:
        filter->frame_rate = g_value_get_float(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_g3d_lidar_parse_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(object);

    switch (prop_id) {
    case PROP_STRIDE:
        g_value_set_int(value, filter->stride);
        break;
    case PROP_FRAME_RATE:
        g_value_set_float(value, filter->frame_rate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_g3d_lidar_parse_start(GstBaseTransform *trans) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(trans);

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

    if (g_str_has_suffix(upstream_location, ".pcd")) {
        filter->file_type = FILE_TYPE_PCD;
        GST_INFO_OBJECT(filter, "File type set to PCD.");
    } else if (g_str_has_suffix(upstream_location, ".bin")) {
        filter->file_type = FILE_TYPE_BIN;
        GST_INFO_OBJECT(filter, "File type set to BIN.");
    } else {
        GST_ERROR_OBJECT(filter, "Unsupported file type for location: %s", upstream_location);
        g_free(upstream_location);
        gst_object_unref(upstream_element);
        gst_object_unref(peer_pad);
        return FALSE;
    }

    g_free(upstream_location);
    gst_object_unref(upstream_element);
    gst_object_unref(peer_pad);

    return TRUE;
}

static gboolean gst_g3d_lidar_parse_stop(GstBaseTransform *trans) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(trans);

    GST_INFO_OBJECT(filter, "[STOP] Stopping lidar parser");
    filter->current_index = 0;
    filter->stream_id = 0;
    GST_INFO_OBJECT(filter, "[STOP] Data cleared");

    return TRUE;
}

static gboolean gst_g3d_lidar_parse_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(trans);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_STREAM_START: {
        const gchar *stream_id = NULL;
        gst_event_parse_stream_start(event, &stream_id);

        guint parsed_group_id = 0;
        if (gst_event_parse_group_id(event, &parsed_group_id)) {
            filter->stream_id = parsed_group_id;
        }

        GST_INFO_OBJECT(filter, "Received STREAM_START stream_id=%s parsed_group_id=%u stored_stream_id=%u",
                        stream_id ? stream_id : "<null>", parsed_group_id, filter->stream_id);
        break;
    }
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

    return GST_BASE_TRANSFORM_CLASS(gst_g3d_lidar_parse_parent_class)->sink_event(trans, event);
}

static GstFlowReturn gst_g3d_lidar_parse_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(trans);
    g_mutex_lock(&filter->mutex);

    // Stride control
    if (filter->current_index % filter->stride != 0) {
        GST_DEBUG_OBJECT(filter, "Skipping file #%lu (stride=%d, remainder=%lu)", filter->current_index, filter->stride,
                         filter->current_index % filter->stride);
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

    const size_t frame_id = filter->current_index;
    GST_INFO_OBJECT(filter, "Processing file #%lu (stride=%d)", filter->current_index, filter->stride);
    filter->current_index++;

    GstMapInfo in_map;
    size_t num_floats = 0;
    size_t point_count = 0;
    std::vector<float> float_data;

    if (filter->file_type == FILE_TYPE_BIN) {
        // Process BIN file
        if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ)) {
            GST_ERROR_OBJECT(filter, "Failed to map input buffer for reading");
            g_mutex_unlock(&filter->mutex);
            return GST_FLOW_ERROR;
        }

        if (in_map.size % sizeof(float) != 0) {
            GST_ERROR_OBJECT(filter, "Buffer size (%lu) is not a multiple of float size (%lu)", in_map.size,
                             sizeof(float));
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
        // Map input for PCD parsing
        GstMapInfo in_map;
        if (!gst_buffer_map(inbuf, &in_map, GST_MAP_READ)) {
            GST_ERROR_OBJECT(filter, "Failed to map input buffer for reading (PCD)");
            g_mutex_unlock(&filter->mutex);
            return GST_FLOW_ERROR;
        }

        // Detect PCD format (ASCII or binary) using the header
        const size_t header_len = std::min(in_map.size, static_cast<size_t>(4096));
        std::string header(reinterpret_cast<const char *>(in_map.data), header_len);
        bool is_ascii = header.find("DATA ascii") != std::string::npos;

        if (is_ascii) {
            GST_INFO_OBJECT(filter, "Detected ASCII PCD format.");
            std::istringstream iss(std::string(reinterpret_cast<const char *>(in_map.data), in_map.size));
            std::string line;
            while (std::getline(iss, line)) {
                if (line.empty() || line[0] == '#')
                    continue;
                float x, y, z, i;
                if (std::istringstream(line) >> x >> y >> z >> i) {
                    float_data.insert(float_data.end(), {x, y, z, i});
                }
            }
        } else {
            GST_INFO_OBJECT(filter, "Detected binary PCD format.");
            const size_t token_pos = header.find("DATA binary");
            if (token_pos == std::string::npos) {
                GST_ERROR_OBJECT(filter, "Failed to locate binary data section in PCD file.");
                gst_buffer_unmap(inbuf, &in_map);
                g_mutex_unlock(&filter->mutex);
                return GST_FLOW_ERROR;
            }

            size_t newline_pos = header.find('\n', token_pos);
            if (newline_pos == std::string::npos) {
                GST_ERROR_OBJECT(filter, "Binary PCD header missing newline after DATA binary");
                gst_buffer_unmap(inbuf, &in_map);
                g_mutex_unlock(&filter->mutex);
                return GST_FLOW_ERROR;
            }

            const size_t payload_offset = newline_pos + 1;
            if (payload_offset >= in_map.size) {
                GST_ERROR_OBJECT(filter, "Binary PCD payload offset out of range");
                gst_buffer_unmap(inbuf, &in_map);
                g_mutex_unlock(&filter->mutex);
                return GST_FLOW_ERROR;
            }

            const size_t num_points = (in_map.size - payload_offset) / (4 * sizeof(float));
            const float *data = reinterpret_cast<const float *>(in_map.data + payload_offset);
            float_data.assign(data, data + num_points * 4);
        }

        gst_buffer_unmap(inbuf, &in_map);
        num_floats = float_data.size();
        point_count = num_floats / 4;
    }

    gst_buffer_remove_all_memory(outbuf);
    /*if (!gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_ALL, 0, -1)) {
        GST_ERROR_OBJECT(filter, "Failed to copy input buffer to output buffer");
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_ERROR;
    }*/

    GstClockTime exit_lidarparse_timestamp = GST_CLOCK_TIME_NONE;
    if (GstClock *clock = gst_element_get_clock(GST_ELEMENT(filter))) {
        exit_lidarparse_timestamp = gst_clock_get_time(clock);
        GST_DEBUG_OBJECT(filter, "exit_ts from element clock: %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(exit_lidarparse_timestamp));
        gst_object_unref(clock);
    } else {
        exit_lidarparse_timestamp = gst_util_get_timestamp();
        GST_DEBUG_OBJECT(filter, "exit_ts from gst_util_get_timestamp: %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(exit_lidarparse_timestamp));
    }

    GST_DEBUG_OBJECT(filter, "Add meta frame_id=%zu stream_id=%u exit_ts=%" GST_TIME_FORMAT " n_points=%zu stride=%d",
                     frame_id, filter->stream_id, GST_TIME_ARGS(exit_lidarparse_timestamp), point_count,
                     filter->stride);

    LidarMeta *lidar_meta =
        add_lidar_meta(outbuf, point_count, float_data, frame_id, exit_lidarparse_timestamp, filter->stream_id);
    if (!lidar_meta) {
        GST_ERROR_OBJECT(filter, "Failed to add lidar meta to buffer");
        g_mutex_unlock(&filter->mutex);
        return GST_FLOW_ERROR;
    }

    // Debug dump: print lidar_point_count and first few floats from meta data
    if (!lidar_meta->lidar_data.empty()) {
        const auto &values = lidar_meta->lidar_data;
        const gsize count = values.size();
        const gsize preview_len = std::min<gsize>(count, 5);

        std::ostringstream oss;
        oss << "lidar_point_count=" << lidar_meta->lidar_point_count << " frame_id=" << lidar_meta->frame_id
            << " stream_id=" << lidar_meta->stream_id << " exit_ts=" << lidar_meta->exit_lidarparse_timestamp << "ns"
            << " preview(" << preview_len << "/" << count << "):";

        for (gsize i = 0; i < preview_len; ++i) {
            oss << " " << std::fixed << std::setprecision(6) << values[i];
        }

        GST_INFO_OBJECT(filter, "%s", oss.str().c_str());
    } else {
        GST_INFO_OBJECT(filter, "lidar_point_count=0 preview(0/0): <empty>");
    }

    GST_INFO_OBJECT(filter, "Successfully processed lidar buffer with %u floats", lidar_meta->lidar_point_count);

    g_mutex_unlock(&filter->mutex);

    return GST_FLOW_OK;
}

static GstCaps *gst_g3d_lidar_parse_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                   GstCaps *filter) {
    (void)trans;
    (void)caps;
    GstCaps *result = nullptr;

    if (direction == GST_PAD_SINK) {
        result = gst_caps_from_string("application/x-lidar");
    } else {
        result = gst_caps_from_string("application/octet-stream");
    }

    if (filter) {
        GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = tmp;
    }

    return result;
}

static gboolean gst_g3d_lidar_parse_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstG3DLidarParse *filter = GST_G3D_LIDAR_PARSE(trans);
    if (!incaps || !outcaps) {
        GST_ERROR_OBJECT(filter, "Missing caps during set_caps");
        return FALSE;
    }
    return TRUE;
}
