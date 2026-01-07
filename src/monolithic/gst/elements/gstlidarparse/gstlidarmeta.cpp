#include "gstlidarmeta.h"
#include <gst/gst.h>
#include <vector>
#include <new>

GType lidar_meta_api_get_type(void) {
    static GType type = 0;
    static const gchar *tags[] = { "lidar", NULL };

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register("LidarMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

static gboolean lidar_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    LidarMeta *lidar_meta = (LidarMeta *)meta;
    new (&lidar_meta->lidar_data) std::vector<float>();
    lidar_meta->lidar_point_count = 0;
    lidar_meta->frame_id = 0;
    lidar_meta->exit_lidarparse_timestamp = GST_CLOCK_TIME_NONE;
    return TRUE;
}

static void lidar_meta_free(GstMeta *meta, GstBuffer *buffer) {
    LidarMeta *lidar_meta = (LidarMeta *)meta;
    lidar_meta->lidar_data.~vector();
}

const GstMetaInfo *lidar_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *mi = gst_meta_register(
            LIDAR_META_API_TYPE,
            "LidarMeta",
            sizeof(LidarMeta),
            lidar_meta_init,  
            (GstMetaFreeFunction)lidar_meta_free,
            (GstMetaTransformFunction)NULL
        );
        g_once_init_leave(&meta_info, mi);
    }
    return meta_info;
}

LidarMeta *add_lidar_meta(GstBuffer *buffer, guint lidar_point_count, const std::vector<float> &lidar_data, size_t frame_id, GstClockTime exit_lidarparse_timestamp) {
    if (!buffer) {
        GST_WARNING("Cannot add meta to NULL buffer");
        return nullptr;
    }

    GST_DEBUG("Adding LidarMeta to buffer with lidar_point_count=%u frame_id=%zu exit_ts=%" GST_TIME_FORMAT,
              lidar_point_count, frame_id, GST_TIME_ARGS(exit_lidarparse_timestamp));

    LidarMeta *meta = (LidarMeta *)gst_buffer_add_meta(buffer, LIDAR_META_INFO, NULL);
    if (!meta) {
        GST_ERROR("Failed to add LidarMeta to buffer");
        return nullptr;
    }

    meta->lidar_point_count = lidar_point_count;
    meta->lidar_data = lidar_data;
    meta->frame_id = frame_id;
    meta->exit_lidarparse_timestamp = exit_lidarparse_timestamp;

    GST_DEBUG("LidarMeta added successfully: lidar_point_count=%u, lidar_data_size=%zu, frame_id=%zu, exit_ts=%" GST_TIME_FORMAT,
              meta->lidar_point_count, meta->lidar_data.size(), meta->frame_id, GST_TIME_ARGS(meta->exit_lidarparse_timestamp));

    return meta;
}