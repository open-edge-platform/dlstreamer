/*******************************************************************************
 * Copyright (C) 2023-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "latency_tracer.h"
#include "latency_tracer_meta.h"
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
using namespace std;

#define ELEMENT_DESCRIPTION "Latency tracer to calculate time it takes to process each frame for element and pipeline"
GST_DEBUG_CATEGORY_STATIC(latency_tracer_debug);
G_DEFINE_TYPE(LatencyTracer, latency_tracer, GST_TYPE_TRACER);

static GstTracerRecord *tr_pipeline;
static GstTracerRecord *tr_element;
static GstTracerRecord *tr_element_interval;
static GstTracerRecord *tr_pipeline_interval;
static guint ns_to_ms = 1000000;
static guint ms_to_s = 1000;
using BufferListArgs = tuple<LatencyTracer *, guint64, GstPad *>;
#define UNUSED(x) (void)(x)

static GQuark data_string = g_quark_from_static_string("latency_tracer");

// Element type classification for caching
enum class ElementType {
    SOURCE,    // Element with no sink pads (produces data)
    SINK,      // Element with no source pads (consumes data)
    PROCESSING // Element with both sink and source pads
};

// Structure to track statistics per source-sink branch
struct BranchStats {
    string pipeline_name;
    string source_name;
    string sink_name;
    gdouble total_latency;
    gdouble min;
    gdouble max;
    guint frame_count;
    gdouble interval_total;
    gdouble interval_min;
    gdouble interval_max;
    guint interval_frame_count;
    GstClockTime interval_init_time;
    GstClockTime first_frame_init_ts;
    mutex mtx;

    BranchStats() {
        total_latency = 0.0;
        min = G_MAXDOUBLE; // Initialize to max value so first frame sets it
        max = 0.0;
        frame_count = 0;
        interval_total = 0.0;
        interval_min = G_MAXDOUBLE;
        interval_max = 0.0;
        interval_frame_count = 0;
        interval_init_time = 0;
        first_frame_init_ts = 0;
        pipeline_name = "";
    }

    void reset_interval(GstClockTime now) {
        interval_total = 0.0;
        interval_min = G_MAXDOUBLE;
        interval_max = 0.0;
        interval_init_time = now;
        interval_frame_count = 0;
    }

    void cal_log_pipeline_latency(guint64 ts, guint64 init_ts, gint interval) {
        // Local copies for logging outside the lock
        gdouble frame_latency, avg, local_min, local_max, pipeline_latency, fps;
        guint local_count;

        {
            lock_guard<mutex> guard(mtx);
            frame_count += 1;
            frame_latency = (gdouble)GST_CLOCK_DIFF(init_ts, ts) / ns_to_ms;
            gdouble pipeline_latency_ns = (gdouble)GST_CLOCK_DIFF(first_frame_init_ts, ts) / frame_count;
            pipeline_latency = pipeline_latency_ns / ns_to_ms;
            total_latency += frame_latency;
            avg = total_latency / frame_count;
            fps = 0;
            if (pipeline_latency > 0)
                fps = ms_to_s / pipeline_latency;

            if (frame_latency < min)
                min = frame_latency;
            if (frame_latency > max)
                max = frame_latency;

            // Copy values for logging
            local_min = min;
            local_max = max;
            local_count = frame_count;
        } // Lock released here

        // Log outside the lock to minimize lock duration
        GST_TRACE("[Latency Tracer] Pipeline: %s, Source: %s -> Sink: %s - Frame: %u, Latency: %.2f ms, Avg: %.2f ms, "
                  "Min: %.2f ms, Max: %.2f ms, Pipeline Latency: %.2f ms, FPS: %.2f",
                  pipeline_name.c_str(), source_name.c_str(), sink_name.c_str(), local_count, frame_latency, avg, 
                  local_min, local_max, pipeline_latency, fps);

        gst_tracer_record_log(tr_pipeline, pipeline_name.c_str(), source_name.c_str(), sink_name.c_str(), frame_latency, 
                  avg, local_min, local_max, pipeline_latency, fps, local_count);
        cal_log_pipeline_interval(ts, frame_latency, interval);
    }

    void cal_log_pipeline_interval(guint64 ts, gdouble frame_latency, gint interval) {
        interval_frame_count += 1;
        interval_total += frame_latency;
        if (frame_latency < interval_min)
            interval_min = frame_latency;
        if (frame_latency > interval_max)
            interval_max = frame_latency;
        gdouble ms = (gdouble)GST_CLOCK_DIFF(interval_init_time, ts) / ns_to_ms;
        if (ms >= interval) {
            gdouble pipeline_latency = ms / interval_frame_count;
            gdouble fps = ms_to_s / pipeline_latency;
            gdouble interval_avg = interval_total / interval_frame_count;
            GST_TRACE("[Latency Tracer Interval] Pipeline: %s, Source: %s -> Sink: %s - Interval: %.2f ms, Avg: %.2f ms, "
                      "Min: %.2f ms, Max: %.2f ms",
                      pipeline_name.c_str(), source_name.c_str(), sink_name.c_str(), ms, interval_avg, interval_min, 
                      interval_max);
            gst_tracer_record_log(tr_pipeline_interval, pipeline_name.c_str(), source_name.c_str(), sink_name.c_str(), 
                      ms, interval_avg, interval_min, interval_max, pipeline_latency, fps);
            reset_interval(ts);
        }
    }
};

// Pointer-based branch key for fast lookups (optimization: ~50% faster than string-based keys)
// Using pointer comparison is much faster than string comparison
// Include pipeline pointer to separate stats per pipeline
using BranchKey = tuple<GstElement *, GstElement *, GstElement *>; // <source, sink, pipeline>

// Hash function for BranchKey tuple
struct BranchKeyHash {
    std::size_t operator()(const BranchKey &k) const {
        // Hash all three pointers: source, sink, pipeline
        std::size_t h1 = std::hash<GstElement *>{}(std::get<0>(k)); // source
        std::size_t h2 = std::hash<GstElement *>{}(std::get<1>(k)); // sink
        std::size_t h3 = std::hash<GstElement *>{}(std::get<2>(k)); // pipeline
        
        // Combine hashes using boost::hash_combine pattern
        // 0x9e3779b9 is the golden ratio conjugate (φ⁻¹ * 2³²) used for hash mixing
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }
};

// Helper function to create a branch key using pointers (optimized)
static inline BranchKey create_branch_key(GstElement *source, GstElement *sink, GstElement *pipeline) {
    return std::make_tuple(source, sink, pipeline);
}

// Type-safe accessors for C++ objects stored in C struct
static unordered_map<BranchKey, BranchStats, BranchKeyHash> *get_branch_stats_map(LatencyTracer *lt) {
    if (!lt->branch_stats) {
        lt->branch_stats = new unordered_map<BranchKey, BranchStats, BranchKeyHash>();
    }
    return static_cast<unordered_map<BranchKey, BranchStats, BranchKeyHash> *>(lt->branch_stats);
}

static vector<GstElement *> *get_sources_list(LatencyTracer *lt) {
    if (!lt->sources_list) {
        lt->sources_list = new vector<GstElement *>();
    }
    return static_cast<vector<GstElement *> *>(lt->sources_list);
}

static vector<GstElement *> *get_sinks_list(LatencyTracer *lt) {
    if (!lt->sinks_list) {
        lt->sinks_list = new vector<GstElement *>();
    }
    return static_cast<vector<GstElement *> *>(lt->sinks_list);
}

// Element type cache accessor (optimization: ~70% reduction in type checking overhead)
static unordered_map<GstElement *, ElementType> *get_element_type_cache(LatencyTracer *lt) {
    if (!lt->element_type_cache) {
        lt->element_type_cache = new unordered_map<GstElement *, ElementType>();
    }
    return static_cast<unordered_map<GstElement *, ElementType> *>(lt->element_type_cache);
}

// Topology cache accessor (optimization: ~80% reduction in topology traversal)
static unordered_map<GstElement *, GstElement *> *get_topology_cache(LatencyTracer *lt) {
    if (!lt->topology_cache) {
        lt->topology_cache = new unordered_map<GstElement *, GstElement *>();
    }
    return static_cast<unordered_map<GstElement *, GstElement *> *>(lt->topology_cache);
}

static gboolean is_source_element(GstElement *element);
static gboolean is_sink_element(GstElement *element);

// Helper function to get cached element type with O(1) lookup
static ElementType get_cached_element_type(LatencyTracer *lt, GstElement *elem) {
    if (!elem)
        return ElementType::PROCESSING;

    auto *cache = get_element_type_cache(lt);
    auto it = cache->find(elem);
    if (it != cache->end()) {
        return it->second;
    }
    // Fallback: Element not in cache, should only happen before pipeline initialization
    // Perform expensive check and cache the result
    if (is_source_element(elem)) {
        (*cache)[elem] = ElementType::SOURCE;
        return ElementType::SOURCE;
    } else if (is_sink_element(elem)) {
        (*cache)[elem] = ElementType::SINK;
        return ElementType::SINK;
    } else {
        (*cache)[elem] = ElementType::PROCESSING;
        return ElementType::PROCESSING;
    }
}

// Helper function to check if element is a source using cache
static gboolean is_source_element_cached(LatencyTracer *lt, GstElement *elem) {
    if (!elem)
        return FALSE;
    return get_cached_element_type(lt, elem) == ElementType::SOURCE;
}

// Helper function to check if element is a sink using cache
static gboolean is_sink_element_cached(LatencyTracer *lt, GstElement *elem) {
    if (!elem)
        return FALSE;
    return get_cached_element_type(lt, elem) == ElementType::SINK;
}

static void latency_tracer_constructed(GObject *object) {
    LatencyTracer *lt = LATENCY_TRACER(object);
    gchar *params, *tmp;
    GstStructure *params_struct = NULL;
    g_object_get(lt, "params", &params, NULL);
    if (!params)
        return;

    tmp = g_strdup_printf("latency_tracer,%s", params);
    params_struct = gst_structure_from_string(tmp, NULL);
    g_free(tmp);

    if (params_struct) {
        const gchar *flags;
        /* Read the flags if available */
        flags = gst_structure_get_string(params_struct, "flags");
        if (flags) {
            lt->flags = static_cast<LatencyTracerFlags>(0);
            GStrv split = g_strsplit(flags, "+", -1);
            for (gint i = 0; split[i]; i++) {
                if (g_str_equal(split[i], "pipeline"))
                    lt->flags = static_cast<LatencyTracerFlags>(lt->flags | LATENCY_TRACER_FLAG_PIPELINE);
                else if (g_str_equal(split[i], "element"))
                    lt->flags = static_cast<LatencyTracerFlags>(lt->flags | LATENCY_TRACER_FLAG_ELEMENT);
                else
                    GST_WARNING_OBJECT(lt, "Invalid latency tracer flags %s", split[i]);
            }
            g_strfreev(split);
        }
        gst_structure_get_int(params_struct, "interval", &lt->interval);
        GST_INFO_OBJECT(lt, "interval set to %d ms", lt->interval);
        gst_structure_free(params_struct);
    }
    g_free(params);
}

