// Minimal 3-input fusion aggregator to align video/lidar/calib buffers by order/PTS
#include <gst/gst.h>
#include <gst/base/gstaggregator.h>


GST_DEBUG_CATEGORY_STATIC(fusion_debug);
#define GST_CAT_DEFAULT fusion_debug

#define GST_TYPE_FUSION (gst_fusion_get_type())
G_DECLARE_FINAL_TYPE(GstFusion, gst_fusion, GST, FUSION, GstAggregator)

struct _GstFusion {
    GstAggregator parent;
    GstAggregatorPad *video_pad;
    GstAggregatorPad *lidar_pad;
    GstAggregatorPad *calib_pad;
    guint64 sequence;
};

struct _GstFusionClass {
    GstAggregatorClass parent_class;
};

G_DEFINE_TYPE(GstFusion, gst_fusion, GST_TYPE_AGGREGATOR)

static GstFlowReturn gst_fusion_aggregate(GstAggregator *aggregator, gboolean timeout);

static void gst_fusion_class_init(GstFusionClass *klass) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstAggregatorClass *aggregator_class = GST_AGGREGATOR_CLASS(klass);

    static GstStaticPadTemplate sink_video_tmpl = GST_STATIC_PAD_TEMPLATE(
        "sink_video",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

    static GstStaticPadTemplate sink_lidar_tmpl = GST_STATIC_PAD_TEMPLATE(
        "sink_lidar",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

    static GstStaticPadTemplate sink_calib_tmpl = GST_STATIC_PAD_TEMPLATE(
        "sink_calib",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

    static GstStaticPadTemplate src_tmpl = GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);

    gst_element_class_add_static_pad_template(element_class, &sink_video_tmpl);
    gst_element_class_add_static_pad_template(element_class, &sink_lidar_tmpl);
    gst_element_class_add_static_pad_template(element_class, &sink_calib_tmpl);
    gst_element_class_add_static_pad_template(element_class, &src_tmpl);

    gst_element_class_set_static_metadata(element_class,
        "Fusion aggregator (3-way)",
        "Generic",
        "Aligns video/lidar/calib buffers by order/PTS and emits fused buffer",
        "dlstreamer");

    aggregator_class->aggregate = gst_fusion_aggregate;
}

static void gst_fusion_init(GstFusion *self) {
    self->sequence = 0;

    GstElement *elem = GST_ELEMENT(self);

    auto add_sink = [elem](const gchar *name) -> GstAggregatorPad * {
        GstPadTemplate *tmpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(elem), name);
        g_return_val_if_fail(tmpl != nullptr, nullptr);

        GstAggregatorPad *pad = GST_AGGREGATOR_PAD(
            g_object_new(GST_TYPE_AGGREGATOR_PAD,
                         "name", name,
                         "direction", GST_PAD_SINK,
                         "template", tmpl,
                         nullptr));

        // If the runtime needs pad templates for caps negotiation, they are already
        // registered on the element class; here we just attach the pad to the element.

        g_return_val_if_fail(pad != nullptr, nullptr);
        // Use gst_element_add_pad to avoid dependency on newer aggregator API symbols.
        gst_element_add_pad(elem, GST_PAD(pad));
        return pad;
    };

    self->video_pad = GST_AGGREGATOR_PAD(add_sink("sink_video"));
    self->lidar_pad = GST_AGGREGATOR_PAD(add_sink("sink_lidar"));
    self->calib_pad = GST_AGGREGATOR_PAD(add_sink("sink_calib"));
}

