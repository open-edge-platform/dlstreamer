/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <gst/video/video.h>

namespace gvaeventrecorder {

enum class EncoderMode { Software, VaApi, Auto };

// One finished recording session (pre + event + post frames) waiting to be encoded/muxed.
struct DumpJob {
    std::string output_path;
    std::vector<GstBuffer *> frames; // owning refs; consumed (pushed into appsrc) by the writer
    std::vector<std::string> frame_annotations;
    GstVideoInfo video_info{};
    EncoderMode encoder_mode = EncoderMode::Software;
};

// Encodes and muxes finished sessions to disk off the streaming thread, via an internal
// appsrc ! videoconvert ! encoder ! muxer ! filesink mini-pipeline per job. A single
// background worker processes jobs strictly sequentially, so encode/disk load is bounded
// by how fast sessions finish (itself bounded upstream by max-concurrent-events) rather
// than by one thread per session.
class VideoDumpWriter {
  public:
    VideoDumpWriter();
    ~VideoDumpWriter();

    VideoDumpWriter(const VideoDumpWriter &) = delete;
    VideoDumpWriter &operator=(const VideoDumpWriter &) = delete;

    void start();
    // Drains all queued jobs before joining the worker thread (so stop() never silently
    // discards a finished session, matching the "finish and flush on EOS/stop" requirement).
    void stop();

    void enqueue(DumpJob job);

  private:
    void worker_main();
    bool write_job(const DumpJob &job, std::string &error_out);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<DumpJob> queue_;
    bool running_ = false;
    bool stop_requested_ = false;
};

} // namespace gvaeventrecorder
