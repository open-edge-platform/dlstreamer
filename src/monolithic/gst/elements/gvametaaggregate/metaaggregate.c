/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "metaaggregate.h"
#include "glib.h"
#include "gst/analytics/analytics.h"
#include "gst/analytics/gstanalyticsclassificationmtd.h"
#include "gst/gstclock.h"
#include "gst/gstinfo.h"
#include "utils.h"
#include <gst/gst.h>
#include <gstanalyticskeypointsmtd.h>

gboolean buffer_attach_roi_meta_from_sink_pad(GstBuffer *buf, const GstVideoInfo *src_pad_video_info,
                                              GstGvaMetaAggregatePad *sink_pad);

gboolean roi_meta_scale(GstVideoRegionOfInterestMeta *roi_meta, const GstVideoInfo *video_info,
                        const GstStructure *detection) {
    if (!detection) {
        GST_ERROR("Detection tensor should be passed to gvametaaggregate as part of GstVideoRegionOfInterestMeta");
        return FALSE;
    }
    if (!roi_meta || !video_info || !detection) {
        GST_ERROR("roi_meta_scale: bad arguments");
        return FALSE;
    }

    gdouble x_min, x_max, y_min, y_max, w, h;
    if (!gst_structure_get_double(detection, "x_min", &x_min) ||
        !gst_structure_get_double(detection, "x_max", &x_max) ||
        !gst_structure_get_double(detection, "y_min", &y_min) ||
        !gst_structure_get_double(detection, "y_max", &y_max)) {
        GST_ERROR("roi_meta_scale: error getting bbox coordinates");
        return FALSE;
    }
    w = x_max - x_min;
    h = y_max - y_min;

    // clip to [0, 1] range
    if (!((x_min >= 0) && (y_min >= 0) && (w >= 0) && (h >= 0) && (x_max <= 1) && (y_max <= 1))) {
        GST_DEBUG("ROI coordinates x=[%.5f, %.5f], y=[%.5f, %.5f] are out of range [0,1] and will be clipped", x_min,
                  x_max, y_min, y_max);
        x_min = (x_min < 0) ? 0 : (x_min > 1) ? 1 : x_min;
        y_min = (y_min < 0) ? 0 : (y_min > 1) ? 1 : y_min;
        w = (w < 0) ? 0 : (w > 1 - x_min) ? 1 - x_min : w;
        h = (h < 0) ? 0 : (h > 1 - y_min) ? 1 - y_min : h;
    }

    // update GstVideoRegionOfInterestMeta
    roi_meta->x = x_min * video_info->width;
    roi_meta->y = y_min * video_info->height;
    roi_meta->w = w * video_info->width;
    roi_meta->h = h * video_info->height;

    return TRUE;
}

