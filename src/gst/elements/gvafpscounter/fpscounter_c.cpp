/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "fpscounter_c.h"
#include "config.h"
#include "fpscounter.h"
#include "gstgvafpscounter.h"
#include "inference_backend/logger.h"
#include "utils.h"

#include <assert.h>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <regex>

static std::map<std::string, std::shared_ptr<FpsCounter>> fps_counters;
static std::mutex channels_mutex;
static FILE *output = stdout;
//////////////////////////////////////////////////////////////////////////
// C interface
void fps_counter_create_iterative(const char *intervals, bool print_std_dev, bool print_latency) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        std::vector<std::string> intervals_list = Utils::splitString(intervals, ',');
        for (const std::string &interval : intervals_list)
            if (not fps_counters.count(interval)) {
                std::shared_ptr<FpsCounter> fps_counter = nullptr;
                fps_counter =
                    std::make_shared<IterativeFpsCounter>(0, std::stoi(interval), false, print_std_dev, print_latency);
                fps_counters.insert({interval, fps_counter});
            }
    } catch (std::exception &e) {
        GVA_ERROR("Error during creation iterative fpscounter: %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_create_average(unsigned int starting_frame, unsigned int interval) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        if (not fps_counters.count("average")) {
            fps_counters.insert(
                {"average", std::make_shared<IterativeFpsCounter>(starting_frame, interval, true, false, false)});
        }
    } catch (std::exception &e) {
        GVA_ERROR("Error during creation average fpscounter: %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_create_writepipe(const char *pipe_name) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        if (not fps_counters.count("writepipe")) {
            fps_counters.insert({"writepipe", std::make_shared<WritePipeFpsCounter>(pipe_name)});
        }
    } catch (std::exception &e) {
        GVA_ERROR("Error during creation writepipe fpscounter: %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_create_readpipe(void *fpscounter, const char *pipe_name) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        if (not fps_counters.count("readpipe")) {
            auto pipe_complete_lambda = [=]() {
                fps_counter_eos(GST_ELEMENT_NAME(GST_BASE_TRANSFORM(fpscounter)));
                // Pushing an EOS event downstream to signal that we're done.
                bool handled = gst_pad_push_event(GST_BASE_TRANSFORM(fpscounter)->srcpad, gst_event_new_eos());
                if (!handled)
                    throw std::runtime_error("FpsCounter ReadPipe: EOS event wasn't handled. Spinning...");
            };
            auto new_message_lambda = [](const char *name) { fps_counter_new_frame(NULL, name, NULL); };
            fps_counters.insert({"readpipe", std::make_shared<ReadPipeFpsCounter>(pipe_name, new_message_lambda,
                                                                                  pipe_complete_lambda)});
        }
    } catch (std::exception &e) {
        GVA_ERROR("Error during creation readpipe fpscounter: %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_new_frame(GstBuffer *buffer, const char *element_name, void *gstgvafpscounter) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        for (auto counter = fps_counters.begin(); counter != fps_counters.end(); ++counter) {
            counter->second->NewFrame(element_name, output, buffer);
            if (gstgvafpscounter && counter->first == "average") {
                auto iterative_counter = std::dynamic_pointer_cast<IterativeFpsCounter>(counter->second);
                if (iterative_counter) {
                    ((GstGvaFpscounter *)gstgvafpscounter)->avg_fps = iterative_counter->get_avg_fps();
                    ((GstGvaFpscounter *)gstgvafpscounter)->detections = iterative_counter->get_detections();
                }
            }
        }
    } catch (std::exception &e) {
        GVA_ERROR("Error during adding new frame: %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_eos(const char *element_name) {
    try {
        std::lock_guard<std::mutex> lock(channels_mutex);
        for (auto counter = fps_counters.begin(); counter != fps_counters.end(); ++counter)
            counter->second->EOS(element_name, output);
    } catch (std::exception &e) {
        GVA_ERROR("Error during handling EOS : %s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void fps_counter_set_output(FILE *out) {
    if (out != nullptr)
        output = out;
}

gboolean fps_counter_validate_intervals(const char *intervals_string) {
    if (!intervals_string)
        return false;

    try {
        std::regex re("^[0-9]{1,9}(,[0-9]{1,9})*$");
        return std::regex_match(std::string(intervals_string), re);
    } catch (const std::exception &e) {
        GVA_ERROR("Error during validation of intervals string: %s", Utils::createNestedErrorMsg(e).c_str());
    }

    return false;
}