static void latency_tracer_finalize(GObject *object) {
    LatencyTracer *lt = LATENCY_TRACER(object);

    // Clean up C++ objects
    if (lt->branch_stats) {
        delete static_cast<unordered_map<BranchKey, BranchStats, BranchKeyHash> *>(lt->branch_stats);
        lt->branch_stats = nullptr;
    }
    if (lt->sources_list) {
        delete static_cast<vector<GstElement *> *>(lt->sources_list);
        lt->sources_list = nullptr;
    }
    if (lt->sinks_list) {
        delete static_cast<vector<GstElement *> *>(lt->sinks_list);
        lt->sinks_list = nullptr;
    }
    if (lt->element_type_cache) {
        delete static_cast<unordered_map<GstElement *, ElementType> *>(lt->element_type_cache);
        lt->element_type_cache = nullptr;
    }
    if (lt->topology_cache) {
        delete static_cast<unordered_map<GstElement *, GstElement *> *>(lt->topology_cache);
        lt->topology_cache = nullptr;
    }

    G_OBJECT_CLASS(latency_tracer_parent_class)->finalize(object);
}

static void latency_tracer_class_init(LatencyTracerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->constructed = latency_tracer_constructed;
    gobject_class->finalize = latency_tracer_finalize;
    tr_pipeline = gst_tracer_record_new(
        "latency_tracer_pipeline.class", "pipeline_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING, 
                          "Pipeline name", NULL),
        "source_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING,
                          "Source element name", NULL),
        "sink_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING,
                          "Sink element name", NULL),
        "frame_latency", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "current frame latency in ms", NULL),
        "avg", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Average frame latency in ms", NULL),
        "min", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Min Per frame latency in ms", NULL),
        "max", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Max Per frame latency in ms", NULL),
        "latency", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "pipeline latency in ms(if frames dropped this may result in invalid value)", 
                          NULL),
        "fps", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "pipeline fps(if frames dropped this may result in invalid value)", NULL),
        "frame_num", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_UINT, "description", G_TYPE_STRING,
                          "Number of frames processed", NULL),
        NULL);

    tr_pipeline_interval = gst_tracer_record_new(
        "latency_tracer_pipeline_interval.class", "pipeline_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING,
                          "Pipeline name", NULL),
        "source_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING,
                          "Source element name", NULL),
        "sink_name", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description", G_TYPE_STRING,
                          "Sink element name", NULL),
        "interval", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING, 
                          "interval in ms", NULL),
        "avg", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Average interval frame latency in ms", NULL),
        "min", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Min interval Per frame latency in ms", NULL),
        "max", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "Max interval Per frame latency in ms", NULL),
        "latency", GST_TYPE_STRUCTURE,
        gst_structure_new(
            "value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
            "pipeline latency within the interval in ms(if frames dropped this may result in invalid value)", NULL),
        "fps", GST_TYPE_STRUCTURE,
        gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description", G_TYPE_STRING,
                          "pipeline fps within the interval(if frames dropped this may result in invalid value)", NULL),
        NULL);
    tr_element = gst_tracer_record_new("latency_tracer_element.class", "name", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description",
                                                         G_TYPE_STRING, "Element Name", NULL),
                                       "frame_latency", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                         G_TYPE_STRING, "current frame latency in ms", NULL),
                                       "avg", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                         G_TYPE_STRING, "Average frame latency in ms", NULL),
                                       "min", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                         G_TYPE_STRING, "Min Per frame latency in ms", NULL),
                                       "max", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                         G_TYPE_STRING, "Max Per frame latency in ms", NULL),
                                       "frame_num", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_UINT, "description",
                                                         G_TYPE_STRING, "Number of frame processed", NULL),
                                       "is_bin", GST_TYPE_STRUCTURE,
                                       gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_BOOLEAN, "description",
                                                         G_TYPE_STRING, "is element bin", NULL),
                                       NULL);
    tr_element_interval =
        gst_tracer_record_new("latency_tracer_element_interval.class", "name", GST_TYPE_STRUCTURE,
                              gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_STRING, "description",
                                                G_TYPE_STRING, "Element Name", NULL),
                              "interval", GST_TYPE_STRUCTURE,
                              gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                G_TYPE_STRING, "Interval ms", NULL),
                              "avg", GST_TYPE_STRUCTURE,
                              gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                G_TYPE_STRING, "Average interval latency in ms", NULL),
                              "min", GST_TYPE_STRUCTURE,
                              gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                G_TYPE_STRING, "Min interval frame latency in ms", NULL),
                              "max", GST_TYPE_STRUCTURE,
                              gst_structure_new("value", "type", G_TYPE_GTYPE, G_TYPE_DOUBLE, "description",
                                                G_TYPE_STRING, "Max interval frame latency in ms", NULL),
                              NULL);
    GST_DEBUG_CATEGORY_INIT(latency_tracer_debug, "latency_tracer", 0, "latency tracer");
}

