/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "video_clip_writer.hpp"

#include <algorithm>
#include <chrono>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(gst_gvaeventrecorder_debug);
#define GST_CAT_DEFAULT gst_gvaeventrecorder_debug

namespace gvaeventrecorder {

namespace {

// AUTO prefers the hardware VA-API encoder and falls back to software when unavailable.
std::string pick_encoder(EncoderMode mode) {
    auto factory_available = [](const char *name) {
        GstElementFactory *f = gst_element_factory_find(name);
        if (f)
            gst_object_unref(f);
        return f != nullptr;
    };

    // x264enc lives in gst-plugins-ugly and is often not installed; openh264enc is the fallback.
    const std::string software =
        factory_available("x264enc") ? "x264enc tune=zerolatency speed-preset=ultrafast" : "openh264enc";

    switch (mode) {
    case EncoderMode::VaApi:
        return "vah264enc";
    case EncoderMode::Auto:
        return factory_available("vah264enc") ? "vah264enc" : software;
    case EncoderMode::Software:
    default:
        return software;
    }
}

void unref_all(std::vector<GstBuffer *> &frames) {
    for (GstBuffer *b : frames)
        gst_buffer_unref(b);
    frames.clear();
}

std::string parse_error_message(GstMessage *msg) {
    GError *err = nullptr;
    gchar *dbg = nullptr;
    gst_message_parse_error(msg, &err, &dbg);
    std::string text = err ? err->message : "unknown pipeline error";
    if (err)
        g_error_free(err);
    g_free(dbg);
    return text;
}

constexpr auto kClosingPollPeriod = std::chrono::milliseconds(20);

} // namespace

struct VideoClipWriter::Clip {
    std::string output_path;
    GstElement *pipeline = nullptr;
    GstElement *appsrc = nullptr;
    GstBus *bus = nullptr;
    guint64 frame_duration = 0;
    guint64 next_pts = 0;
    guint64 flush_interval_ns = 0; // 0 = not fragmented
    guint64 fragment_start_pts = 0;
    guint64 frames_written = 0;
    bool failed = false;

    ~Clip() {
        teardown();
    }

    void teardown() {
        if (pipeline)
            gst_element_set_state(pipeline, GST_STATE_NULL);
        if (bus)
            gst_object_unref(bus);
        if (appsrc)
            gst_object_unref(appsrc);
        if (pipeline)
            gst_object_unref(pipeline);
        bus = nullptr;
        appsrc = nullptr;
        pipeline = nullptr;
    }

    void fail(const std::string &reason) {
        GST_WARNING("Failed to write event recording '%s': %s", output_path.c_str(), reason.c_str());
        failed = true;
        teardown();
    }
};

VideoClipWriter::VideoClipWriter() = default;

VideoClipWriter::~VideoClipWriter() {
    stop();
    while (!queue_.empty()) {
        unref_all(queue_.front().frames);
        queue_.pop();
    }
}

void VideoClipWriter::start() {
    if (running_)
        return;
    stop_requested_ = false;
    running_ = true;
    thread_ = std::thread(&VideoClipWriter::worker_main, this);
}

void VideoClipWriter::stop() {
    if (!running_)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    running_ = false;
}

VideoClipWriter::ClipId VideoClipWriter::open(ClipConfig config) {
    Command cmd;
    cmd.type = Command::Type::Open;
    cmd.config = std::move(config);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cmd.id = next_id_++;
    }
    const ClipId id = cmd.id;
    push_command(std::move(cmd));
    return id;
}

void VideoClipWriter::append(ClipId id, std::vector<GstBuffer *> frames, bool start_new_fragment) {
    if (frames.empty())
        return;
    Command cmd;
    cmd.type = Command::Type::Append;
    cmd.id = id;
    cmd.frames = std::move(frames);
    cmd.start_new_fragment = start_new_fragment;
    push_command(std::move(cmd));
}

void VideoClipWriter::close(ClipId id) {
    Command cmd;
    cmd.type = Command::Type::Close;
    cmd.id = id;
    push_command(std::move(cmd));
}

void VideoClipWriter::push_command(Command cmd) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(cmd));
    }
    cv_.notify_one();
}

void VideoClipWriter::worker_main() {
    for (;;) {
        Command cmd;
        bool have_cmd = false;
        bool draining = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto ready = [this] { return stop_requested_ || !queue_.empty(); };
            if (closing_.empty())
                cv_.wait(lock, ready);
            else
                cv_.wait_for(lock, kClosingPollPeriod, ready);
            if (!queue_.empty()) {
                cmd = std::move(queue_.front());
                queue_.pop();
                have_cmd = true;
            } else {
                draining = stop_requested_;
            }
        }

        if (have_cmd) {
            switch (cmd.type) {
            case Command::Type::Open:
                handle_open(cmd);
                break;
            case Command::Type::Append:
                handle_append(cmd);
                break;
            case Command::Type::Close:
                handle_close(cmd.id);
                break;
            }
        }

        if (draining) {
            while (!clips_.empty())
                handle_close(clips_.begin()->first);
            reap_closing(true);
            return;
        }
        reap_closing(false);
    }
}

