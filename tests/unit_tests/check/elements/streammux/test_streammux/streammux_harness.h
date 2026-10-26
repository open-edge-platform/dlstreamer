/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gtest/gtest.h>

/* A gvastreammux (optionally followed by a gvastreamdemux) fed by appsrc pads.
 *
 * appsrc rather than videotestsrc so every timestamp is exactly what the test
 * asks for: the batching rules are all about PTS, and a test that cannot state
 * the timestamps cannot state what it is checking. Nothing here sleeps for a
 * fixed period either -- the waits are on what is being observed, with a
 * deadline -- so the tests finish as soon as the behaviour has been shown.
 */
class StreammuxHarness {
  public:
    struct StreamState {
        guint pad_index;
        std::string state;
        guint active_pads;
        guint total_pads;
    };

    explicit StreammuxHarness(const std::string &output_mode = "passthrough", bool with_demux = false)
        : _with_demux(with_demux) {
        _pipeline = gst_pipeline_new(nullptr);
        _mux = gst_element_factory_make("gvastreammux", "mux");
        EXPECT_NE(_mux, nullptr) << "gvastreammux is not registered; check GST_PLUGIN_PATH";
        /* Both are enums, so they have to be set by nick: g_object_set would
         * read the string pointer as the enum value and reject it. */
        gst_util_set_object_arg(G_OBJECT(_mux), "sync-mode", "none");
        gst_util_set_object_arg(G_OBJECT(_mux), "output-mode", output_mode.c_str());
        g_object_set(_mux,
                     /* Short enough that a batch which has to time out does not
                      * dominate the test's runtime, long enough to be a timeout
                      * rather than a race. */
                     "max-wait-time", (guint64)5000000, nullptr);
        gst_bin_add(GST_BIN(_pipeline), _mux);

        if (_with_demux) {
            _demux = gst_element_factory_make("gvastreamdemux", "demux");
            EXPECT_NE(_demux, nullptr);
            gst_bin_add(GST_BIN(_pipeline), _demux);
            EXPECT_TRUE(gst_element_link(_mux, _demux));
        } else {
            _tail = gst_element_factory_make("fakesink", nullptr);
            g_object_set(_tail, "sync", FALSE, nullptr);
            gst_bin_add(GST_BIN(_pipeline), _tail);
            EXPECT_TRUE(gst_element_link(_mux, _tail));
            watchOutput(gst_element_get_static_pad(_mux, "src"), &_out);
        }
    }

    ~StreammuxHarness() {
        gst_element_set_state(_pipeline, GST_STATE_NULL);
        gst_object_unref(_pipeline);
    }

    /* Attach a source to sink_<index>. Caps are fixed across sources because
     * passthrough mode requires them to match. */
    void addSource(guint index) {
        GstElement *src = gst_element_factory_make("appsrc", nullptr);
        GstCaps *caps = gst_caps_from_string(kCaps);
        g_object_set(src, "format", GST_FORMAT_TIME, "caps", caps, "is-live", TRUE, nullptr);
        gst_caps_unref(caps);
        gst_bin_add(GST_BIN(_pipeline), src);

        gchar *name = g_strdup_printf("sink_%u", index);
        GstPad *sinkpad = gst_element_request_pad_simple(_mux, name);
        g_free(name);
        ASSERT_NE(sinkpad, nullptr);
        GstPad *srcpad = gst_element_get_static_pad(src, "src");
        ASSERT_EQ(gst_pad_link(srcpad, sinkpad), GST_PAD_LINK_OK);
        gst_object_unref(srcpad);

        gst_element_sync_state_with_parent(src);
        _sources[index] = {src, sinkpad};
    }

    /* Attach a demuxer branch for a source, counted separately. */
    void addBranch(guint index) {
        ASSERT_TRUE(_with_demux);
        gchar *name = g_strdup_printf("src_%u", index);
        GstPad *pad = gst_element_request_pad_simple(_demux, name);
        g_free(name);
        ASSERT_NE(pad, nullptr);

        /* Each branch needs its own queue: the demuxer pushes to all of them
         * from one thread, so without it they cannot preroll independently. */
        GstElement *queue = gst_element_factory_make("queue", nullptr);
        GstElement *sink = gst_element_factory_make("fakesink", nullptr);
        g_object_set(sink, "sync", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(_pipeline), queue, sink, nullptr);
        EXPECT_TRUE(gst_element_link(queue, sink));

        GstPad *qpad = gst_element_get_static_pad(queue, "sink");
        ASSERT_EQ(gst_pad_link(pad, qpad), GST_PAD_LINK_OK);
        gst_object_unref(qpad);

        gst_element_sync_state_with_parent(queue);
        gst_element_sync_state_with_parent(sink);

        {
            std::lock_guard<std::mutex> lock(_lock);
            _branch_counts[index] = 0;
        }
        watchOutput(gst_element_get_static_pad(queue, "src"), &_branch_counts[index]);
        _branches[index] = {pad, queue, sink};
    }

