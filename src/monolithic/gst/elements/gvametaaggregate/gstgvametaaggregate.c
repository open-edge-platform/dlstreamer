/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvametaaggregate.h"
#include "metaaggregate.h"
#include "utils.h"
#include <gst/gstallocator.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_gva_meta_aggregate_debug);
#define GST_CAT_DEFAULT gst_gva_meta_aggregate_debug

/**
 * Metadata aggregation from multiple streams.
 * The element has 1..N sink pads and one source pad.
 * Reuses buffer from the first sink and adds metas from others, if any.
 * The order of element`s pads is defined by their order in the pipeline.
 * INPUT: [GstBuffer + GstVideoRegionOfInterestMeta]
 * OUTPUT: INPUT + extended GstVideoRegionOfInterestMeta
 */
#define ELEMENT_LONG_NAME "Meta Aggregate"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Aggregates inference results from multiple pipeline branches. Data that is transferred further along the "        \
    "pipeline is taken from the first sink pad of the gvametaaggregate element."

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE(GstGvaMetaAggregatePad, gst_gva_meta_aggregate_pad, GST_TYPE_AGGREGATOR_PAD,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_meta_aggregate_debug, "gvametaaggregate", 0,
                                                "debug category for gvametaaggregate element"));

static void update_caps_feature(GstCaps *caps, CapsFeature feature) {
    if (caps == NULL)
        return;

    switch (feature) {
    case VA_SURFACE_CAPS_FEATURE:
        gst_caps_set_features_simple(caps, gst_caps_features_from_string(VASURFACE_FEATURE_STR));
        break;
    case VA_MEMORY_CAPS_FEATURE:
        gst_caps_set_features_simple(caps, gst_caps_features_from_string(VAMEMORY_FEATURE_STR));
        break;
    case DMA_BUF_CAPS_FEATURE:
        gst_caps_set_features_simple(caps, gst_caps_features_from_string(DMABUF_FEATURE_STR));
        break;
    default:
        break;
    }
}

static GstCaps *gst_gva_meta_aggregate_pad_get_caps(GstGvaMetaAggregatePad *pad) {
    GstCaps *caps = gst_video_info_to_caps(&pad->info);
    update_caps_feature(caps, pad->caps_feature);
    return caps;
}

static GstFlowReturn gst_gva_meta_aggregate_flush_pad(GstAggregatorPad *aggpad, GstAggregator *aggregator) {
    UNUSED(aggregator);
    GstGvaMetaAggregatePad *pad = GST_GVA_META_AGGREGATE_PAD(aggpad);
    gst_buffer_replace(&pad->buffer, NULL);
    pad->start_time = GST_CLOCK_TIME_NONE;
    pad->end_time = GST_CLOCK_TIME_NONE;

    return GST_FLOW_OK;
}

static void gst_gva_meta_aggregate_pad_class_init(GstGvaMetaAggregatePadClass *klass) {
    GstAggregatorPadClass *aggpadclass = GST_AGGREGATOR_PAD_CLASS(klass);
    aggpadclass->flush = GST_DEBUG_FUNCPTR(gst_gva_meta_aggregate_flush_pad);
}

static void gst_gva_meta_aggregate_pad_init(GstGvaMetaAggregatePad *gvametaaggregatepad) {
    UNUSED(gvametaaggregatepad);
}

gboolean gst_gva_meta_aggregate_pad_has_current_buffer(GstGvaMetaAggregatePad *pad) {
    g_return_val_if_fail(GST_IS_GVA_META_AGGREGATE_PAD(pad), FALSE);

    return pad->buffer != NULL;
}

GstBuffer *gst_gva_meta_aggregate_pad_get_current_buffer(GstGvaMetaAggregatePad *pad) {
    g_return_val_if_fail(GST_IS_GVA_META_AGGREGATE_PAD(pad), NULL);

    return pad->buffer;
}

static void gst_gva_meta_aggregate_init(GstGvaMetaAggregate *self, GstGvaMetaAggregateClass *klass);
static void gst_gva_meta_aggregate_class_init(GstGvaMetaAggregateClass *klass);
static gpointer gst_gva_meta_aggregate_parent_class = NULL;