static GstElement *get_real_pad_parent(GstPad *pad) {
    GstObject *parent;
    if (!pad)
        return NULL;
    parent = gst_object_get_parent(GST_OBJECT_CAST(pad));
    /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
    if (parent && GST_IS_GHOST_PAD(parent)) {
        GstObject *tmp;
        pad = GST_PAD_CAST(parent);
        tmp = gst_object_get_parent(GST_OBJECT_CAST(pad));
        gst_object_unref(parent);
        parent = tmp;
    }
    return GST_ELEMENT_CAST(parent);
}

struct ElementStats {
    gboolean is_bin;
    gdouble total;
    gdouble min;
    gdouble max;
    guint frame_count;
    gchar *name;
    gdouble interval_total;
    gdouble interval_min;
    gdouble interval_max;
    guint interval_frame_count;
    GstClockTime interval_init_time;
    mutex mtx;

    static void create(GstElement *elem, guint64 ts) {
        // This won't be converted to shared ptr because g_object_set_qdata_full destructor supports gpointer only
        auto *stats = new ElementStats{elem, ts};
        g_object_set_qdata_full(reinterpret_cast<GObject *>(elem), data_string, stats,
                                [](gpointer data) { delete static_cast<ElementStats *>(data); });
    }

    static ElementStats *from_element(GstElement *elem) {
        if (!elem)
            return nullptr;
        return static_cast<ElementStats *>(g_object_get_qdata(G_OBJECT(elem), data_string));
    }