GstFlowReturn aggregate_metas(GstGvaMetaAggregate *magg, GstBuffer *outbuf) {
    GstGvaMetaAggregatePad *src_pad = GST_GVA_META_AGGREGATE_PAD(GST_AGGREGATOR_SRC_PAD(magg));
    if (!src_pad) {
        GST_ERROR("Nullptr src pad during meta aggregate. Meta won't be aggregated");
        return GST_FLOW_ERROR;
    }
    if (!outbuf) {
        GST_ERROR("Ouput buffer is null. Meta won't be aggregated");
        return GST_FLOW_ERROR;
    }

    GList *first_sink_pad_it = GST_ELEMENT(magg)->sinkpads;
    for (GList *l = first_sink_pad_it->next; l; l = l->next) {
        GstGvaMetaAggregatePad *pad = GST_GVA_META_AGGREGATE_PAD_CAST(l->data);
        if (!pad) {
            GST_ERROR("Nullptr sink pad during meta aggregate");
            return GST_FLOW_ERROR;
        }
        gboolean status = buffer_attach_roi_meta_from_sink_pad(outbuf, &src_pad->info, pad);
        if (status == FALSE)
            return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

gboolean copy_one_gst_analytics_mtd(GstAnalyticsRelationMeta *dst, const GstAnalyticsMtd *mtd, GstAnalyticsMtd *new_mtd,
                                    gdouble scale_x, gdouble scale_y) {
    if (!dst || !mtd->meta) {
        GST_ERROR("copy_one_gst_analytics_mtd: bad arguments");
        return FALSE;
    }

    GstAnalyticsMtdType mtd_type = gst_analytics_mtd_get_mtd_type(mtd);

    if (mtd_type == gst_analytics_od_mtd_get_mtd_type()) {
        gint x, y, w, h;
        gfloat r, loc_conf_lvl;
        if (!gst_analytics_od_mtd_get_oriented_location(mtd, &x, &y, &w, &h, &r, &loc_conf_lvl)) {
            GST_ERROR("Failed to get oriented location from GstAnalyticsODMtd");
            return FALSE;
        }

        GQuark label_quark = gst_analytics_od_mtd_get_obj_type(mtd);

        // Applies scaling factors to x, y coordinates and width, height dimensions.
        // The addition of 0.5 before casting to integer ensures proper rounding
        // to the nearest integer value instead of truncation, which provides
        // more accurate coordinate transformation when scaling.
        gint new_x = scale_x != 1 ? (gint)(x * scale_x + 0.5) : x;
        gint new_y = scale_y != 1 ? (gint)(y * scale_y + 0.5) : y;
        gint new_w = scale_x != 1 ? (gint)(w * scale_x + 0.5) : w;
        gint new_h = scale_y != 1 ? (gint)(h * scale_y + 0.5) : h;

        if (!gst_analytics_relation_meta_add_oriented_od_mtd(dst, label_quark, new_x, new_y, new_w, new_h, r,
                                                             loc_conf_lvl, new_mtd)) {
            GST_ERROR("Failed to add GstAnalyticsODMtd to GstAnalyticsRelationMeta");
            return FALSE;
        }
    } else if (mtd_type == gst_analytics_cls_mtd_get_mtd_type()) {
        gsize length = gst_analytics_cls_mtd_get_length(mtd);
        gfloat *confidence_levels = g_new(gfloat, length);
        GQuark *class_quarks = g_new(GQuark, length);

        for (gsize i = 0; i < length; i++) {
            confidence_levels[i] = gst_analytics_cls_mtd_get_level(mtd, i);
            class_quarks[i] = gst_analytics_cls_mtd_get_quark(mtd, i);
        }

        gboolean success =
            gst_analytics_relation_meta_add_cls_mtd(dst, length, confidence_levels, class_quarks, new_mtd);

        g_free(confidence_levels);
        g_free(class_quarks);

        if (!success) {
            GST_ERROR("Failed to add GstAnalyticsClassificationMtd to GstAnalyticsRelationMeta");
            return FALSE;
        }
    } else if (mtd_type == gst_analytics_keypointgroup_mtd_get_mtd_type()) {
        gsize keypoint_count = gst_analytics_keypointgroup_mtd_get_count((const GstAnalyticsKeypointGroupMtd *)mtd);
        GstAnalyticsKeypointMtd *keypoints = g_new(GstAnalyticsKeypointMtd, keypoint_count);

        for (gsize i = 0; i < keypoint_count; i++) {
            GstAnalyticsKeypointMtd keypoint_mtd;
            if (!gst_analytics_keypointgroup_mtd_get_keypoint_mtd((const GstAnalyticsKeypointGroupMtd *)mtd,
                                                                  &keypoint_mtd, i)) {
                GST_ERROR("Failed to get keypoint mtd from keypoint group mtd");
                g_free(keypoints);
                return FALSE;
            }

            GstAnalyticsKeypoint keypoint;
            if (!gst_analytics_keypoint_mtd_get(&keypoint_mtd, &keypoint)) {
                GST_ERROR("Failed to get keypoint from keypoint mtd");
                g_free(keypoints);
                return FALSE;
            }

            if (!gst_analytics_relation_meta_add_keypoint_mtd(dst, &keypoint, &keypoints[i])) {
                GST_ERROR("Failed to add keypoint mtd to relation meta");
                g_free(keypoints);
                return FALSE;
            }
        }

        if (!gst_analytics_relation_meta_add_keypointgroup_mtd(dst, keypoint_count, keypoints,
                                                               (GstAnalyticsKeypointGroupMtd *)new_mtd)) {
            GST_ERROR("Failed to add keypoint group mtd to relation meta");
            g_free(keypoints);
            return FALSE;
        }

        g_free(keypoints);
    } else if (mtd_type == gst_analytics_keypoint_skeleton_mtd_get_mtd_type()) {
        gsize skeleton_count =
            gst_analytics_keypoint_skeleton_mtd_get_count((const GstAnalyticsKeypointSkeletonMtd *)mtd);
        GstAnalyticsKeypointPair *skeletons = g_new(GstAnalyticsKeypointPair, skeleton_count);

        for (gsize i = 0; i < skeleton_count; i++) {
            if (!gst_analytics_keypoint_skeleton_mtd_get((const GstAnalyticsKeypointSkeletonMtd *)mtd, &skeletons[i],
                                                         i)) {
                GST_ERROR("Failed to get keypoint pair from keypoint skeleton mtd");
                g_free(skeletons);
                return FALSE;
            }
        }

        if (!gst_analytics_relation_meta_add_keypoint_skeleton_mtd(dst, skeleton_count, skeletons,
                                                                   (GstAnalyticsKeypointSkeletonMtd *)new_mtd)) {
            GST_ERROR("Failed to add keypoint skeleton mtd to relation meta");
            g_free(skeletons);
            return FALSE;
        }

        g_free(skeletons);
    } else if (mtd_type == gst_analytics_tracking_mtd_get_mtd_type()) {
        guint64 tracking_id;
        GstClockTime tracking_first_seen, tracking_last_seen;
        gboolean tracking_lost;

        if (!gst_analytics_tracking_mtd_get_info(mtd, &tracking_id, &tracking_first_seen, &tracking_last_seen,
                                                 &tracking_lost)) {
            GST_ERROR("Failed to get tracking info from GstAnalyticsTrackingMtd");
            return FALSE;
        }

        if (!gst_analytics_relation_meta_add_tracking_mtd(dst, tracking_id, tracking_first_seen, new_mtd)) {
            GST_ERROR("Failed to add GstAnalyticsTrackingMtd to GstAnalyticsRelationMeta");
            return FALSE;
        }
    } else if (mtd_type == gst_analytics_keypoint_mtd_get_mtd_type()) {
        return FALSE; // Keypoint mtds are copied as part of keypoint group mtd
    } else if (mtd_type == gst_analytics_segmentation_mtd_get_mtd_type()) {
        return FALSE; // Segmentation mtds are not supported yet
    }
    return TRUE;
}

gboolean copy_all_gst_analytics_mtd(GstAnalyticsRelationMeta *src, GstAnalyticsRelationMeta *dst, GHashTable *id_map,
                                    gdouble scale_x, gdouble scale_y) {
    if (!src || !dst || !id_map) {
        GST_ERROR("copy_all_gst_analytics_mtd: bad arguments");
        return FALSE;
    }

    // GHashTable *id_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    gpointer state = NULL;
    GstAnalyticsMtd mtd;
    while (gst_analytics_relation_meta_iterate(src, &state, GST_ANALYTICS_MTD_TYPE_ANY, &mtd)) {
        if (gst_analytics_mtd_get_mtd_type(&mtd) == gst_analytics_keypoint_mtd_get_mtd_type()) {
            // Keypoint mtds are copied as part of keypoint group mtd, skip them here
            continue;
        }

        if (gst_analytics_mtd_get_mtd_type(&mtd) == gst_analytics_tracking_mtd_get_mtd_type()) {
            continue;
        }

        GstAnalyticsMtd new_mtd;
        if (!copy_one_gst_analytics_mtd(dst, &mtd, &new_mtd, scale_x, scale_y)) {
            GST_ERROR("Failed to copy one analytics mtd");
            return FALSE;
        }

        g_hash_table_insert(id_map, GUINT_TO_POINTER(mtd.id), GUINT_TO_POINTER(new_mtd.id));
    }

    // Copy relations between metadata
    gpointer original_id, new_id;
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, id_map);

    while (g_hash_table_iter_next(&iter, &original_id, &new_id)) {
        guint orig_id = GPOINTER_TO_UINT(original_id);
        guint new_id_val = GPOINTER_TO_UINT(new_id);

        gpointer state = NULL;
        GstAnalyticsMtd rlt_mtd;
        while (gst_analytics_relation_meta_get_direct_related(src, orig_id, GST_ANALYTICS_REL_TYPE_ANY,
                                                              GST_ANALYTICS_MTD_TYPE_ANY, &state, &rlt_mtd)) {
            gpointer related_new_id = g_hash_table_lookup(id_map, GUINT_TO_POINTER(rlt_mtd.id));
            if (!related_new_id) {
                GST_ERROR("Failed to find new id for related mtd id %u", rlt_mtd.id);
                return FALSE;
            }

            GstAnalyticsRelTypes rel_type = gst_analytics_relation_meta_get_relation(src, orig_id, rlt_mtd.id);

            if (!gst_analytics_relation_meta_set_relation(dst, rel_type, new_id_val,
                                                          GPOINTER_TO_UINT(related_new_id))) {
                GST_ERROR("Failed to set relation between mtd ids %u and %u", new_id_val,
                          GPOINTER_TO_UINT(related_new_id));
                return FALSE;
            }
        }

        if (!gst_analytics_relation_meta_get_mtd(dst, new_id_val, GST_ANALYTICS_MTD_TYPE_ANY, &rlt_mtd)) {
            GST_ERROR("Failed to get mtd by id %u from dst relation meta", new_id_val);
            return FALSE;
        }

        if (gst_analytics_mtd_get_mtd_type(&rlt_mtd) == gst_analytics_keypointgroup_mtd_get_mtd_type()) {
            if (!gst_analytics_relation_meta_set_keypointgroup_relations(dst, (GstAnalyticsKeypointGroupMtd *)&rlt_mtd,
                                                                         NULL, NULL)) {
                GST_ERROR("Failed to set keypoint group relations");
                return FALSE;
            }
        }
    }

    // g_hash_table_destroy(id_map);

    return TRUE;
}

gboolean buffer_attach_roi_meta_from_sink_pad(GstBuffer *buf, const GstVideoInfo *src_pad_video_info,
                                              GstGvaMetaAggregatePad *sink_pad) {
    GstMeta *meta = NULL;
    gpointer state = NULL;
    GstStructure *detection = NULL;
    if (!buf || !sink_pad || !src_pad_video_info)
        return FALSE;

    GstVideoInfo *sink_pad_video_info = &sink_pad->info;
    if (!sink_pad_video_info)
        return FALSE;

    GstBuffer *buf_with_meta = sink_pad->buffer;
    if (!buf_with_meta)
        return TRUE; // there is no buffer on the sink_pad this time. It's accepted behavior

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buf_with_meta);
    if (!relation_meta) {
        // No relation meta on the sink pad buffer. Nothing to attach
        return TRUE;
    }

    g_return_val_if_fail(gst_buffer_is_writable(buf), FALSE);
    GstAnalyticsRelationMeta *new_relation_meta = gst_buffer_add_analytics_relation_meta(buf);

    if (!new_relation_meta) {
        GST_ERROR("Failed to add GstAnalyticsRelationMeta to output buffer");
        return FALSE;
    }

    gfloat scale_x = (gfloat)sink_pad_video_info->width / (gfloat)src_pad_video_info->width;
    gfloat scale_y = (gfloat)sink_pad_video_info->height / (gfloat)src_pad_video_info->height;

    GHashTable *id_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!copy_all_gst_analytics_mtd(relation_meta, new_relation_meta, id_map, scale_x, scale_y)) {
        GST_ERROR("Failed to copy all analytics mtd from sink buffer to output buffer");
        return FALSE;
    }

    while ((meta = gst_buffer_iterate_meta(buf_with_meta, &state))) {
        g_return_val_if_fail(gst_buffer_is_writable(buf), FALSE);
        if (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
            GstVideoRegionOfInterestMeta *original_roi_meta = (GstVideoRegionOfInterestMeta *)meta;

            GstVideoRegionOfInterestMeta *output_meta = gst_buffer_add_video_region_of_interest_meta(
                buf, g_quark_to_string(original_roi_meta->roi_type), original_roi_meta->x, original_roi_meta->y,
                original_roi_meta->w, original_roi_meta->h);
            output_meta->id = GPOINTER_TO_INT(g_hash_table_lookup(id_map, GINT_TO_POINTER(original_roi_meta->id)));

            for (GList *l = original_roi_meta->params; l; l = l->next) {
                GstStructure *s = GST_STRUCTURE(l->data);
                if (!gst_structure_has_name(s, "object_id")) {
                    gst_video_region_of_interest_meta_add_param(
                        output_meta, gst_structure_copy(gst_video_region_of_interest_meta_get_param(
                                         original_roi_meta, gst_structure_get_name(s))));
                    if (gst_structure_has_name(s, "detection"))
                        detection = s;
                }
            }

            if (src_pad_video_info->width != sink_pad_video_info->width ||
                src_pad_video_info->height != sink_pad_video_info->height) {
                // apply scale only when needed (if image size on src pad is different from image size on this
                // sink pad)
                g_return_val_if_fail(roi_meta_scale(output_meta, src_pad_video_info, detection), FALSE);
            }
        } else if (meta->info->api == GST_ANALYTICS_RELATION_META_API_TYPE) {
            // Already copied all analytics mtd above. Nothing to do here
        } else if (meta->info->transform_func) {
            // Try to copy the whole meta from sink buffer to out buffer
            GstMetaTransformCopy copy_data = {.region = FALSE, .offset = 0, .size = -1};
            if (!meta->info->transform_func(buf, meta, buf_with_meta, _gst_meta_transform_copy, &copy_data)) {
                GST_ERROR("Failed to copy metadata to out buffer");
                return FALSE;
            }
        }
    }

    g_hash_table_destroy(id_map);

    return TRUE;
}

