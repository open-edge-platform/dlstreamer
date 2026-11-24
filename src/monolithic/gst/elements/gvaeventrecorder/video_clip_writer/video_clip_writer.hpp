/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <gst/video/video.h>

namespace gvaeventrecorder {

enum class EncoderMode { Software, VaApi, Auto };

struct ClipConfig {
    std::string output_path;
    GstVideoInfo video_info{};
    EncoderMode encoder_mode = EncoderMode::Software;
    // > 0: fragmented MP4 whose fragments reach the disk at least every flush_interval_ms of
    // stream time while the clip is still being recorded. 0: regular MP4 finalized on close().
    guint flush_interval_ms = 0;
};

// Encodes and muxes event clips to disk off the streaming thread. Each open clip owns a
// persistent appsrc ! videoconvert ! encoder ! h264parse ! mp4mux ! filesink pipeline that
// is fed incrementally via append() and finalized by close(). All commands are executed in
// order by a single background worker, so the caller never blocks on pipeline setup/teardown.
class VideoClipWriter {
  public:
    using ClipId = uint64_t;

    VideoClipWriter();
    ~VideoClipWriter();

    VideoClipWriter(const VideoClipWriter &) = delete;
    VideoClipWriter &operator=(const VideoClipWriter &) = delete;

    void start();
    // Executes all queued commands and finalizes every clip (including ones never closed)
    // before joining the worker thread, so no recorded frame is silently discarded.
    void stop();

    ClipId open(ClipConfig config);
    // Takes ownership of the buffer refs. With start_new_fragment the first frame is encoded
    // as a keyframe, which flushes all previously appended frames to disk (fragmented clips only).
    void append(ClipId id, std::vector<GstBuffer *> frames, bool start_new_fragment = false);
    void close(ClipId id);

  private:
    struct Clip;
    struct Command {
        enum class Type { Open, Append, Close } type = Type::Open;
        ClipId id = 0;
        ClipConfig config;
        std::vector<GstBuffer *> frames;
        bool start_new_fragment = false;
    };

    void push_command(Command cmd);
    void worker_main();
    void handle_open(Command &cmd);
    void handle_append(Command &cmd);
    void handle_close(ClipId id);
    void reap_closing(bool wait);
    void check_bus_errors(Clip &clip);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Command> queue_;
    ClipId next_id_ = 0;
    bool running_ = false;
    bool stop_requested_ = false;

    // Worker-thread only.
    std::map<ClipId, std::unique_ptr<Clip>> clips_;
    std::vector<std::unique_ptr<Clip>> closing_;
};

} // namespace gvaeventrecorder