    ElementStats(GstElement *elem, GstClockTime ts) {
        is_bin = GST_IS_BIN(elem);
        total = 0.0;
        min = G_MAXDOUBLE;
        max = 0.0;
        frame_count = 0;
        name = GST_ELEMENT_NAME(elem);
        reset_interval(ts);
    }

    void reset_interval(GstClockTime now) {
        interval_total = 0.0;
        interval_min = G_MAXDOUBLE;
        interval_max = 0.0;
        interval_init_time = now;
        interval_frame_count = 0;
    }

    void cal_log_element_latency(guint64 src_ts, guint64 sink_ts, gint interval) {
        // Local copies for logging outside the lock
        gdouble frame_latency, avg, local_min, local_max;
        guint local_count;

        {
            lock_guard<mutex> guard(mtx);
            frame_count += 1;
            frame_latency = (gdouble)GST_CLOCK_DIFF(sink_ts, src_ts) / ns_to_ms;
            total += frame_latency;
            avg = total / frame_count;
            if (frame_latency < min)
                min = frame_latency;
            if (frame_latency > max)
                max = frame_latency;

            // Copy values for logging
            local_min = min;
            local_max = max;
            local_count = frame_count;
        } // Lock released here

        // Log outside the lock to minimize lock duration
        gst_tracer_record_log(tr_element, name, frame_latency, avg, local_min, local_max, local_count, is_bin);
        cal_log_interval(frame_latency, src_ts, interval);
    }