void VideoClipWriter::handle_open(Command &cmd) {
    auto clip = std::make_unique<Clip>();
    Clip &c = *clip;
    c.output_path = cmd.config.output_path;
    clips_[cmd.id] = std::move(clip);

    const GstVideoInfo &info = cmd.config.video_info;
    c.frame_duration = (info.fps_n > 0) ? gst_util_uint64_scale(GST_SECOND, info.fps_d, info.fps_n) : GST_SECOND / 30;

    std::string mux = "mp4mux";
    if (cmd.config.flush_interval_ms > 0) {
        c.flush_interval_ns = static_cast<guint64>(cmd.config.flush_interval_ms) * GST_MSECOND;
        // Fragment boundaries are driven by keyframes forced every flush interval (mp4mux closes a
        // fragment on each keyframe); the muxer's own duration limit is only a fallback.
        const guint64 fallback_ms = std::min<guint64>(2ull * cmd.config.flush_interval_ms, G_MAXUINT);
        mux += " fragment-duration=" + std::to_string(fallback_ms);
    }

    // Unbounded, non-blocking appsrc: the queued frames are already referenced by the caller,
    // and the worker must never block on a stalled or failed pipeline.
    const std::string desc = "appsrc name=src format=time is-live=false block=false max-bytes=0 ! videoconvert ! " +
                             pick_encoder(cmd.config.encoder_mode) + " ! h264parse ! " + mux +
                             " ! filesink name=sink buffer-mode=unbuffered";

    GError *gerr = nullptr;
    c.pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (!c.pipeline || gerr) {
        const std::string reason = gerr ? gerr->message : "gst_parse_launch failed";
        if (gerr)
            g_error_free(gerr);
        c.fail(reason);
        return;
    }

    c.appsrc = gst_bin_get_by_name(GST_BIN(c.pipeline), "src");
    c.bus = gst_element_get_bus(c.pipeline);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(c.pipeline), "sink");
    g_object_set(sink, "location", c.output_path.c_str(), NULL);
    gst_object_unref(sink);

    GstCaps *caps = gst_video_info_to_caps(&info);
    g_object_set(c.appsrc, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (gst_element_set_state(c.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        c.fail("failed to start internal recording pipeline");
        return;
    }
    GST_DEBUG("Opened event recording '%s' (flush interval %u ms)", c.output_path.c_str(),
              cmd.config.flush_interval_ms);
}

void VideoClipWriter::check_bus_errors(Clip &clip) {
    if (!clip.bus)
        return;
    if (GstMessage *msg = gst_bus_pop_filtered(clip.bus, GST_MESSAGE_ERROR)) {
        const std::string reason = parse_error_message(msg);
        gst_message_unref(msg);
        clip.fail(reason);
    }
}

void VideoClipWriter::handle_append(Command &cmd) {
    auto it = clips_.find(cmd.id);
    if (it == clips_.end()) {
        unref_all(cmd.frames);
        return;
    }
    Clip &c = *it->second;
    check_bus_errors(c);
    if (c.failed) {
        unref_all(cmd.frames);
        return;
    }

    bool first = true;
    for (GstBuffer *buf : cmd.frames) {
        if (c.failed) {
            gst_buffer_unref(buf);
            continue;
        }
        const guint64 pts = c.next_pts;
        c.next_pts += c.frame_duration;

        if (c.flush_interval_ns > 0 && c.frames_written > 0 &&
            ((first && cmd.start_new_fragment) || pts - c.fragment_start_pts >= c.flush_interval_ns)) {
            // Serialized with the buffers queued in appsrc, so it applies exactly to this frame.
            gst_element_send_event(c.appsrc, gst_video_event_new_downstream_force_key_unit(
                                                 GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, TRUE,
                                                 static_cast<guint>(c.frames_written)));
            c.fragment_start_pts = pts;
            GST_DEBUG("Event recording '%s': new fragment at frame %" G_GUINT64_FORMAT, c.output_path.c_str(),
                      c.frames_written);
        }
        first = false;

        // Ownership of buf transfers to push_buffer(); synthesize a monotonic PTS/DURATION
        // since the original stream PTS is not guaranteed contiguous for this clip.
        buf = gst_buffer_make_writable(buf);
        GST_BUFFER_PTS(buf) = pts;
        GST_BUFFER_DTS(buf) = pts;
        GST_BUFFER_DURATION(buf) = c.frame_duration;
        if (gst_app_src_push_buffer(GST_APP_SRC(c.appsrc), buf) != GST_FLOW_OK)
            c.fail("internal recording pipeline refused a frame");
        else
            ++c.frames_written;
    }
}

void VideoClipWriter::handle_close(ClipId id) {
    auto it = clips_.find(id);
    if (it == clips_.end())
        return;
    std::unique_ptr<Clip> clip = std::move(it->second);
    clips_.erase(it);

    check_bus_errors(*clip);
    if (clip->failed)
        return;
    if (clip->frames_written == 0) {
        clip->fail("no frames to write");
        return;
    }
    gst_app_src_end_of_stream(GST_APP_SRC(clip->appsrc));
    closing_.push_back(std::move(clip));
}

// Finalizes clips whose pipelines reached EOS (or failed); with wait=true blocks until all have.
void VideoClipWriter::reap_closing(bool wait) {
    for (auto it = closing_.begin(); it != closing_.end();) {
        Clip &c = **it;
        GstMessage *msg = gst_bus_timed_pop_filtered(c.bus, wait ? GST_CLOCK_TIME_NONE : 0,
                                                     static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (!msg) {
            ++it;
            continue;
        }
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
            c.fail(parse_error_message(msg));
        else
            GST_DEBUG("Finished event recording '%s' (%" G_GUINT64_FORMAT " frames)", c.output_path.c_str(),
                      c.frames_written);
        gst_message_unref(msg);
        it = closing_.erase(it);
    }
}

} // namespace gvaeventrecorder