static GstFlowReturn gst_fusion_aggregate(GstAggregator *aggregator, gboolean /*timeout*/) {
    GstFusion *self = GST_FUSION(aggregator);

    const gboolean eos_video = gst_aggregator_pad_is_eos(self->video_pad);
    const gboolean eos_lidar = gst_aggregator_pad_is_eos(self->lidar_pad);
    const gboolean eos_calib = gst_aggregator_pad_is_eos(self->calib_pad);

    // Peek first; do not consume unless all three are ready.
    GstBuffer *video_peek = gst_aggregator_pad_peek_buffer(self->video_pad);
    GstBuffer *lidar_peek = gst_aggregator_pad_peek_buffer(self->lidar_pad);
    GstBuffer *calib_peek = gst_aggregator_pad_peek_buffer(self->calib_pad);

    if (!video_peek || !lidar_peek || !calib_peek) {
        // If any pad has already hit EOS and has no pending buffer, we cannot
        // produce further fused outputs; exit cleanly to avoid spinning.
        if ((eos_video && !video_peek) || (eos_lidar && !lidar_peek) || (eos_calib && !calib_peek)) {
            GST_INFO_OBJECT(self,
                "ending because pad EOS without buffer (v:%d l:%d c:%d)",
                eos_video, eos_lidar, eos_calib);
            return GST_FLOW_EOS;
        }

        gboolean all_eos = eos_video && eos_lidar && eos_calib;

        GST_LOG_OBJECT(self,
            "waiting video=%s lidar=%s calib=%s eos=(v:%d l:%d c:%d)",
            video_peek ? "yes" : "no",
            lidar_peek ? "yes" : "no",
            calib_peek ? "yes" : "no",
            gst_aggregator_pad_is_eos(self->video_pad),
            gst_aggregator_pad_is_eos(self->lidar_pad),
            gst_aggregator_pad_is_eos(self->calib_pad));

        return all_eos ? GST_FLOW_EOS : GST_FLOW_OK;
    }

    // Now consume one buffer from each pad.
    GstBuffer *video = gst_aggregator_pad_pop_buffer(self->video_pad);
    GstBuffer *lidar = gst_aggregator_pad_pop_buffer(self->lidar_pad);
    GstBuffer *calib = gst_aggregator_pad_pop_buffer(self->calib_pad);

    GstClockTime pts_v = GST_BUFFER_PTS_IS_VALID(video) ? GST_BUFFER_PTS(video) : GST_CLOCK_TIME_NONE;
    GstClockTime pts_r = GST_BUFFER_PTS_IS_VALID(lidar) ? GST_BUFFER_PTS(lidar) : GST_CLOCK_TIME_NONE;
    GstClockTime pts_c = GST_BUFFER_PTS_IS_VALID(calib) ? GST_BUFFER_PTS(calib) : GST_CLOCK_TIME_NONE;

    GstClockTime out_pts = GST_CLOCK_TIME_NONE;
    if (pts_v != GST_CLOCK_TIME_NONE || pts_r != GST_CLOCK_TIME_NONE || pts_c != GST_CLOCK_TIME_NONE) {
        out_pts = MAX(pts_v, MAX(pts_r, pts_c));
    }

    GST_INFO_OBJECT(self, "fused seq=%" G_GUINT64_FORMAT " pts_video=%" GST_TIME_FORMAT " pts_lidar=%" GST_TIME_FORMAT " pts_calib=%" GST_TIME_FORMAT,
                    self->sequence, GST_TIME_ARGS(pts_v), GST_TIME_ARGS(pts_r), GST_TIME_ARGS(pts_c));
    self->sequence++;

    GstBuffer *outbuf = gst_buffer_new();
    GST_BUFFER_PTS(outbuf) = out_pts;
    GST_BUFFER_DTS(outbuf) = out_pts;
    GST_BUFFER_DURATION(outbuf) = GST_CLOCK_TIME_NONE;

    gst_buffer_unref(video);
    gst_buffer_unref(lidar);
    gst_buffer_unref(calib);

    return gst_aggregator_finish_buffer(aggregator, outbuf);
}

static gboolean fusion_plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(fusion_debug, "fusion", 0, "Fusion aggregator");
    return gst_element_register(plugin, "fusion", GST_RANK_NONE, GST_TYPE_FUSION);
}

#ifndef PACKAGE
#define PACKAGE "fusion"
#endif

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    fusion,
    "Three-way fusion aggregator",
    fusion_plugin_init,
    "1.0",
    "LGPL",
    "dlstreamer",
    "https://github.com/dlstreamer/dlstreamer"
)
