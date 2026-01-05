/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstradarprocessor.h"
#include "radar_config.hpp"
#include <cstring>
#include <numeric>
#include <algorithm>

GST_DEBUG_CATEGORY_STATIC(gst_radar_processor_debug);
#define GST_CAT_DEFAULT gst_radar_processor_debug

enum {
    PROP_0,
    PROP_RADAR_CONFIG,
    PROP_FRAME_RATE
};

#define DEFAULT_FRAME_RATE 0.0

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("application/octet-stream")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("application/x-radar-processed")
);

static void gst_radar_processor_set_property(GObject *object, guint prop_id,
                                             const GValue *value, GParamSpec *pspec);
static void gst_radar_processor_get_property(GObject *object, guint prop_id,
                                             GValue *value, GParamSpec *pspec);
static void gst_radar_processor_finalize(GObject *object);

static gboolean gst_radar_processor_start(GstBaseTransform *trans);
static gboolean gst_radar_processor_stop(GstBaseTransform *trans);
static GstFlowReturn gst_radar_processor_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static GstCaps *gst_radar_processor_transform_caps(GstBaseTransform *trans,
                                                    GstPadDirection direction,
                                                    GstCaps *caps,
                                                    GstCaps *filter);

G_DEFINE_TYPE(GstRadarProcessor, gst_radar_processor, GST_TYPE_BASE_TRANSFORM);

