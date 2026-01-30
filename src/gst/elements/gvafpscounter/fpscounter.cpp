/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "fpscounter.h"

#include <assert.h>
#include <chrono>
#include <exception>
#include <mutex>
#include <stdexcept>
#ifdef __linux__
#include <unistd.h>
#endif
#include <cmath>
#include <numeric>

namespace {
constexpr double TIME_THRESHOLD = 0.1;
using seconds_double = std::chrono::duration<double>;
constexpr int ELEMENT_NAME_MAX_SIZE = 64;
constexpr double MICRO_TO_MILLI = 0.001;
constexpr double SECOND_TO_MILLI = 1000.0;
} // namespace

////////////////////////////////////////////////////////////////////////////////
// IterativeFpsCounter

bool IterativeFpsCounter::NewFrame(const std::string &element_name, FILE *output, GstBuffer *buffer) {
    std::lock_guard<std::mutex> lock(mutex);
    if (++total_frames <= starting_frame)
        return false;
    if (output == nullptr)
        return false;

    auto now = std::chrono::high_resolution_clock::now();
    if (print_std_dev) {
        double millis = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time).count() * MICRO_TO_MILLI;
        frame_intervals[element_name].push_back(millis);
    }
    if (print_latency) {
        GstVideoTimeCodeMeta *tc_meta = nullptr;
        if (buffer) {
            tc_meta = gst_buffer_get_video_time_code_meta(buffer);
        }
        if (tc_meta) {
            GstVideoTimeCode *vtc = gst_video_time_code_copy(&tc_meta->tc);
            GDateTime *frame_date_time = gst_video_time_code_to_date_time(vtc);
            double frame_date_time_millis = g_date_time_get_microsecond(frame_date_time) * MICRO_TO_MILLI;
            frame_date_time_millis += g_date_time_to_unix(frame_date_time) * SECOND_TO_MILLI;
            double now_millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            latencies[element_name].push_back(now_millis - frame_date_time_millis);
        } else {
            print_latency = false;
        }
    }

    if (!init_time.time_since_epoch().count()) {
        init_time = last_time = now;
    }

    // reset average counter everytime a new stream is detected
    if (average && (num_frames.find(element_name) == num_frames.end())) {
        init_time = last_time = now;
        for (auto it = num_frames.begin(); it != num_frames.end(); it++)
            it->second = 0;
    }

    num_frames[element_name]++;

    double sec = std::chrono::duration_cast<seconds_double>(now - last_time).count();
    if (sec >= interval) {
        last_time = now;
        if (average) {
            sec = std::chrono::duration_cast<seconds_double>(now - init_time).count();
            PrintFPS(output, sec);
        } else {
            PrintFPS(output, sec);
            for (auto it = num_frames.begin(); it != num_frames.end(); it++)
                it->second = 0;
        }
        return true;
    }
    return false;
}

double IterativeFpsCounter::calculate_std_dev(std::vector<double> &v) {
    // create vector for differences between adjacent elements of v
    std::vector<double> diffs;
    double prev_val = *(v.begin());
    for (auto it = v.begin() + 1; it != v.end(); ++it) {
        if (*it - prev_val < 0) {
            // in case of concatenated input vector, during measuring total vector
            continue;
        }
        diffs.push_back(*it - prev_val);
        prev_val = *it;
    }
    auto sum = std::accumulate(diffs.begin(), diffs.end(), 0.0);
    auto diffs_size = diffs.size();
    if (diffs_size == 0)
        return 0.0;
    auto mean = sum / diffs_size;
    // computes squares sum needed to calculate standard deviation
    auto sq_sum = 0.0;
    for (auto &el : diffs) {
        sq_sum += (el - mean) * (el - mean);
    }
    double std_dev = diffs_size < 2 ? 0.0 : std::sqrt(sq_sum / (diffs_size - 1));
    // in case of very big number at the beginning of calculation return 0.0
    if (std_dev > 1000.0)
        return 0.0;
    return std_dev;
}

double IterativeFpsCounter::calculate_latency(std::vector<double> &v) {
    auto v_size = v.size();
    if (v_size == 0)
        return 0.0;
    auto sum = std::accumulate(v.begin(), v.end(), 0.0);
    return (sum / v_size);
}

