/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstradarprocess.h"
#include "g3d_radarprocess_meta.h"
#include "radar_config.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <numeric>

GST_DEBUG_CATEGORY_STATIC(gst_radar_process_debug);
#define GST_CAT_DEFAULT gst_radar_process_debug

enum { PROP_0, PROP_RADAR_CONFIG, PROP_FRAME_RATE };

#define DEFAULT_FRAME_RATE 0.0

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/octet-stream"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-radar-processed"));

static void gst_radar_process_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_radar_process_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_radar_process_finalize(GObject *object);

static gboolean gst_radar_process_start(GstBaseTransform *trans);
static gboolean gst_radar_process_stop(GstBaseTransform *trans);
static GstFlowReturn gst_radar_process_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static GstCaps *gst_radar_process_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter);

G_DEFINE_TYPE(GstRadarProcess, gst_radar_process, GST_TYPE_BASE_TRANSFORM);

static void gst_radar_process_class_init(GstRadarProcessClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_radar_process_debug, "g3dradarprocess", 0, "Radar Signal Processing Element");

    gobject_class->set_property = gst_radar_process_set_property;
    gobject_class->get_property = gst_radar_process_get_property;
    gobject_class->finalize = gst_radar_process_finalize;

    g_object_class_install_property(gobject_class, PROP_RADAR_CONFIG,
                                    g_param_spec_string("radar-config", "Radar Config",
                                                        "Path to radar configuration JSON file", nullptr,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAME_RATE,
        g_param_spec_double("frame-rate", "Frame Rate", "Frame rate for output (0 = no limit)", 0.0, G_MAXDOUBLE,
                            DEFAULT_FRAME_RATE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class, "Radar Signal Process", "Filter/Converter",
                                          "Processes millimeter wave radar signals with DC removal and reordering",
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_radar_process_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_radar_process_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_radar_process_transform_ip);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_radar_process_transform_caps);
}

static void gst_radar_process_init(GstRadarProcess *filter) {
    filter->radar_config = nullptr;
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

    filter->radar_buffer = nullptr;
    filter->radar_buffer_size = 0;

    filter->libradar_handle = nullptr;
    filter->radarGetMemSize_fn = nullptr;
    filter->radarInitHandle_fn = nullptr;
    filter->radarDetection_fn = nullptr;
    filter->radarClustering_fn = nullptr;
    filter->radarTracking_fn = nullptr;
    filter->radarDestroyHandle_fn = nullptr;
}

static void gst_radar_process_finalize(GObject *object) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(object);

    g_free(filter->radar_config);
    filter->input_data.clear();
    filter->output_data.clear();

    if (filter->radar_buffer) {
        free(filter->radar_buffer);
        filter->radar_buffer = nullptr;
    }

    if (filter->libradar_handle) {
        dlclose(filter->libradar_handle);
        filter->libradar_handle = nullptr;
    }

    G_OBJECT_CLASS(gst_radar_process_parent_class)->finalize(object);
}

static void gst_radar_process_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(object);

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

static void gst_radar_process_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(object);

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

static gboolean gst_radar_process_start(GstBaseTransform *trans) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(trans);

    GST_DEBUG_OBJECT(filter, "Starting radar process");

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
        filter->radar_cube.mat = nullptr; // Will be set to output_data in transform

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
        filter->cluster_result.cd =
            nullptr; //(ClusterDescription*)ALIGN_ALLOC(64, sizeof(ClusterDescription) * config.max_clusters)

        // Initialize radar handle
        filter->radar_handle = nullptr;

        // Initialize TrackingResult
        const int maxTrackingLen = 64;
        filter->tracking_desc_buf.resize(maxTrackingLen);
        filter->tracking_result.len = 0;
        filter->tracking_result.maxLen = maxTrackingLen;
        filter->tracking_result.td = filter->tracking_desc_buf.data();

        GST_INFO_OBJECT(filter, "Loaded radar config: RX=%u, TX=%u, Chirps=%u, Samples=%u, TRN=%u", filter->num_rx,
                        filter->num_tx, filter->num_chirps, filter->adc_samples, filter->trn);

        // Allocate buffers
        size_t total_samples = filter->trn * filter->num_chirps * filter->adc_samples;
        filter->input_data.resize(total_samples);
        filter->output_data.resize(total_samples);

        GST_INFO_OBJECT(filter, "Allocated buffers for %zu complex samples", total_samples);

        // Initialize libradar handle
        filter->radar_buffer_size = 0;

        // Load libradar.so dynamically
        // Use library name without path to let the system search via LD_LIBRARY_PATH