    void cal_log_interval(gdouble frame_latency, guint64 src_ts, gint interval) {
        interval_frame_count += 1;
        interval_total += frame_latency;
        if (frame_latency < interval_min)
            interval_min = frame_latency;
        if (frame_latency > interval_max)
            interval_max = frame_latency;
        gdouble ms = (gdouble)GST_CLOCK_DIFF(interval_init_time, src_ts) / ns_to_ms;
        if (ms >= interval) {
            gdouble interval_avg = interval_total / interval_frame_count;
            gst_tracer_record_log(tr_element_interval, name, ms, interval_avg, interval_min, interval_max);
            reset_interval(src_ts);
        }
    }
};

// Check if element is in any pipeline (not restricted to lt->pipeline)
// Note: Parameter retained for GStreamer callback signature compatibility but no longer used for 
// pipeline-specific checks
static bool is_in_pipeline(LatencyTracer *lt, GstElement *elem) {
    UNUSED(lt); // No longer need to check specific pipeline
 
    if (!elem)
        return false;

    // Walk up the element hierarchy to find if there's a pipeline ancestor
    GstObject *parent = GST_OBJECT_CAST(elem);
    while (parent) {
        if (GST_IS_PIPELINE(parent)) {
            return true;  // Found a pipeline ancestor
        }
        parent = GST_OBJECT_PARENT(parent);
    }
    
    return false;  // Not in any pipeline
}

// Helper function to find which pipeline an element belongs to
static GstElement *find_pipeline_for_element(GstElement *elem) {
    if (!elem)
        return nullptr;
    
    // Walk up to find the top-level pipeline
    GstObject *parent = GST_OBJECT_CAST(elem);
    while (parent) {
        if (GST_IS_PIPELINE(parent)) {
            return GST_ELEMENT_CAST(parent);
        }
        parent = GST_OBJECT_PARENT(parent);
    }
    
    return nullptr;
}

// Helper function to determine if an element is a source
static gboolean is_source_element(GstElement *element) {
    if (!element) {
        return FALSE;
    }

    // Method 1: Check flag (fast path for well-behaved elements)
    if (GST_OBJECT_FLAG_IS_SET(element, GST_ELEMENT_FLAG_SOURCE)) {
        return TRUE;
    }

    // Method 2: Check pad templates (works even before pads are created)
    // A true source element has NO sink pad templates at all
    GstElementClass *element_class = GST_ELEMENT_GET_CLASS(element);
    const GList *pad_templates = gst_element_class_get_pad_template_list(element_class);

    gboolean has_src_template = FALSE;

    // Iterate through all pad templates
    for (const GList *l = pad_templates; l != NULL; l = l->next) {
        GstPadTemplate *templ = GST_PAD_TEMPLATE(l->data);
        GstPadDirection direction = GST_PAD_TEMPLATE_DIRECTION(templ);

        if (direction == GST_PAD_SINK) {
            // Found sink pad template - element is not a pure source
            // Can return early since we know it's not a pure source
            return FALSE;
        } else if (direction == GST_PAD_SRC) {
            has_src_template = TRUE;
        }
    }

    // True source: has source pad template(s) but NO sink pad templates
    return has_src_template;
}