GType gst_gva_meta_aggregate_get_type(void) {
    static gsize g_define_type_id_volatile = 0;

    if (g_once_init_enter(&g_define_type_id_volatile)) {
        GType g_define_type_id = g_type_register_static_simple(
            GST_TYPE_AGGREGATOR, g_intern_static_string("GstGvaMetaAggregate"), sizeof(GstGvaMetaAggregateClass),
            (GClassInitFunc)((void *)gst_gva_meta_aggregate_class_init), sizeof(GstGvaMetaAggregate),
            (GInstanceInitFunc)((void *)gst_gva_meta_aggregate_init), (GTypeFlags)0);

        g_once_init_leave(&g_define_type_id_volatile, g_define_type_id);
    }
    return g_define_type_id_volatile;
}

static void gst_gva_meta_aggregate_reset(GstGvaMetaAggregate *gvametaaggregate) {
    GstAggregator *agg = GST_AGGREGATOR(gvametaaggregate);

    gst_video_info_init(&gvametaaggregate->info);
    gvametaaggregate->ts_offset = 0;
    gvametaaggregate->nframes = 0;

    GST_AGGREGATOR_PAD(agg->srcpad)->segment.position = GST_CLOCK_TIME_NONE;

    GST_OBJECT_LOCK(gvametaaggregate);
    for (GList *l = GST_ELEMENT(gvametaaggregate)->sinkpads; l; l = l->next) {
        GstGvaMetaAggregatePad *p = l->data;

        gst_buffer_replace(&p->buffer, NULL);
        p->start_time = GST_CLOCK_TIME_NONE;
        p->end_time = GST_CLOCK_TIME_NONE;

        gst_video_info_init(&p->info);
    }

    GST_OBJECT_UNLOCK(gvametaaggregate);
}

static gboolean gst_gva_meta_aggregate_pad_sink_setcaps(GstPad *pad, GstObject *parent, GstCaps *caps) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(parent);
    GstGvaMetaAggregatePad *gvametaaggregatepad = GST_GVA_META_AGGREGATE_PAD(pad);

    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        return FALSE;
    }
    CapsFeature feature = get_caps_feature(caps);

    GST_GVA_META_AGGREGATE_LOCK(gvametaaggregate);
    gvametaaggregatepad->info = info;
    gvametaaggregatepad->caps_feature = feature;
    GST_GVA_META_AGGREGATE_UNLOCK(gvametaaggregate);

    return TRUE;
}

static gboolean gst_gva_meta_aggregate_sink_event(GstAggregator *agg, GstAggregatorPad *bpad, GstEvent *event) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);
    GstGvaMetaAggregatePad *pad = GST_GVA_META_AGGREGATE_PAD(bpad);
    gboolean ret = TRUE;
    static gboolean stream_started = FALSE;
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_STREAM_START: {
        stream_started = TRUE;
        gst_event_ref(event);
        gst_pad_push_event(GST_AGGREGATOR_SRC_PAD(gvametaaggregate), event);
        break;
    }
    case GST_EVENT_CAPS: {
        GstCaps *caps;
        gst_event_parse_caps(event, &caps);
        GstVideoInfo info;
        ret |= gst_video_info_from_caps(&info, caps);
        gvametaaggregate->info = info;
        ret = gst_gva_meta_aggregate_pad_sink_setcaps(GST_PAD(pad), GST_OBJECT(gvametaaggregate), caps);
        if (stream_started) {
            ret |= gst_pad_push_event(GST_AGGREGATOR_SRC_PAD(gvametaaggregate), event);
        }
        event = NULL;
        break;
    }
    case GST_EVENT_SEGMENT: {
        GstSegment seg;
        gst_event_copy_segment(event, &seg);
        g_assert(seg.format == GST_FORMAT_TIME);
        break;
    }
    default:
        break;
    }

    if (event != NULL)
        return GST_AGGREGATOR_CLASS(gst_gva_meta_aggregate_parent_class)->sink_event(agg, bpad, event);

    return ret;
}