GstFlowReturn gst_gva_meta_aggregate_fill_queues(GstGvaMetaAggregate *gvametaaggregate,
                                                 GstClockTime output_start_running_time,
                                                 GstClockTime output_end_running_time) {

    gboolean eos = TRUE;
    gboolean need_more_data = FALSE;
    gboolean need_reconfigure = FALSE;

    GST_OBJECT_LOCK(gvametaaggregate);
    for (GList *l = GST_ELEMENT(gvametaaggregate)->sinkpads; l; l = l->next) {
        GstGvaMetaAggregatePad *pad = l->data;
        GstSegment segment;
        GstAggregatorPad *bpad;
        GstBuffer *buf;
        gboolean is_eos;

        bpad = GST_AGGREGATOR_PAD(pad);
        GST_OBJECT_LOCK(bpad);
        segment = bpad->segment;
        GST_OBJECT_UNLOCK(bpad);
        is_eos = gst_aggregator_pad_is_eos(bpad);

        if (!is_eos)
            eos = FALSE;
        buf = gst_aggregator_pad_peek_buffer(bpad);
        if (buf) {
            if (!gst_buffer_is_writable(buf)) {
                buf = gst_buffer_make_writable(buf);
            }
            GstClockTime start_time, end_time;

            start_time = GST_BUFFER_TIMESTAMP(buf);
            if (start_time == GST_CLOCK_TIME_NONE) {
                gst_buffer_unref(buf);
                GST_ERROR_OBJECT(pad, "Need timestamped buffers!");
                GST_OBJECT_UNLOCK(gvametaaggregate);
                return GST_FLOW_ERROR;
            }

            end_time = GST_BUFFER_DURATION(buf);

            if (end_time == GST_CLOCK_TIME_NONE) {
                start_time = MAX(start_time, segment.start);
                start_time = gst_segment_to_running_time(&segment, GST_FORMAT_TIME, start_time);

                if (start_time >= output_end_running_time) {
                    gst_buffer_unref(buf);
                    continue;
                } else if (start_time < output_start_running_time) {
                    gst_buffer_replace(&pad->buffer, buf);
                    gst_buffer_unref(buf);
                    gst_aggregator_pad_drop_buffer(bpad);
                    pad->start_time = start_time;
                    need_more_data = TRUE;
                    continue;
                }
                gst_buffer_unref(buf);
                buf = gst_aggregator_pad_pop_buffer(bpad);
                gst_buffer_replace(&pad->buffer, buf);
                pad->start_time = start_time;
                gst_buffer_unref(buf);
                continue;
            }

            g_assert(start_time != GST_CLOCK_TIME_NONE && end_time != GST_CLOCK_TIME_NONE);
            end_time += start_time;

            if (start_time >= segment.stop || end_time < segment.start) {
                gst_buffer_unref(buf);
                gst_aggregator_pad_drop_buffer(bpad);

                need_more_data = TRUE;
                continue;
            }

            start_time = MAX(start_time, segment.start);
            if (segment.stop != GST_CLOCK_TIME_NONE)
                end_time = MIN(end_time, segment.stop);
            start_time = gst_segment_to_running_time(&segment, GST_FORMAT_TIME, start_time);
            end_time = gst_segment_to_running_time(&segment, GST_FORMAT_TIME, end_time);
            g_assert(start_time != GST_CLOCK_TIME_NONE && end_time != GST_CLOCK_TIME_NONE);

            if (pad->end_time != GST_CLOCK_TIME_NONE && pad->end_time > end_time) {
                gst_buffer_unref(buf);
                gst_aggregator_pad_drop_buffer(bpad);
                continue;
            }

            if (end_time >= output_start_running_time && start_time < output_end_running_time) {
                gst_buffer_replace(&pad->buffer, buf);
                pad->start_time = start_time;
                pad->end_time = end_time;

                gst_buffer_unref(buf);
                gst_aggregator_pad_drop_buffer(bpad);
                eos = FALSE;
            } else if (start_time >= output_end_running_time) {
                gst_buffer_replace(&pad->buffer, NULL);
                gst_buffer_unref(buf);
                eos = FALSE;
            } else {
                gst_buffer_replace(&pad->buffer, buf);
                pad->start_time = start_time;
                pad->end_time = end_time;
                gst_buffer_unref(buf);
                gst_aggregator_pad_drop_buffer(bpad);

                need_more_data = TRUE;
                continue;
            }
        } else {
            gst_buffer_replace(&pad->buffer, NULL);
        }
    }
    GST_OBJECT_UNLOCK(gvametaaggregate);

    if (need_reconfigure)
        gst_pad_mark_reconfigure(GST_AGGREGATOR_SRC_PAD(gvametaaggregate));

    if (need_more_data)
        return GST_AGGREGATOR_FLOW_NEED_DATA;
    if (eos)
        return GST_FLOW_EOS;

    return GST_FLOW_OK;
}