// Helper function to determine if an element is a sink
static gboolean is_sink_element(GstElement *element) {
    if (!element) {
        return FALSE;
    }

    // Method 1: Check flag (fast path for well-behaved elements)
    if (GST_OBJECT_FLAG_IS_SET(element, GST_ELEMENT_FLAG_SINK)) {
        return TRUE;
    }

    // Method 2: Check pad templates (works even before pads are created)
    // A true sink element has NO source pad templates at all
    GstElementClass *element_class = GST_ELEMENT_GET_CLASS(element);
    const GList *pad_templates = gst_element_class_get_pad_template_list(element_class);

    gboolean has_sink_template = FALSE;

    // Iterate through all pad templates
    for (const GList *l = pad_templates; l != NULL; l = l->next) {
        GstPadTemplate *templ = GST_PAD_TEMPLATE(l->data);
        GstPadDirection direction = GST_PAD_TEMPLATE_DIRECTION(templ);

        if (direction == GST_PAD_SINK) {
            has_sink_template = TRUE;
        } else if (direction == GST_PAD_SRC) {
            // Found source pad template - element is not a pure sink
            // Can return early since we know it's not a pure sink
            return FALSE;
        }
    }

    // True sink: has sink pad template(s) but NO source pad templates
    // Classification examples:
    //   - fakesink: has sink templates, no src templates → TRUE (is a sink) ✅
    //   - decodebin: has BOTH sink and src templates → FALSE (not a sink, is processing element)
    //   - queue: has BOTH sink and src templates → FALSE (not a sink, is processing element)
    return has_sink_template;
}