#ifdef _WIN32
        const char *libradar_name = "libradar.dll";
#else
        const char *libradar_name = "libradar.so";
#endif
        filter->libradar_handle = dlopen(libradar_name, RTLD_LAZY);
        if (!filter->libradar_handle) {
            GST_ERROR_OBJECT(filter, "Failed to load library %s: %s", libradar_name, dlerror());
            GST_ERROR_OBJECT(filter, "Make sure libradar is installed and library paths are configured:");
            GST_ERROR_OBJECT(filter, "  1. Run: scripts\\install_radar_dependencies.sh");
            GST_ERROR_OBJECT(filter, "  2. Source oneAPI env if new terminal: source /opt/intel/oneapi/setvars.sh");
            return FALSE;
        }
        GST_INFO_OBJECT(filter, "Successfully loaded %s", libradar_name);

        // Load function pointers
        filter->radarGetMemSize_fn =
            (RadarErrorCode(*)(RadarParam *, ulong *))dlsym(filter->libradar_handle, "radarGetMemSize");
        if (!filter->radarGetMemSize_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarGetMemSize': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        filter->radarInitHandle_fn = (RadarErrorCode(*)(RadarHandle **, RadarParam *, void *, ulong))dlsym(
            filter->libradar_handle, "radarInitHandle");
        if (!filter->radarInitHandle_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarInitHandle': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        filter->radarDetection_fn = (RadarErrorCode(*)(RadarHandle *, RadarCube *, RadarPointClouds *))dlsym(
            filter->libradar_handle, "radarDetection");
        if (!filter->radarDetection_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarDetection': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        filter->radarClustering_fn = (RadarErrorCode(*)(RadarHandle *, RadarPointClouds *, ClusterResult *))dlsym(
            filter->libradar_handle, "radarClustering");
        if (!filter->radarClustering_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarClustering': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        filter->radarTracking_fn = (RadarErrorCode(*)(RadarHandle *, ClusterResult *, TrackingResult *))dlsym(
            filter->libradar_handle, "radarTracking");
        if (!filter->radarTracking_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarTracking': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        filter->radarDestroyHandle_fn =
            (RadarErrorCode(*)(RadarHandle *))dlsym(filter->libradar_handle, "radarDestroyHandle");
        if (!filter->radarDestroyHandle_fn) {
            GST_ERROR_OBJECT(filter, "Failed to find symbol 'radarDestroyHandle': %s", dlerror());
            dlclose(filter->libradar_handle);
            filter->libradar_handle = nullptr;
            return FALSE;
        }

        GST_INFO_OBJECT(filter, "All libradar function symbols loaded successfully");

        RadarErrorCode ret = filter->radarGetMemSize_fn(&filter->radar_param, &filter->radar_buffer_size);
        if (ret != R_SUCCESS || filter->radar_buffer_size == 0) {
            GST_ERROR_OBJECT(filter, "Failed to get radar memory size, error code: %d", ret);
            return FALSE;
        }

        GST_INFO_OBJECT(filter, "Radar memory size required: %lu bytes", filter->radar_buffer_size);

        // Allocate aligned memory buffer
#ifdef _WIN32
        filter->radar_buffer = _aligned_malloc(filter->radar_buffer_size, 64);
        if (!filter->radar_buffer) {
            GST_ERROR_OBJECT(filter, "Failed to allocate aligned memory buffer");
            return FALSE;
        }
#else
        if (posix_memalign(&filter->radar_buffer, 64, filter->radar_buffer_size) != 0) {
            GST_ERROR_OBJECT(filter, "Failed to allocate aligned memory buffer");
            filter->radar_buffer = nullptr;
            return FALSE;
        }
#endif

        // Initialize radar handle
        ret = filter->radarInitHandle_fn(&filter->radar_handle, &filter->radar_param, filter->radar_buffer,
                                         filter->radar_buffer_size);
        if (ret != R_SUCCESS) {
            GST_ERROR_OBJECT(filter, "Failed to initialize radar handle, error code: %d", ret);
#ifdef _WIN32
            _aligned_free(filter->radar_buffer);
#else
            free(filter->radar_buffer);
#endif
            filter->radar_buffer = nullptr;
            return FALSE;
        }

        GST_INFO_OBJECT(filter, "Radar handle initialized successfully");

    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(filter, "Exception loading config: %s", e.what());
        return FALSE;
    }

    filter->last_frame_time = GST_CLOCK_TIME_NONE;

    return TRUE;
}