gboolean sync_pad_values(GstElement *gvametaaggregate, GstPad *pad, gpointer user_data) {
    UNUSED(gvametaaggregate);
    gint64 *out_stream_time = user_data;
    if (GST_CLOCK_TIME_IS_VALID(*out_stream_time))
        gst_object_sync_values(GST_OBJECT_CAST(pad), *out_stream_time);

    return TRUE;
}

void gst_gva_meta_aggregate_advance_on_timeout(GstGvaMetaAggregate *gvametaaggregate) {
    GstAggregator *agg = GST_AGGREGATOR(gvametaaggregate);
    guint64 frame_duration;
    gint fps_d, fps_n;
    GstSegment *agg_segment = &GST_AGGREGATOR_PAD(agg->srcpad)->segment;

    GST_OBJECT_LOCK(agg);
    if (agg_segment->position == GST_CLOCK_TIME_NONE) {
        if (agg_segment->rate > 0.0)
            agg_segment->position = agg_segment->start;
        else
            agg_segment->position = agg_segment->stop;
    }

    fps_d = GST_VIDEO_INFO_FPS_D(&gvametaaggregate->info) ? GST_VIDEO_INFO_FPS_D(&gvametaaggregate->info) : 1;
    fps_n = GST_VIDEO_INFO_FPS_N(&gvametaaggregate->info) ? GST_VIDEO_INFO_FPS_N(&gvametaaggregate->info) : 25;
    frame_duration = gst_util_uint64_scale(GST_SECOND, fps_d, fps_n);
    if (agg_segment->rate > 0.0)
        agg_segment->position += frame_duration;
    else if (agg_segment->position > frame_duration)
        agg_segment->position -= frame_duration;
    else
        agg_segment->position = 0;
    gvametaaggregate->nframes++;
    GST_OBJECT_UNLOCK(agg);
}