    /* Deliberately does not wait for the state change to finish. appsrc only
     * sends its caps on the first buffer, so until a test pushes one the
     * muxer cannot negotiate, nothing prerolls, and waiting here would just
     * burn the timeout. The tests wait on what they are actually checking. */
    void start() {
        _bus = gst_element_get_bus(_pipeline);
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    /* Push one buffer. flags lets a test mark a discontinuity. */
    void push(guint index, GstClockTime pts, GstBufferFlags flags = (GstBufferFlags)0) {
        auto it = _sources.find(index);
        ASSERT_NE(it, _sources.end());
        GstBuffer *buf = gst_buffer_new_allocate(nullptr, kFrameBytes, nullptr);
        GST_BUFFER_PTS(buf) = pts;
        GST_BUFFER_DURATION(buf) = kFrameDuration;
        if (flags)
            GST_BUFFER_FLAG_SET(buf, flags);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(it->second.element, "push-buffer", buf, &ret);
        gst_buffer_unref(buf);
        EXPECT_EQ(ret, GST_FLOW_OK);
    }

    void pushEos(guint index) {
        auto it = _sources.find(index);
        ASSERT_NE(it, _sources.end());
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(it->second.element, "end-of-stream", &ret);
    }

    void releaseSource(guint index) {
        auto it = _sources.find(index);
        ASSERT_NE(it, _sources.end());
        gst_element_release_request_pad(_mux, it->second.pad);
        gst_object_unref(it->second.pad);
        gst_element_set_state(it->second.element, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(_pipeline), it->second.element);
        _sources.erase(it);
    }

    /* The accessors below read what the pad probes write from streaming
     * threads -- one per demuxer branch -- so everything goes through _lock. */
    guint outputCount() const {
        std::lock_guard<std::mutex> lock(_lock);
        return _out;
    }
    guint branchCount(guint index) const {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _branch_counts.find(index);
        return it == _branch_counts.end() ? 0 : it->second;
    }
    guint srcEventCount(GstEventType type) const {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _events.find(type);
        return it == _events.end() ? 0 : it->second;
    }
    /* Whether a stream-start carrying the muxer's own id reached the source
     * pad, as opposed to one forwarded from a source. */
    bool sawOwnStreamStart() const {
        std::lock_guard<std::mutex> lock(_lock);
        return _own_stream_start;
    }
    GstClockTime lastBatchPts() const {
        std::lock_guard<std::mutex> lock(_lock);
        return _last_pts;
    }
    std::vector<GstClockTime> batchPtsSeen() const {
        std::lock_guard<std::mutex> lock(_lock);
        return _pts_seen;
    }

    /* Wait until a predicate holds, or give up. Returns whether it held, so a
     * test can assert on it rather than on having slept long enough. */
    bool waitFor(const std::function<bool()> &done, guint timeout_ms = 2000) {
        gint64 deadline = g_get_monotonic_time() + timeout_ms * 1000;
        while (!done()) {
            if (g_get_monotonic_time() > deadline)
                return false;
            g_usleep(1000);
        }
        return true;
    }

    /* Element messages this muxer posted about its sources, newest last. */
    std::vector<StreamState> drainStreamStates() {
        std::vector<StreamState> out;
        while (GstMessage *msg = gst_bus_timed_pop_filtered(_bus, 0, GST_MESSAGE_ELEMENT)) {
            const GstStructure *s = gst_message_get_structure(msg);
            if (s && gst_structure_has_name(s, "gva-streammux-stream-state")) {
                StreamState st{};
                gst_structure_get_uint(s, "pad-index", &st.pad_index);
                st.state = gst_structure_get_string(s, "state");
                gst_structure_get_uint(s, "active-pads", &st.active_pads);
                gst_structure_get_uint(s, "total-pads", &st.total_pads);
                out.push_back(st);
            }
            gst_message_unref(msg);
        }
        return out;
    }

    bool sawErrorOnBus() {
        GstMessage *msg = gst_bus_timed_pop_filtered(_bus, 0, GST_MESSAGE_ERROR);
        if (!msg)
            return false;
        gst_message_unref(msg);
        return true;
    }

    GstElement *mux() {
        return _mux;
    }
    GstElement *demux() {
        return _demux;
    }

    static constexpr const char *kCaps = "video/x-raw,format=RGB,width=16,height=16,framerate=30/1";
    static constexpr guint kFrameBytes = 16 * 16 * 3;
    static constexpr GstClockTime kFrameDuration = GST_SECOND / 30;

  private:
    struct Source {
        GstElement *element;
        GstPad *pad;
    };
    struct Branch {
        GstPad *pad;
        GstElement *queue;
        GstElement *sink;
    };

    void watchOutput(GstPad *pad, guint *counter) {
        struct Ctx {
            StreammuxHarness *self;
            guint *counter;
        };
        auto *ctx = new Ctx{this, counter};
        gst_pad_add_probe(
            pad, (GstPadProbeType)(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
            [](GstPad *, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                auto *c = static_cast<Ctx *>(user_data);
                std::lock_guard<std::mutex> lock(c->self->_lock);
                if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                    (*c->counter)++;
                    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
                    c->self->_last_pts = GST_BUFFER_PTS(b);
                    c->self->_pts_seen.push_back(GST_BUFFER_PTS(b));
                } else {
                    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT(info);
                    c->self->_events[GST_EVENT_TYPE(ev)]++;
                    if (GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START) {
                        const gchar *id = nullptr;
                        gst_event_parse_stream_start(ev, &id);
                        if (id && g_str_has_prefix(id, "gvastreammux/"))
                            c->self->_own_stream_start = true;
                    }
                }
                return GST_PAD_PROBE_OK;
            },
            ctx, [](gpointer d) { delete static_cast<Ctx *>(d); });
        gst_object_unref(pad);
    }

    bool _with_demux;
    GstElement *_pipeline = nullptr;
    GstElement *_mux = nullptr;
    GstElement *_demux = nullptr;
    GstElement *_tail = nullptr;
    GstBus *_bus = nullptr;

    mutable std::mutex _lock;
    std::map<guint, Source> _sources;
    std::map<guint, Branch> _branches;
    std::map<guint, guint> _branch_counts;
    std::map<GstEventType, guint> _events;

    guint _out = 0;
    bool _own_stream_start = false;
    GstClockTime _last_pts = GST_CLOCK_TIME_NONE;
    std::vector<GstClockTime> _pts_seen;
};