static gboolean gst_gva_meta_aggregate_stop(GstAggregator *agg) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);

    gst_gva_meta_aggregate_reset(gvametaaggregate);

    return TRUE;
}

static gboolean gst_gva_meta_aggregate_propose_allocation(GstAggregator *agg, GstAggregatorPad *pad,
                                                          GstQuery *decide_query, GstQuery *query) {
    UNUSED(agg);
    UNUSED(pad);
    UNUSED(decide_query);
    if (query) {
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        return TRUE;
    }
    return FALSE;
}

static void gst_gva_meta_aggregate_finalize(GObject *o) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(o);

    g_mutex_clear(&gvametaaggregate->mutex);

    G_OBJECT_CLASS(gst_gva_meta_aggregate_parent_class)->finalize(o);
}

static void gst_gva_meta_aggregate_dispose(GObject *o) {

    G_OBJECT_CLASS(gst_gva_meta_aggregate_parent_class)->dispose(o);
}

static GstCaps *gst_gva_meta_aggregate_default_fixate_src_caps(GstAggregator *agg, GstCaps *caps) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);

    GST_OBJECT_LOCK(gvametaaggregate);
    GList *sinkpads = GST_ELEMENT(gvametaaggregate)->sinkpads;
    GstGvaMetaAggregatePad *first_pad = sinkpads->data;
    gint width = GST_VIDEO_INFO_WIDTH(&first_pad->info);
    gint height = GST_VIDEO_INFO_HEIGHT(&first_pad->info);
    gint fps_n = GST_VIDEO_INFO_FPS_N(&first_pad->info);
    gint fps_d = GST_VIDEO_INFO_FPS_D(&first_pad->info);
    CapsFeature feature = first_pad->caps_feature;
    GST_OBJECT_UNLOCK(gvametaaggregate);

    caps = gst_caps_make_writable(caps);
    update_caps_feature(caps, feature);
    GstStructure *s = gst_caps_get_structure(caps, 0);
    gst_structure_fixate_field_nearest_int(s, "width", width);
    gst_structure_fixate_field_nearest_int(s, "height", height);
    gst_structure_fixate_field_nearest_fraction(s, "framerate", fps_n, fps_d);
    if (gst_structure_has_field(s, "pixel-aspect-ratio"))
        gst_structure_fixate_field_nearest_fraction(s, "pixel-aspect-ratio", 1, 1);
    return gst_caps_fixate(caps);
}

static GstFlowReturn gst_gva_meta_aggregate_update_src_caps(GstAggregator *agg, GstCaps *caps, GstCaps **ret) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);
    gboolean needs_reconfigure = TRUE;

    GST_OBJECT_LOCK(gvametaaggregate);
    for (GList *l = GST_ELEMENT(gvametaaggregate)->sinkpads; l; l = l->next) {
        GstGvaMetaAggregatePad *pad = l->data;
        if (GST_VIDEO_INFO_WIDTH(&pad->info) == 0 || GST_VIDEO_INFO_HEIGHT(&pad->info) == 0)
            continue;

        needs_reconfigure = FALSE;
    }

    if (needs_reconfigure) {
        gst_pad_mark_reconfigure(agg->srcpad);
        GST_OBJECT_UNLOCK(gvametaaggregate);
        return GST_AGGREGATOR_FLOW_NEED_DATA;
    }

    GList *sinkpads = GST_ELEMENT(gvametaaggregate)->sinkpads;
    GstGvaMetaAggregatePad *first_pad = sinkpads->data;
    GstCaps *first_caps = gst_gva_meta_aggregate_pad_get_caps(first_pad);

    if (!gst_caps_can_intersect(caps, first_caps)) {
        GST_ELEMENT_ERROR(
            gvametaaggregate, STREAM, FORMAT,
            ("Can't apply first sink pad's caps to src caps.\n\tInit caps are %s.\n\tFirst sink pad's caps are %s",
             gst_caps_to_string(caps), gst_caps_to_string(first_caps)),
            (NULL));
        gst_caps_unref(first_caps);
        GST_OBJECT_UNLOCK(gvametaaggregate);
        return GST_FLOW_ERROR;
    }

    // update src caps pad
    GstFlowReturn flow_return = GST_FLOW_OK;
    GstGvaMetaAggregatePad *src_pad = GST_GVA_META_AGGREGATE_PAD(GST_AGGREGATOR_SRC_PAD(gvametaaggregate));
    if (gst_video_info_from_caps(&src_pad->info, first_caps)) {
        // send caps update event
        *ret = gst_caps_copy(first_caps);
    } else {
        GST_WARNING_OBJECT(gvametaaggregate, "update_src_caps: gst_video_info_from_caps failed");
        flow_return = GST_FLOW_ERROR;
    }
    GST_OBJECT_UNLOCK(gvametaaggregate);

    return flow_return;
}