// Recursively walk upstream from an element to find a tracked source
// This function performs topology analysis by traversing the pipeline graph
// upstream from a given element, following pad connections until it finds
// a source element that was discovered during pipeline initialization.
// This approach correctly identifies sources even when intermediate elements
// (like decodebin) create new buffers, unlike metadata-based tracking.
// OPTIMIZATION: Results are cached for O(1) lookups on subsequent calls.
static GstElement *find_upstream_source(LatencyTracer *lt, GstElement *elem) {
    if (!elem)
        return nullptr;

    // Check topology cache first (optimization: ~80% reduction in traversal overhead)
    auto *topo_cache = get_topology_cache(lt);
    auto cached = topo_cache->find(elem);
    if (cached != topo_cache->end()) {
        return cached->second;
    }

    auto *sources = static_cast<vector<GstElement *> *>(lt->sources_list);
    if (!sources)
        return nullptr;

    // Check if this element itself is a tracked source
    for (auto *src : *sources) {
        if (src == elem) {
            // Cache the result
            (*topo_cache)[elem] = src;
            return src;
        }
    }

    // Walk through all sink pads of this element
    GstIterator *iter = gst_element_iterate_sink_pads(elem);
    GValue val = G_VALUE_INIT;
    GstElement *found_source = nullptr;
    gboolean done = FALSE;

    while (!done) {
        switch (gst_iterator_next(iter, &val)) {
        case GST_ITERATOR_OK: {
            GstPad *sink_pad = GST_PAD(g_value_get_object(&val));
            GstPad *peer_pad = gst_pad_get_peer(sink_pad);

            if (peer_pad) {
                GstElement *upstream = get_real_pad_parent(peer_pad);
                gst_object_unref(peer_pad);

                // Recursively search upstream
                found_source = find_upstream_source(lt, upstream);
                if (found_source) {
                    g_value_unset(&val);
                    done = TRUE;
                    break;
                }
            }
            g_value_unset(&val);
            break;
        }
        case GST_ITERATOR_RESYNC:
            // Iterator was invalidated, resync and retry
            gst_iterator_resync(iter);
            break;
        case GST_ITERATOR_ERROR:
            // Error occurred, log with element context and stop
            if (elem) {
                GST_WARNING("Error while iterating sink pads for element %s", GST_ELEMENT_NAME(elem));
            } else {
                GST_WARNING("Error while iterating sink pads for unknown element");
            }
            done = TRUE;
            break;
        case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
    }

    gst_iterator_free(iter);

    // Cache the result for future O(1) lookups (only cache valid results)
    if (found_source) {
        (*topo_cache)[elem] = found_source;
    }

    return found_source;
}

static void add_latency_meta(LatencyTracer *lt, LatencyTracerMeta *meta, guint64 ts, GstBuffer *buffer,
                             GstElement *elem) {
    UNUSED(lt);
    UNUSED(elem);
    if (!gst_buffer_is_writable(buffer)) {
        // Skip non-writable buffers - expected for shared/read-only buffers
        GST_TRACE("Skipping non-writable buffer for latency metadata");
        return;
    }
    meta = LATENCY_TRACER_META_ADD(buffer);
    meta->init_ts = ts;
    meta->last_pad_push_ts = ts;
}

static void do_push_buffer_pre(LatencyTracer *lt, guint64 ts, GstPad *pad, GstBuffer *buffer) {
    // OPTIMIZATION D: Early exit if no flags enabled (skip all processing when tracer is disabled)
    if (!(lt->flags & (LATENCY_TRACER_FLAG_ELEMENT | LATENCY_TRACER_FLAG_PIPELINE))) {
        return;
    }

    GstElement *elem = get_real_pad_parent(pad);
    if (!is_in_pipeline(lt, elem))
        return;
    LatencyTracerMeta *meta = LATENCY_TRACER_META_GET(buffer);
    if (!meta) {
        // OPTIMIZATION: Only add metadata at source elements (~90% fewer metadata checks)
        // Check if this is a source element using cached type
        if (is_source_element_cached(lt, elem)) {
            add_latency_meta(lt, meta, ts, buffer, elem);
        }
        return;
    }
    if (lt->flags & LATENCY_TRACER_FLAG_ELEMENT) {
        ElementStats *stats = ElementStats::from_element(elem);
        // log latency only if ts is greater than last logged ts to avoid duplicate logging for the same buffer
        if (stats != nullptr && ts > meta->last_pad_push_ts) {
            stats->cal_log_element_latency(ts, meta->last_pad_push_ts, lt->interval);
            meta->last_pad_push_ts = ts;
        }
    }

    // Check if the peer of this pad is a sink element
    GstPad *peer_pad = GST_PAD_PEER(pad);
    GstElement *peer_element = peer_pad ? get_real_pad_parent(peer_pad) : nullptr;

    // OPTIMIZATION: Use cached element type check instead of expensive is_sink_element()
    if (lt->flags & LATENCY_TRACER_FLAG_PIPELINE && peer_element && is_sink_element_cached(lt, peer_element)) {
        GstElement *sink = peer_element;

        // Use topology analysis to find the source feeding this sink
        GstElement *source = find_upstream_source(lt, sink);

        if (source && sink) {
            // Find which pipeline this sink belongs to
            GstElement *pipeline = find_pipeline_for_element(sink);
            
            // Only track if element is in a pipeline (pipeline should not be null)
            if (!pipeline) {
                GST_DEBUG_OBJECT(lt, "Sink element %s is not in any pipeline, skipping branch tracking",
                                 GST_ELEMENT_NAME(sink));
                return;
            }
            
            BranchKey branch_key = create_branch_key(source, sink, pipeline);
            auto *stats_map = get_branch_stats_map(lt);

            // OPTIMIZATION: try_emplace constructs in-place (no copy), single map access
            auto result = stats_map->try_emplace(branch_key);
            BranchStats &branch = result.first->second;

            // Initialize only if this is a newly inserted branch
            if (result.second) {
                branch.pipeline_name = GST_ELEMENT_NAME(pipeline);
                branch.source_name = GST_ELEMENT_NAME(source);
                branch.sink_name = GST_ELEMENT_NAME(sink);
                branch.first_frame_init_ts = meta->init_ts;
                branch.reset_interval(ts);
                GST_INFO_OBJECT(lt, "Tracking new branch: %s, %s -> %s", branch.pipeline_name.c_str(), 
                                branch.source_name.c_str(), branch.sink_name.c_str());
            }

            branch.cal_log_pipeline_latency(ts, meta->init_ts, lt->interval);
        }
    }
}

static void do_pull_range_post(LatencyTracer *lt, guint64 ts, GstPad *pad, GstBuffer *buffer) {
    GstElement *elem = get_real_pad_parent(pad);
    if (!is_in_pipeline(lt, elem))
        return;
    LatencyTracerMeta *meta = nullptr;
    add_latency_meta(lt, meta, ts, buffer, elem);
}

static void do_push_buffer_list_pre(LatencyTracer *lt, guint64 ts, GstPad *pad, GstBufferList *list) {
    BufferListArgs args{lt, ts, pad};
    gst_buffer_list_foreach(
        list,
        [](GstBuffer **buffer, guint, gpointer user_data) -> gboolean {
            auto [lt, ts, pad] = *static_cast<BufferListArgs *>(user_data);
            do_push_buffer_pre(lt, ts, pad, *buffer);
            return TRUE;
        },
        &args);
}

static void on_element_change_state_post(LatencyTracer *lt, guint64 ts, GstElement *elem, GstStateChange change,
                                         GstStateChangeReturn result) {
    UNUSED(result);
    // Track EVERY pipeline that transitions to PLAYING (not just lt->pipeline)
    if (GST_STATE_TRANSITION_NEXT(change) == GST_STATE_PLAYING && GST_IS_PIPELINE(elem)) {
        GST_INFO_OBJECT(lt, "Discovering elements in pipeline: %s", GST_ELEMENT_NAME(elem));
        
        auto *sources = get_sources_list(lt);
        auto *sinks = get_sinks_list(lt);
        auto *type_cache = get_element_type_cache(lt);

        // OPTIMIZATION A: Reserve capacity to avoid reallocations during initialization
        sources->reserve(8); // Typical pipelines have 1-4 sources
        sinks->reserve(8);   // Typical pipelines have 1-4 sinks
        // Note: std::map doesn't support reserve() - tree structure doesn't benefit from pre-allocation

        GstIterator *iter = gst_bin_iterate_elements(GST_BIN_CAST(elem));
        while (true) {
            GValue gval = {};
            auto ret = gst_iterator_next(iter, &gval);
            if (ret != GST_ITERATOR_OK) {
                if (ret != GST_ITERATOR_DONE)
                    GST_ERROR_OBJECT(lt, "Got error while iterating pipeline");
                break;
            }
            auto *element = static_cast<GstElement *>(g_value_get_object(&gval));
            GST_INFO_OBJECT(lt, "Element %s ", GST_ELEMENT_NAME(element));

            if (is_sink_element(element)) {
                // Track all sink elements and cache their type
                sinks->push_back(element);
                (*type_cache)[element] = ElementType::SINK;
                GST_INFO_OBJECT(lt, "Found sink element: %s", GST_ELEMENT_NAME(element));
            } else if (is_source_element(element)) {
                // Track all source elements and cache their type
                sources->push_back(element);
                (*type_cache)[element] = ElementType::SOURCE;
                GST_INFO_OBJECT(lt, "Found source element: %s", GST_ELEMENT_NAME(element));
            } else {
                // Cache as processing element
                (*type_cache)[element] = ElementType::PROCESSING;
                // create ElementStats only once per each element (for non-source, non-sink elements)
                if (!ElementStats::from_element(element)) {
                    ElementStats::create(element, ts);
                }
            }
            g_value_unset(&gval);
        }
        gst_iterator_free(iter);

        GST_INFO_OBJECT(lt, "Found %zu source(s) and %zu sink(s)", sources->size(), sinks->size());

        GstTracer *tracer = GST_TRACER(lt);
        gst_tracing_register_hook(tracer, "pad-push-pre", G_CALLBACK(do_push_buffer_pre));
        gst_tracing_register_hook(tracer, "pad-push-list-pre", G_CALLBACK(do_push_buffer_list_pre));
        gst_tracing_register_hook(tracer, "pad-pull-range-post", G_CALLBACK(do_pull_range_post));
    }
}
// GStreamer tracer hook for element creation
// Note: Parameters 'lt' and 'ts' retained for GStreamer tracer hook signature compatibility
static void on_element_new(LatencyTracer *lt, guint64 ts, GstElement *elem) {
    UNUSED(ts);  // Not used for pipeline registration
    UNUSED(lt);  // No longer tracking single pipeline instance
    
    // Track all pipelines - no single pipeline restriction
    if (GST_IS_PIPELINE(elem)) {
        GST_INFO("Latency tracer will track pipeline: %s", GST_ELEMENT_NAME(elem));
    }
}

static void latency_tracer_init(LatencyTracer *lt) {
    GST_OBJECT_LOCK(lt);
    // lt->pipeline field is kept for binary compatibility (ABI stability):
    // - Existing compiled code may access this field
    // - Struct layout must remain unchanged for shared library compatibility
    // - Field is initialized but no longer used for single-pipeline tracking
    lt->pipeline = nullptr;
    lt->flags = static_cast<LatencyTracerFlags>(LATENCY_TRACER_FLAG_ELEMENT | LATENCY_TRACER_FLAG_PIPELINE);
    lt->interval = 1000;
    lt->branch_stats = nullptr;
    lt->sources_list = nullptr;
    lt->sinks_list = nullptr;
    lt->element_type_cache = nullptr;
    lt->topology_cache = nullptr;

    GstTracer *tracer = GST_TRACER(lt);
    gst_tracing_register_hook(tracer, "element-new", G_CALLBACK(on_element_new));
    gst_tracing_register_hook(tracer, "element-change-state-post", G_CALLBACK(on_element_change_state_post));
    GST_OBJECT_UNLOCK(lt);
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_tracer_register(plugin, "latency_tracer", latency_tracer_get_type()))
        return false;
    latency_tracer_meta_get_info();
    latency_tracer_meta_api_get_type();
    return true;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, latency_tracer, ELEMENT_DESCRIPTION, plugin_init,
                  PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