void IterativeFpsCounter::PrintFPS(FILE *output, double sec, bool eos) {
    assert(output);

    if (sec < TIME_THRESHOLD) {
        fprintf(output, "FPSCounter: Not enough data for calculation. The time interval (%.7f sec) is too short.\n",
                sec);
        return;
    }
    if (num_frames.empty())
        return;

    double total = 0;
    for (const auto &num : num_frames)
        total += num.second;
    total /= sec;

    if (average) {

        if (num_frames.size() == 1) {
            // avg fps for only one stream
            avg_fps = total;
        } else {
            // avg fps for multiple streams
            avg_fps = total / num_frames.size();
        }

        if (eos)
            fprintf(output, "FpsCounter(overall %.2fsec): ", sec);
        else
            fprintf(output, "FpsCounter(average %.2fsec): ", sec);
    } else {
        fprintf(output, "FpsCounter(last %.2fsec): ", sec);
    }
    fprintf(output, "total=%.2f fps, number-streams=%ld, per-stream=%.2f fps", total, num_frames.size(),
            total / num_frames.size());
    if (num_frames.size() > 1 && print_each_stream) {
        fprintf(output, " (");
        auto num = num_frames.begin();
        fprintf(output, "%.2f", num->second / sec);
        for (++num; num != num_frames.end(); ++num)
            fprintf(output, ", %.2f", num->second / sec);
        fprintf(output, ")");
    }
    if (print_std_dev) {
        for (auto &num_frame : num_frames) {
            total_frame_intervals.insert(total_frame_intervals.end(), frame_intervals[num_frame.first].begin(),
                                         frame_intervals[num_frame.first].end());
        }
        fprintf(output, "\nstd dev interval: %.2fms", calculate_std_dev(total_frame_intervals));
        // clear vector for the next iteration of displaying standard deviation
        total_frame_intervals.clear();
        if (num_frames.size() > 1 && print_each_stream) {
            fprintf(output, " (");
            auto num = num_frames.begin();
            fprintf(output, "%.2f", calculate_std_dev(frame_intervals[num->first]));
            for (++num; num != num_frames.end(); ++num) {
                fprintf(output, ", %.2f", calculate_std_dev(frame_intervals[num->first]));
            }
            fprintf(output, ")");
        }
        for (auto &num_frame : num_frames) {
            // clear vector for the next iteration of displaying standard deviation
            frame_intervals[num_frame.first].clear();
        }
    }
    if (print_latency) {
        for (auto &num_frame : num_frames) {
            total_latencies.insert(total_latencies.end(), latencies[num_frame.first].begin(),
                                   latencies[num_frame.first].end());
        }
        fprintf(output, "\nlatency: %.2fms", calculate_latency(total_latencies));
        // clear vector for the next iteration of displaying standard deviation
        total_latencies.clear();
        if (num_frames.size() > 1 && print_each_stream) {
            fprintf(output, " (");
            auto num = num_frames.begin();
            fprintf(output, "%.2f", calculate_latency(latencies[num->first]));
            for (++num; num != num_frames.end(); ++num) {
                fprintf(output, ", %.2f", calculate_latency(latencies[num->first]));
            }
            fprintf(output, ")");
        }
        for (auto &num_frame : num_frames) {
            // clear vector for the next iteration of displaying standard deviation
            latencies[num_frame.first].clear();
        }
    }
    fprintf(output, "\n");
    fflush(output);
}

void IterativeFpsCounter::EOS(const std::string &element_name, FILE *output) {
    assert(output);
    std::lock_guard<std::mutex> lock(mutex);
    if (!eos_result_reported) {
        auto now = std::chrono::high_resolution_clock::now();
        auto last = average ? init_time : last_time;
        double sec = std::chrono::duration_cast<seconds_double>(now - last).count();
        PrintFPS(output, sec, true);
        eos_result_reported = true;
    }

    // remove stream from counter list
    if (num_frames.find(element_name) != num_frames.end()) {
        num_frames.erase(element_name);
        // reset counter if there are still active streams
        if (num_frames.size() > 0) {
            init_time = last_time = std::chrono::high_resolution_clock::now();
            for (auto it = num_frames.begin(); it != num_frames.end(); it++)
                it->second = 0;
            eos_result_reported = false;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// WritePipeFpsCounter

static int getProcessId() {
#ifdef __linux__
    return getpid();
#elif _WIN32
    assert(!"getpid() is not implemented for Windows.");
    return -1;
#endif
}

WritePipeFpsCounter::WritePipeFpsCounter(const char *pipe_name)
    : pipe(std::make_unique<NamedPipe>(std::string(pipe_name), NamedPipe::Mode::WriteOnly)),
      pid(std::to_string(getProcessId())) {
}

bool WritePipeFpsCounter::NewFrame(const std::string &element_name, FILE *, GstBuffer *buffer) {
    (void)buffer;
    std::string name = element_name + "_" + pid;
    assert(name.size() < ELEMENT_NAME_MAX_SIZE && "WritePipe message length exceeded");
    name.resize(ELEMENT_NAME_MAX_SIZE);

    auto count_bytes = pipe->write(name.c_str(), ELEMENT_NAME_MAX_SIZE);

    if (count_bytes == -1)
        throw std::runtime_error("Error writing to FIFO file " + name);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// ReadPipeFpsCounter

ReadPipeFpsCounter::ReadPipeFpsCounter(const char *pipe_name, std::function<void(const char *)> new_message,
                                       std::function<void(void)> pipe_completed)
    : pipe(std::make_unique<NamedPipe>(std::string(pipe_name), NamedPipe::Mode::ReadOnly)),
      new_message_callback(new_message), pipe_completion_callback(pipe_completed) {
    thread = std::thread([&] {
        try {
            char name[ELEMENT_NAME_MAX_SIZE];
            while (true) {
                int nbytes = pipe->read(name, ELEMENT_NAME_MAX_SIZE);
#ifndef _WIN32
                // If there's not enough data - wait for 10 ms
                if (nbytes < ELEMENT_NAME_MAX_SIZE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    // Since we're waiting and have nothing to do,
                    // let's find out how many pipe descriptors are open.
                    // If there're zero, our work here is done.
                    if (getOpenedByProcessesDescriptorsCount(pipe->getName(), "w") == 0)
                        break;
                    continue;
                }
#endif
                new_message_callback(name);
            }
            pipe_completion_callback();
        } catch (const std::exception &e) {
            printf("ReadPipe error: %s", e.what());
        }
    });
}

ReadPipeFpsCounter::~ReadPipeFpsCounter() {
    try {
        if (thread.joinable()) {
            thread.join();
        }
    } catch (const std::exception &e) {
        printf("An error occurred while destructing ReadPipe: %s", e.what());
    }
}
