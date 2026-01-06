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
#include <sys/time.h>

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
    
    filter->frame_id = 0;
    filter->total_frames = 0;
    filter->total_processing_time = 0.0;
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

        // Initialize RadarParam with config values
        filter->radar_param.startFreq = config.start_frequency;
        filter->radar_param.idle = config.idle;
        filter->radar_param.adcStartTime = config.adc_start_time;
        filter->radar_param.rampEndTime = config.ramp_end_time;
        filter->radar_param.freqSlopeConst = config.freq_slope_const;
        filter->radar_param.adcSampleRate = config.adc_sample_rate;
        filter->radar_param.rn = config.num_rx;
        filter->radar_param.tn = config.num_tx;
        filter->radar_param.sn = config.adc_samples;
        filter->radar_param.cn = config.num_chirps;
        filter->radar_param.fps = config.fps;

        filter->radar_param.dFAR = config.doppler_pfa;
        filter->radar_param.rFAR = config.range_pfa;
        filter->radar_param.dGWL = config.doppler_win_guard_len;
        filter->radar_param.dTWL = config.doppler_win_train_len;
        filter->radar_param.rGWL = config.range_win_guard_len;
        filter->radar_param.rTWL = config.range_win_train_len;
        // JSON uses 1-based indexing, RadarDoaType enum is 0-based
        filter->radar_param.doaType = (RadarDoaType)(config.aoa_estimation_type - 1);

        filter->radar_param.eps = config.eps;
        filter->radar_param.weight = config.weight;
        filter->radar_param.mpc = config.min_points_in_cluster;
        filter->radar_param.mc = config.max_clusters;
        filter->radar_param.mp = config.max_points;

        filter->radar_param.tat = config.tracker_association_threshold;
        filter->radar_param.mnv = config.measurement_noise_variance;
        filter->radar_param.tpf = config.time_per_frame;
        filter->radar_param.iff = config.iir_forget_factor;
        filter->radar_param.at = config.tracker_active_threshold;
        filter->radar_param.ft = config.tracker_forget_threshold;

        // Initialize RadarCube
        filter->radar_cube.rn = config.num_rx;
        filter->radar_cube.tn = config.num_tx;
        filter->radar_cube.sn = config.adc_samples;
        filter->radar_cube.cn = config.num_chirps;
        filter->radar_cube.mat = nullptr;  // Will be set to output_data in transform

        // Initialize RadarPointClouds 
        filter->radar_point_clouds.len = 0;
        filter->radar_point_clouds.maxLen = config.max_points;
        filter->radar_point_clouds.rangeIdx = nullptr; //(ushort*)ALIGN_ALLOC(64, sizeof(ushort) * config.max_points)
        filter->radar_point_clouds.speedIdx = nullptr; //(ushort*)ALIGN_ALLOC(64, sizeof(ushort) * config.max_points)
        filter->radar_point_clouds.range = nullptr;    //(float*)ALIGN_ALLOC(64, sizeof(float) * config.max_points)
        filter->radar_point_clouds.speed = nullptr;    //(float*)ALIGN_ALLOC(64, sizeof(float) * config.max_points)
        filter->radar_point_clouds.angle = nullptr;    //(float*)ALIGN_ALLOC(64, sizeof(float) * config.max_points)
        filter->radar_point_clouds.snr = nullptr;      //(float*)ALIGN_ALLOC(64, sizeof(float) * config.max_points)

        // Initialize ClusterResult
        filter->cluster_result.n = 0;
        filter->cluster_result.idx = nullptr; //(int*)ALIGN_ALLOC(64, sizeof(int) * config.max_clusters)
        filter->cluster_result.cd = nullptr;  //(ClusterDescription*)ALIGN_ALLOC(64, sizeof(ClusterDescription) * config.max_clusters)

        // Initialize radar handle
        filter->radar_handle = nullptr;

        // Initialize TrackingResult
        const int maxTrackingLen = 64;
        filter->tracking_desc_buf.resize(maxTrackingLen);
        filter->tracking_result.len = 0;
        filter->tracking_result.maxLen = maxTrackingLen;
        filter->tracking_result.td = filter->tracking_desc_buf.data();

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

    // Print statistics
    if (filter->total_frames > 0) {
        gdouble avg_time = filter->total_processing_time / filter->total_frames;
        GST_INFO_OBJECT(filter, "=== Radar Processor Statistics ===");
        GST_INFO_OBJECT(filter, "Total frames processed: %" G_GUINT64_FORMAT, filter->total_frames);
        GST_INFO_OBJECT(filter, "Total processing time: %.3f seconds", filter->total_processing_time);
        GST_INFO_OBJECT(filter, "Average time per frame: %.3f ms", avg_time * 1000.0);
        GST_INFO_OBJECT(filter, "===================================");
    }

    filter->input_data.clear();
    filter->output_data.clear();
    filter->tracking_desc_buf.clear();
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

    // Start timing for this frame
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

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

    GST_DEBUG_OBJECT(filter, "Processing frame #%" G_GUINT64_FORMAT ":TRN=%u, Chirps=%u, Samples=%u", filter->frame_id,
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

    // Update RadarCube mat pointer to output_data
    // std::complex<float> and cfloat have compatible memory layout
    filter->radar_cube.mat = reinterpret_cast<cfloat*>(filter->output_data.data());

    gst_buffer_unmap(buffer, &map);

    // Calculate processing time
    gettimeofday(&end_time, NULL);
    gdouble frame_time = (end_time.tv_sec - start_time.tv_sec) + 
                         (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    // Update statistics
    filter->total_processing_time += frame_time;
    filter->total_frames++;
    
    GST_DEBUG_OBJECT(filter, "Frame #%" G_GUINT64_FORMAT " processed successfully in %.3f ms",
                   filter->frame_id, frame_time * 1000.0);
    
    filter->frame_id++;

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