GstFlowReturn gst_gva_meta_aggregate_aggregate(GstAggregator *agg, gboolean timeout) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);
    GstClockTime output_start_time, output_end_time;
    GstClockTime output_start_running_time, output_end_running_time;
    GstBuffer *outbuf = NULL;
    GstFlowReturn flow_ret;
    GstSegment *agg_segment = &GST_AGGREGATOR_PAD(agg->srcpad)->segment;

    GST_GVA_META_AGGREGATE_LOCK(gvametaaggregate);
    if (timeout)
        gst_gva_meta_aggregate_advance_on_timeout(gvametaaggregate);
    output_start_time = agg_segment->position;
    if (agg_segment->position == GST_CLOCK_TIME_NONE || agg_segment->position < agg_segment->start)
        output_start_time = agg_segment->start;

    if (gvametaaggregate->nframes == 0) {
        gvametaaggregate->ts_offset = output_start_time;
    }

    if (GST_VIDEO_INFO_FPS_N(&gvametaaggregate->info) == 0) {
        output_end_time = GST_CLOCK_TIME_NONE;
    } else {
        output_end_time = gvametaaggregate->ts_offset +
                          gst_util_uint64_scale_round(gvametaaggregate->nframes + 1,
                                                      GST_SECOND * GST_VIDEO_INFO_FPS_D(&gvametaaggregate->info),
                                                      GST_VIDEO_INFO_FPS_N(&gvametaaggregate->info));
    }

    if (agg_segment->stop != GST_CLOCK_TIME_NONE)
        output_end_time = MIN(output_end_time, agg_segment->stop);

    output_start_running_time = gst_segment_to_running_time(agg_segment, GST_FORMAT_TIME, output_start_time);
    output_end_running_time = gst_segment_to_running_time(agg_segment, GST_FORMAT_TIME, output_end_time);

    if (output_end_time == output_start_time) {
        flow_ret = GST_FLOW_EOS;
    } else {
        flow_ret =
            gst_gva_meta_aggregate_fill_queues(gvametaaggregate, output_start_running_time, output_end_running_time);
    }

    if (flow_ret != GST_FLOW_OK) {
        GST_GVA_META_AGGREGATE_UNLOCK(gvametaaggregate);
        return flow_ret;
    }

    if (gst_pad_needs_reconfigure(GST_AGGREGATOR_SRC_PAD(gvametaaggregate))) {
        flow_ret = GST_AGGREGATOR_FLOW_NEED_DATA;
        goto unlock_and_return;
    }

    flow_ret = gst_gva_meta_aggregate_do_aggregate(gvametaaggregate, output_start_time, output_end_time, &outbuf);
    if (flow_ret != GST_FLOW_OK)
        goto done;
    GST_GVA_META_AGGREGATE_UNLOCK(gvametaaggregate);
    if (outbuf) {
        outbuf = gst_buffer_ref(outbuf);
        GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_DISCONT);

        flow_ret = gst_aggregator_finish_buffer(agg, outbuf);
    }

    GST_GVA_META_AGGREGATE_LOCK(gvametaaggregate);
    gvametaaggregate->nframes++;
    agg_segment->position = output_end_time;
    GST_GVA_META_AGGREGATE_UNLOCK(gvametaaggregate);

    return flow_ret;

