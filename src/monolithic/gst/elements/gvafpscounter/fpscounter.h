/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "named_pipe.h"

#include <chrono>
#include <functional>
#include <gst/video/video.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class FpsCounter {
  public:
    virtual ~FpsCounter() = default;
    virtual bool NewFrame(const std::string &element_name, FILE *output, GstBuffer *buffer) = 0;
    virtual void EOS(const std::string &element_name, FILE *output) = 0;
};

class IterativeFpsCounter : public FpsCounter {
  public:
    IterativeFpsCounter(unsigned starting_frame, unsigned interval, bool average, bool print_std_dev,
                        bool print_latency)
        : starting_frame(starting_frame), interval(interval), average(average), print_each_stream(true),
          total_frames(0), detections(0), avg_fps(0.0), eos_result_reported(false), print_std_dev(print_std_dev),
          print_latency(print_latency) {
    }
    bool NewFrame(const std::string &element_name, FILE *output, GstBuffer *buffer) override;
    void EOS(const std::string &element_name, FILE *) override;
    double calculate_std_dev(std::vector<double> &v);
    double calculate_latency(std::vector<double> &v);
    float get_avg_fps() {
        std::lock_guard<std::mutex> lock(mutex);
        return avg_fps;
    }
    unsigned get_detections() {
        std::lock_guard<std::mutex> lock(mutex);
        return detections;
    }

  protected:
    unsigned starting_frame;
    unsigned interval;
    bool average;
    bool print_each_stream;
    unsigned total_frames;
    unsigned detections;
    float avg_fps;
    std::chrono::time_point<std::chrono::high_resolution_clock> init_time;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_time;
    std::map<std::string, int> num_frames;
    std::map<std::string, std::vector<double>> frame_intervals;
    std::map<std::string, std::vector<double>> latencies;
    std::vector<double> total_frame_intervals;
    std::vector<double> total_latencies;
    std::mutex mutex;
    bool eos_result_reported;
    bool print_std_dev;
    bool print_latency;

    void PrintFPS(FILE *output, double sec, bool eos = false);
};

class WritePipeFpsCounter : public FpsCounter {
  public:
    WritePipeFpsCounter(const char *pipe_name);
    bool NewFrame(const std::string &element_name, FILE *output, GstBuffer *buffer) override;
    void EOS(const std::string &, FILE *) override {
    }

  protected:
    std::unique_ptr<NamedPipe> pipe;
    std::string pid;
};

class ReadPipeFpsCounter : public FpsCounter {
  public:
    ReadPipeFpsCounter(const char *pipe_name, std::function<void(const char *)> new_message,
                       std::function<void(void)> pipe_completed);
    ~ReadPipeFpsCounter();
    bool NewFrame(const std::string &, FILE *, GstBuffer *buffer) override {
        (void)buffer;
        return true;
    }
    void EOS(const std::string &, FILE *) override {
    }

  protected:
    std::unique_ptr<NamedPipe> pipe;
    std::thread thread;
    std::function<void(const char *)> new_message_callback;
    std::function<void(void)> pipe_completion_callback;
};