static void gst_radar_processor_class_init(GstRadarProcessorClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_radar_processor_set_property;
    gobject_class->get_property = gst_radar_processor_get_property;
    gobject_class->finalize = gst_radar_processor_finalize;

    g_object_class_install_property(gobject_class, PROP_RADAR_CONFIG,
        g_param_spec_string("radar-config", "Radar Config",
                           "Path to radar configuration JSON file",
                           NULL,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_FRAME_RATE,
        g_param_spec_double("frame-rate", "Frame Rate",
                           "Frame rate for output (0 = no limit)",
                           0.0, G_MAXDOUBLE, DEFAULT_FRAME_RATE,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class,
        "Radar Signal Processor",
        "Filter/Converter",
        "Processes millimeter wave radar signals with DC removal and reordering",
        "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_radar_processor_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_radar_processor_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_radar_processor_transform_ip);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_radar_processor_transform_caps);
}

static void gst_radar_processor_init(GstRadarProcessor *filter) {
    filter->radar_config = NULL;
    filter->frame_rate = DEFAULT_FRAME_RATE;
    
    filter->num_rx = 0;
    filter->num_tx = 0;
    filter->num_chirps = 0;
    filter->adc_samples = 0;
    filter->trn = 0;
    
    filter->last_frame_time = GST_CLOCK_TIME_NONE;
    filter->frame_duration = GST_CLOCK_TIME_NONE;
}

static void gst_radar_processor_finalize(GObject *object) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(object);

    g_free(filter->radar_config);
    filter->input_data.clear();
    filter->output_data.clear();

    G_OBJECT_CLASS(gst_radar_processor_parent_class)->finalize(object);
}

static void gst_radar_processor_set_property(GObject *object, guint prop_id,
                                             const GValue *value, GParamSpec *pspec) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(object);

    switch (prop_id) {
        case PROP_RADAR_CONFIG:
            g_free(filter->radar_config);
            filter->radar_config = g_value_dup_string(value);
            break;
        case PROP_FRAME_RATE:
            filter->frame_rate = g_value_get_double(value);
            if (filter->frame_rate > 0.0) {
                filter->frame_duration = (GstClockTime)(GST_SECOND / filter->frame_rate);
            } else {
                filter->frame_duration = GST_CLOCK_TIME_NONE;
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_radar_processor_get_property(GObject *object, guint prop_id,
                                             GValue *value, GParamSpec *pspec) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(object);

    switch (prop_id) {
        case PROP_RADAR_CONFIG:
            g_value_set_string(value, filter->radar_config);
            break;
        case PROP_FRAME_RATE:
            g_value_set_double(value, filter->frame_rate);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static gboolean gst_radar_processor_start(GstBaseTransform *trans) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(trans);

    GST_DEBUG_OBJECT(filter, "Starting radar processor");

    if (!filter->radar_config) {
        GST_ERROR_OBJECT(filter, "No radar config file specified");
        return FALSE;
    }

    // Load radar configuration
    try {
        RadarConfig config;
        if (!config.load_from_json(filter->radar_config)) {
            GST_ERROR_OBJECT(filter, "Failed to load radar config from: %s", filter->radar_config);
            return FALSE;
        }

        filter->num_rx = config.num_rx;
        filter->num_tx = config.num_tx;
        filter->num_chirps = config.num_chirps;
        filter->adc_samples = config.adc_samples;
        filter->trn = filter->num_rx * filter->num_tx;

        GST_INFO_OBJECT(filter, "Loaded radar config: RX=%u, TX=%u, Chirps=%u, Samples=%u, TRN=%u",
                       filter->num_rx, filter->num_tx, filter->num_chirps, 
                       filter->adc_samples, filter->trn);

        // Allocate buffers
        size_t total_samples = filter->trn * filter->num_chirps * filter->adc_samples;
        filter->input_data.resize(total_samples);
        filter->output_data.resize(total_samples);

        GST_INFO_OBJECT(filter, "Allocated buffers for %zu complex samples", total_samples);

    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(filter, "Exception loading config: %s", e.what());
        return FALSE;
    }

    filter->last_frame_time = GST_CLOCK_TIME_NONE;

    return TRUE;
}

static gboolean gst_radar_processor_stop(GstBaseTransform *trans) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(trans);

    GST_DEBUG_OBJECT(filter, "Stopping radar processor");

    filter->input_data.clear();
    filter->output_data.clear();
    filter->last_frame_time = GST_CLOCK_TIME_NONE;

    return TRUE;
}

static GstCaps *gst_radar_processor_transform_caps(GstBaseTransform *trans,
                                                    GstPadDirection direction,
                                                    GstCaps *caps,
                                                    GstCaps *filter) {
    if (direction == GST_PAD_SINK) {
        return gst_caps_new_simple("application/x-radar-processed", NULL, NULL);
    } else {
        return gst_caps_new_simple("application/octet-stream", NULL, NULL);
    }
}

// DC removal function: removes mean from real and imaginary parts
static void dc_removal(std::complex<float> *data, size_t count) {
    if (count == 0) return;

    // Calculate mean of real and imaginary parts
    float real_sum = 0.0f;
    float imag_sum = 0.0f;
    
    for (size_t i = 0; i < count; i++) {
        real_sum += data[i].real();
        imag_sum += data[i].imag();
    }
    
    float real_avg = real_sum / count;
    float imag_avg = imag_sum / count;
    
    // Subtract mean from each sample
    for (size_t i = 0; i < count; i++) {
        data[i] = std::complex<float>(data[i].real() - real_avg, 
                                      data[i].imag() - imag_avg);
    }
}

static GstFlowReturn gst_radar_processor_transform_ip(GstBaseTransform *trans, GstBuffer *buffer) {
    GstRadarProcessor *filter = GST_RADAR_PROCESSOR(trans);

    // Frame rate control
    if (filter->frame_duration != GST_CLOCK_TIME_NONE) {
        GstClock *clock = gst_element_get_clock(GST_ELEMENT(trans));
        if (clock) {
            GstClockTime current_time = gst_clock_get_time(clock);
            gst_object_unref(clock);

            if (filter->last_frame_time != GST_CLOCK_TIME_NONE) {
                GstClockTime elapsed = current_time - filter->last_frame_time;
                if (elapsed < filter->frame_duration) {
                    GstClockTime sleep_time = filter->frame_duration - elapsed;
                    g_usleep(GST_TIME_AS_USECONDS(sleep_time));
                }
            }
            filter->last_frame_time = gst_clock_get_time(clock);
        }
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READWRITE)) {
        GST_ERROR_OBJECT(filter, "Failed to map buffer");
        return GST_FLOW_ERROR;
    }

    size_t expected_size = filter->trn * filter->num_chirps * filter->adc_samples * sizeof(std::complex<float>);
    
    if (map.size != expected_size) {
        GST_ERROR_OBJECT(filter, "Buffer size mismatch: got %zu bytes, expected %zu bytes",
                        map.size, expected_size);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    // Copy input data (trn*c*s layout)
    std::complex<float> *input_ptr = reinterpret_cast<std::complex<float>*>(map.data);
    std::copy(input_ptr, input_ptr + filter->input_data.size(), filter->input_data.begin());

    GST_DEBUG_OBJECT(filter, "Processing frame: TRN=%u, Chirps=%u, Samples=%u",
                    filter->trn, filter->num_chirps, filter->adc_samples);

    // Reorder from trn*c*s to c*trn*s and apply DC removal
    for (guint c = 0; c < filter->num_chirps; c++) {
        for (guint t = 0; t < filter->trn; t++) {
            // Apply DC removal on each 256-sample segment
            std::complex<float> temp_samples[256];
            
            // Extract samples for this chirp and channel
            for (guint s = 0; s < filter->adc_samples; s++) {
                // Input layout: trn*c*s
                size_t input_idx = t * filter->num_chirps * filter->adc_samples + 
                                  c * filter->adc_samples + s;
                temp_samples[s] = filter->input_data[input_idx];
            }
            
            // Apply DC removal
            dc_removal(temp_samples, filter->adc_samples);
            
            // Copy to output with new layout: c*trn*s
            for (guint s = 0; s < filter->adc_samples; s++) {
                size_t output_idx = c * filter->trn * filter->adc_samples + 
                                   t * filter->adc_samples + s;
                filter->output_data[output_idx] = temp_samples[s];
            }
        }
    }

    // Copy processed data back to buffer
    std::copy(filter->output_data.begin(), filter->output_data.end(), input_ptr);

    gst_buffer_unmap(buffer, &map);

    GST_LOG_OBJECT(filter, "Frame processed successfully");

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(gst_radar_processor_debug, "radarprocessor", 0, 
                            "Radar Signal Processor");

    return gst_element_register(plugin, "radarprocessor", GST_RANK_NONE, 
                               GST_TYPE_RADAR_PROCESSOR);
}

#ifndef PACKAGE
#define PACKAGE "radarprocessor"
#endif

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    radarprocessor,
    "Radar Signal Processor",
    plugin_init,
    "1.0",
    "MIT",
    "dlstreamer",
    "https://github.com/dlstreamer/dlstreamer"
)
