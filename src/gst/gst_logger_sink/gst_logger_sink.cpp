/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gst_logger_sink.h"

#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

#include <cstdlib>
#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <iostream>

#define IGNORE_EXTRA_SINK_LOGGER

namespace dlstreamer::log {

spdlog::level::level_enum to_sdp_level(GstDebugLevel gst_level) {
    switch (gst_level) {
    case GstDebugLevel::GST_LEVEL_MEMDUMP:
    case GstDebugLevel::GST_LEVEL_TRACE:
        return spdlog::level::level_enum::trace;
    case GstDebugLevel::GST_LEVEL_LOG:
    case GstDebugLevel::GST_LEVEL_DEBUG:
        return spdlog::level::level_enum::debug;
    case GstDebugLevel::GST_LEVEL_INFO:
        return spdlog::level::level_enum::info;
    case GstDebugLevel::GST_LEVEL_FIXME:
    case GstDebugLevel::GST_LEVEL_WARNING:
        return spdlog::level::level_enum::warn;
    case GstDebugLevel::GST_LEVEL_ERROR:
        return spdlog::level::level_enum::err;
    case GstDebugLevel::GST_LEVEL_NONE:
        return spdlog::level::level_enum::off;
    }
    return spdlog::level::level_enum::off;
}

GstDebugLevel to_GstDebugLevel(spdlog::level::level_enum spd_level) {
    switch (spd_level) {
    case SPDLOG_LEVEL_TRACE:
        return GstDebugLevel::GST_LEVEL_TRACE;
    case SPDLOG_LEVEL_DEBUG:
        return GstDebugLevel::GST_LEVEL_DEBUG;
    case SPDLOG_LEVEL_INFO:
        return GstDebugLevel::GST_LEVEL_INFO;
    case SPDLOG_LEVEL_WARN:
        return GstDebugLevel::GST_LEVEL_WARNING;
    case SPDLOG_LEVEL_ERROR:
    case SPDLOG_LEVEL_CRITICAL:
        return GstDebugLevel::GST_LEVEL_ERROR;
    case SPDLOG_LEVEL_OFF:
        return GstDebugLevel::GST_LEVEL_NONE;
    }
    return GstDebugLevel::GST_LEVEL_NONE;
}

class gst_sink : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
  public:
    gst_sink(GstDebugCategory *cat, GObject *object) : _category(cat), _object(object) {
        // set simple pattern for spdlog, gstreamer pattern is actually used
        set_pattern("%v");
    }

  private:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        GstDebugLevel level = to_GstDebugLevel(msg.level);
        if (level > GST_LEVEL_MAX || level > gst_debug_category_get_threshold(_category))
            return;
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        const char *filename = msg.source.filename ? msg.source.filename : "";
        const char *funcname = msg.source.funcname ? msg.source.funcname : "";
        // use string format with length, remove trailing new line symbol if present.
        size_t length = formatted.size();
        if (length > 0 && formatted.data()[length - 1] == '\n')
            --length;
        gst_debug_log(_category, level, filename, funcname, msg.source.line, _object, "%.*s", static_cast<int>(length),
                      formatted.data());
    }

    void flush_() override {
    }

  private:
    GstDebugCategory *_category;
    GObject *_object;
};

spdlog::sink_ptr create_gst_sink_instance(GstDebugCategory *category, GObject *object) {
    return std::make_shared<gst_sink>(category, object);
}

spdlog::sink_ptr create_sink() {
    auto custom_sink = std::make_shared<spdlog::sinks::stdout_sink_st>();
    // static auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("dlstreamer.txt", true);
    const char *env_pattern = std::getenv("GVA_LOG_PATTERN");
    if (env_pattern)
        custom_sink->set_pattern(env_pattern);
    const char *level = std::getenv("GVA_LOG_LEVEL");
    if (level) {
        auto level_int = to_sdp_level(GstDebugLevel(std::atoi(level)));
        custom_sink->set_level(level_int);
    }
    return custom_sink;
}

spdlog::sink_ptr common_sink_instance() {
    static auto custom_sink = create_sink();
    return custom_sink;
}

std::string get_logger_name(GstDebugCategory *category, GObject *object) {
    if (object && GST_OBJECT_NAME(object))
        return GST_OBJECT_NAME(object);
    else if (category && category->name)
        return fmt::format("{}@{}", category->name, fmt::ptr(object));
    assert(false && "invalid params to get logger");
    return "";
}

std::shared_ptr<spdlog::logger> init_logger(GstDebugCategory *category, GObject *object) {
    std::string logger_name = get_logger_name(category, object);
    auto logger = spdlog::get(logger_name);
    if (logger)
        return logger;
    spdlog::sinks_init_list sink_list = {create_gst_sink_instance(category, object),
#ifndef IGNORE_EXTRA_SINK_LOGGER
                                         common_sink_instance()
#endif
    };
    logger = std::make_shared<spdlog::logger>(logger_name, sink_list);
    spdlog::register_logger(logger);
    logger->set_level(to_sdp_level(gst_debug_category_get_threshold(category)));
    return logger;
}
} // namespace dlstreamer::log