static gboolean gst_radar_process_stop(GstBaseTransform *trans) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(trans);

    GST_DEBUG_OBJECT(filter, "Stopping radar process");

    // Print statistics
    if (filter->total_frames > 0) {
        gdouble avg_time = filter->total_processing_time / filter->total_frames;
        GST_INFO_OBJECT(filter, "=== Radar Process Statistics ===");
        GST_INFO_OBJECT(filter, "Total frames processed: %" G_GUINT64_FORMAT, filter->total_frames);
        GST_INFO_OBJECT(filter, "Total processing time: %.3f seconds", filter->total_processing_time);
        GST_INFO_OBJECT(filter, "Average time per frame: %.3f ms", avg_time * 1000.0);
        GST_INFO_OBJECT(filter, "===================================");
    }

    // Destroy radar handle
    if (filter->radar_handle && filter->radarDestroyHandle_fn) {
        RadarErrorCode ret = filter->radarDestroyHandle_fn(filter->radar_handle);
        if (ret != R_SUCCESS) {
            GST_WARNING_OBJECT(filter, "Failed to destroy radar handle, error code: %d", ret);
        } else {
            GST_INFO_OBJECT(filter, "Radar handle destroyed successfully");
        }
        filter->radar_handle = nullptr;
    }

    // Close dynamic library
    if (filter->libradar_handle) {
        dlclose(filter->libradar_handle);
        GST_INFO_OBJECT(filter, "libradar.so unloaded");
        filter->libradar_handle = nullptr;
    }

    // Free radar buffer
    if (filter->radar_buffer) {
#ifdef _WIN32
        _aligned_free(filter->radar_buffer);
#else
        free(filter->radar_buffer);
#endif
        filter->radar_buffer = nullptr;
    }
    filter->radar_buffer_size = 0;

    filter->input_data.clear();
    filter->output_data.clear();
    filter->tracking_desc_buf.clear();
    filter->last_frame_time = GST_CLOCK_TIME_NONE;

    return TRUE;
}

static GstCaps *gst_radar_process_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter) {
    if (direction == GST_PAD_SINK) {
        return gst_caps_new_simple("application/x-radar-processed", nullptr, nullptr);
    } else {
        return gst_caps_new_simple("application/octet-stream", nullptr, nullptr);
    }
}

// DC removal function: removes mean from real and imaginary parts
static void dc_removal(std::complex<float> *data, size_t count) {
    if (count == 0)
        return;

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
        data[i] = std::complex<float>(data[i].real() - real_avg, data[i].imag() - imag_avg);
    }
}