static gboolean gst_gva_meta_aggregate_default_negotiated_src_caps(GstAggregator *agg, GstCaps *caps) {
    GstGvaMetaAggregate *gvametaaggregate = GST_GVA_META_AGGREGATE(agg);
    GstVideoInfo info;

    if (!gst_video_info_from_caps(&info, caps))
        return FALSE;

    if (GST_VIDEO_INFO_FPS_N(&gvametaaggregate->info) != GST_VIDEO_INFO_FPS_N(&info) ||
        GST_VIDEO_INFO_FPS_D(&gvametaaggregate->info) != GST_VIDEO_INFO_FPS_D(&info)) {
        if (GST_AGGREGATOR_PAD(agg->srcpad)->segment.position != GST_CLOCK_TIME_NONE) {
            gvametaaggregate->nframes = 0;
            GST_DEBUG_OBJECT(gvametaaggregate, "Framerate changed");
        }
    }

    gvametaaggregate->info = info;

    if (gvametaaggregate->current_caps == NULL || gst_caps_is_equal(caps, gvametaaggregate->current_caps) == FALSE) {
        gst_caps_replace(&gvametaaggregate->current_caps, caps);
        gst_aggregator_set_src_caps(agg, caps);
    }

    return TRUE;
}

static void gst_gva_meta_aggregate_class_init(GstGvaMetaAggregateClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_meta_aggregate_debug, "gvametaaggregate", 0, "gvametaaggregate");

    gst_gva_meta_aggregate_parent_class = g_type_class_peek_parent(klass);

    gobject_class->finalize = gst_gva_meta_aggregate_finalize;
    gobject_class->dispose = gst_gva_meta_aggregate_dispose;

    agg_class->stop = gst_gva_meta_aggregate_stop;
    agg_class->sink_event = gst_gva_meta_aggregate_sink_event;
    agg_class->fixate_src_caps = gst_gva_meta_aggregate_default_fixate_src_caps;
    agg_class->negotiated_src_caps = gst_gva_meta_aggregate_default_negotiated_src_caps;
    agg_class->update_src_caps = gst_gva_meta_aggregate_update_src_caps;
    agg_class->propose_allocation = gst_gva_meta_aggregate_propose_allocation;
    agg_class->aggregate = gst_gva_meta_aggregate_aggregate;
    klass->aggregate_metas = aggregate_metas;

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), ELEMENT_LONG_NAME, "Metadata", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template_with_gtype(gstelement_class, &src_factory,
                                                         GST_TYPE_GVA_META_AGGREGATE_PAD);
    gst_element_class_add_static_pad_template_with_gtype(gstelement_class, &sink_factory,
                                                         GST_TYPE_GVA_META_AGGREGATE_PAD);

    g_type_class_ref(GST_TYPE_GVA_META_AGGREGATE_PAD);
}

static void gst_gva_meta_aggregate_init(GstGvaMetaAggregate *gvametaaggregate, GstGvaMetaAggregateClass *klass) {
    UNUSED(klass);
    g_mutex_init(&gvametaaggregate->mutex);
    gst_gva_meta_aggregate_reset(gvametaaggregate);
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvametaaggregate", GST_RANK_NONE, GST_TYPE_GVA_META_AGGREGATE))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvametaaggregate, PRODUCT_FULL_NAME " gvametaaggregate element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