done:
    if (outbuf)
        gst_buffer_unref(outbuf);
unlock_and_return:
    GST_GVA_META_AGGREGATE_UNLOCK(gvametaaggregate);
    return flow_ret;
}

GstFlowReturn gst_gva_meta_aggregate_do_aggregate(GstGvaMetaAggregate *gvametaaggregate, GstClockTime output_start_time,
                                                  GstClockTime output_end_time, GstBuffer **outbuf) {
    GstAggregator *agg = GST_AGGREGATOR(gvametaaggregate);
    GstFlowReturn ret = GST_FLOW_OK;
    GstElementClass *klass = GST_ELEMENT_GET_CLASS(gvametaaggregate);
    GstGvaMetaAggregateClass *gvametaaggregate_klass = GST_GVA_META_AGGREGATE_CLASS(klass);
    GstClockTime out_stream_time;

    GList *first_sink_pad = GST_ELEMENT(gvametaaggregate)->sinkpads;
    GstGvaMetaAggregatePad *first_pad = GST_GVA_META_AGGREGATE_PAD_CAST(first_sink_pad->data);
    GstAggregatorPad *bpad = GST_AGGREGATOR_PAD(first_pad);
    *outbuf = first_pad->buffer;

    if (*outbuf == NULL) {
        return GST_FLOW_OK;
    }
    GstClockTime timestamp = gst_segment_to_stream_time(&bpad->segment, GST_FORMAT_TIME, first_pad->buffer->pts);

    GST_BUFFER_TIMESTAMP(*outbuf) = timestamp;
    GST_BUFFER_DURATION(*outbuf) = output_end_time - output_start_time;

    GST_OBJECT_LOCK(agg->srcpad);
    out_stream_time =
        gst_segment_to_stream_time(&GST_AGGREGATOR_PAD(agg->srcpad)->segment, GST_FORMAT_TIME, output_start_time);
    GST_OBJECT_UNLOCK(agg->srcpad);

    gst_element_foreach_sink_pad(GST_ELEMENT_CAST(gvametaaggregate), sync_pad_values, &out_stream_time);

    GST_OBJECT_LOCK(gvametaaggregate);
    ret = gvametaaggregate_klass->aggregate_metas(gvametaaggregate, *outbuf);
    GST_OBJECT_UNLOCK(gvametaaggregate);

    return ret;
}