static GstFlowReturn gst_radar_process_transform_ip(GstBaseTransform *trans, GstBuffer *buffer) {
    GstRadarProcess *filter = GST_RADAR_PROCESS(trans);

    // Start timing for this frame using std::chrono
    auto start_time = std::chrono::high_resolution_clock::now();

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
        GST_ERROR_OBJECT(filter, "Buffer size mismatch: got %zu bytes, expected %zu bytes", map.size, expected_size);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }

    // Copy input data (c*trn*s layout)
    std::complex<float> *input_ptr = reinterpret_cast<std::complex<float> *>(map.data);
    std::copy(input_ptr, input_ptr + filter->input_data.size(), filter->input_data.begin());

    GST_DEBUG_OBJECT(filter, "Processing frame #%" G_GUINT64_FORMAT ":TRN=%u, Chirps=%u, Samples=%u", filter->frame_id,
                     filter->trn, filter->num_chirps, filter->adc_samples);

    // Reorder from c*trn*s to trn*c*s and apply DC removal
    for (guint c = 0; c < filter->num_chirps; c++) {
        for (guint t = 0; t < filter->trn; t++) {
            // Apply DC removal on each 256-sample segment
            std::complex<float> temp_samples[256];

            // Extract samples for this chirp and channel
            for (guint s = 0; s < filter->adc_samples; s++) {
                // Input layout: c*trn*s
                size_t input_idx = c * filter->trn * filter->adc_samples + t * filter->adc_samples + s;
                temp_samples[s] = filter->input_data[input_idx];
            }

            // Apply DC removal
            dc_removal(temp_samples, filter->adc_samples);

            // Copy to output with new layout: trn*c*s
            for (guint s = 0; s < filter->adc_samples; s++) {
                size_t output_idx = t * filter->num_chirps * filter->adc_samples + c * filter->adc_samples + s;
                filter->output_data[output_idx] = temp_samples[s];
            }
        }
    }

    // Update RadarCube mat pointer to output_data
    // std::complex<float> and cfloat have compatible memory layout
    filter->radar_cube.mat = reinterpret_cast<cfloat *>(filter->output_data.data());

    // Call radar processing functions
    RadarErrorCode ret;

    // 1. Radar Detection
    ret = filter->radarDetection_fn(filter->radar_handle, &filter->radar_cube, &filter->radar_point_clouds);
    if (ret != R_SUCCESS) {
        GST_ERROR_OBJECT(filter, "radarDetection failed with error code: %d", ret);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }
    GST_DEBUG_OBJECT(filter, "radarDetection completed, detected %d points", filter->radar_point_clouds.len);

    // 2. Radar Clustering
    ret = filter->radarClustering_fn(filter->radar_handle, &filter->radar_point_clouds, &filter->cluster_result);
    if (ret != R_SUCCESS) {
        GST_ERROR_OBJECT(filter, "radarClustering failed with error code: %d", ret);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }
    GST_DEBUG_OBJECT(filter, "radarClustering completed, found %d clusters", filter->cluster_result.n);

    // 3. Radar Tracking
    ret = filter->radarTracking_fn(filter->radar_handle, &filter->cluster_result, &filter->tracking_result);
    if (ret != R_SUCCESS) {
        GST_ERROR_OBJECT(filter, "radarTracking failed with error code: %d", ret);
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_ERROR;
    }
    GST_DEBUG_OBJECT(filter, "radarTracking completed, tracking %d objects", filter->tracking_result.len);

    // Copy processed data back to buffer
    std::copy(filter->output_data.begin(), filter->output_data.end(), input_ptr);
    gst_buffer_unmap(buffer, &map);

    // Add radar processing results as metadata to the buffer
    GstRadarProcessMeta *meta = gst_buffer_add_radar_process_meta(buffer, filter->frame_id, &filter->radar_point_clouds,
                                                                  &filter->cluster_result, &filter->tracking_result);
    if (meta) {
        GST_DEBUG_OBJECT(filter, "Added radar metadata: %d points, %d clusters, %d tracked objects",
                         meta->point_clouds_len, meta->num_clusters, meta->num_tracked_objects);
    } else {
        GST_WARNING_OBJECT(filter, "Failed to add radar metadata to buffer");
    }

    // Calculate processing time using std::chrono
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> frame_time = end_time - start_time;

    // Update statistics
    filter->total_processing_time += frame_time.count();
    filter->total_frames++;

    GST_DEBUG_OBJECT(filter, "Frame #%" G_GUINT64_FORMAT " processed successfully in %.3f ms", filter->frame_id,
                     frame_time.count() * 1000.0);

    filter->frame_id++;

    return GST_FLOW_OK;
}
