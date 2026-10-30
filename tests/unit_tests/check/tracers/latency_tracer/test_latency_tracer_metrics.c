/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * latency_tracer with post-metrics=true: the "dlstreamer/latency-window" messages posted on the bus are checked
 * against the tracer's own latency_tracer_pipeline_interval records, which every field of the message must
 * reproduce.
 */

#include <gst/check/gstcheck.h>
#include <math.h>

#define INTERVAL_MS 100
#define WINDOW_MESSAGE "dlstreamer/latency-window"
#define INTERVAL_RECORD "latency_tracer_pipeline_interval"

static GMutex records_lock;
static GPtrArray *records; /* GstStructure* of latency_tracer_pipeline_interval records */

static void capture_tracer_records(GstDebugCategory *category, GstDebugLevel level, const gchar *file,
                                   const gchar *function, gint line, GObject *object, GstDebugMessage *message,
                                   gpointer user_data) {
    if (level == GST_LEVEL_TRACE && g_strcmp0(gst_debug_category_get_name(category), "GST_TRACER") == 0) {
        const gchar *text = gst_debug_message_get(message);
        if (text && g_str_has_prefix(text, INTERVAL_RECORD ",")) {
            GstStructure *s = gst_structure_from_string(text, NULL);
            if (s) {
                g_mutex_lock(&records_lock);
                g_ptr_array_add(records, s);
                g_mutex_unlock(&records_lock);
            }
        }
        return;
    }
    gst_debug_log_default(category, level, file, function, line, object, message, user_data);
}

/* identity sleeps 2 ms per frame, so every frame has a measurable latency */
static GstElement *make_pipeline(const gchar *name, guint num_buffers) {
    gchar *desc = g_strdup_printf("videotestsrc name=src num-buffers=%u ! "
                                  "video/x-raw,width=64,height=48,framerate=30/1 ! "
                                  "identity sleep-time=2000 ! fakesink name=sink sync=true",
                                  num_buffers);
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    g_free(desc);
    fail_unless(pipeline != NULL, "parse failed: %s", error ? error->message : "?");
    gst_object_set_name(GST_OBJECT(pipeline), name);
    return pipeline;
}

/* Plays the pipeline to EOS, stops it and returns copies of the window messages from its bus. */
static GPtrArray *play_to_eos(GstElement *pipeline) {
    GPtrArray *windows = g_ptr_array_new_with_free_func((GDestroyNotify)gst_structure_free);
    GstBus *bus = gst_element_get_bus(pipeline);
    fail_if(gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
    gboolean done = FALSE;
    while (!done) {
        GstMessage *msg = gst_bus_timed_pop(bus, 20 * GST_SECOND);
        fail_unless(msg != NULL, "%s: no EOS within 20 s", GST_ELEMENT_NAME(pipeline));
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ELEMENT: {
            const GstStructure *s = gst_message_get_structure(msg);
            if (s && gst_structure_has_name(s, WINDOW_MESSAGE)) {
                GstObject *parent = gst_object_get_parent(GST_MESSAGE_SRC(msg));
                fail_unless(parent == GST_OBJECT(pipeline), "window posted by an element of another pipeline");
                gst_object_unref(parent);
                fail_unless_equals_string(GST_MESSAGE_SRC_NAME(msg), "sink");
                g_ptr_array_add(windows, gst_structure_copy(s));
            }
            break;
        }
        case GST_MESSAGE_ERROR:
            fail("%s: pipeline error", GST_ELEMENT_NAME(pipeline));
            break;
        case GST_MESSAGE_EOS:
            done = TRUE;
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }
    fail_if(gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);
    gst_object_unref(bus);
    return windows;
}

static GPtrArray *records_for(const gchar *pipeline_name) {
    GPtrArray *out = g_ptr_array_new();
    g_mutex_lock(&records_lock);
    for (guint i = 0; i < records->len; i++) {
        GstStructure *s = g_ptr_array_index(records, i);
        if (!g_strcmp0(gst_structure_get_string(s, "pipeline_name"), pipeline_name))
            g_ptr_array_add(out, s);
    }
    g_mutex_unlock(&records_lock);
    return out;
}

static guint get_uint(const GstStructure *s, const gchar *field) {
    guint v = 0;
    fail_unless(gst_structure_get_uint(s, field, &v), "missing uint field %s", field);
    return v;
}

static gdouble get_double(const GstStructure *s, const gchar *field) {
    gdouble v = NAN;
    fail_unless(gst_structure_get_double(s, field, &v), "missing double field %s", field);
    return v;
}

/* Tracer records are logged with 6 decimals */
static void assert_close(gdouble expected, gdouble actual, const gchar *what) {
    fail_unless(fabs(expected - actual) <= 1e-5 + 1e-9 * fabs(expected), "%s: expected %f, got %f", what, expected,
                actual);
}

GST_START_TEST(test_window_message_fields) {
    GstElement *pipeline = make_pipeline("fields", 10);
    GPtrArray *windows = play_to_eos(pipeline);
    fail_unless(windows->len > 0, "no window was posted");
    const GstStructure *w = g_ptr_array_index(windows, 0);

    const struct {
        const gchar *name;
        GType type;
    } fields[] = {{"version", G_TYPE_UINT},  {"pipeline", G_TYPE_STRING},  {"source", G_TYPE_STRING},
                  {"sink", G_TYPE_STRING},   {"window_ms", G_TYPE_DOUBLE}, {"count", G_TYPE_UINT},
                  {"sum_ms", G_TYPE_DOUBLE}, {"min_ms", G_TYPE_DOUBLE},    {"max_ms", G_TYPE_DOUBLE}};
    fail_unless_equals_int(gst_structure_n_fields(w), G_N_ELEMENTS(fields));
    for (guint i = 0; i < G_N_ELEMENTS(fields); i++)
        fail_unless(gst_structure_get_field_type(w, fields[i].name) == fields[i].type, "field %s has type %s",
                    fields[i].name, g_type_name(gst_structure_get_field_type(w, fields[i].name)));

    fail_unless_equals_int(get_uint(w, "version"), 1);
    fail_unless_equals_string(gst_structure_get_string(w, "pipeline"), "fields");
    fail_unless_equals_string(gst_structure_get_string(w, "source"), "src");
    fail_unless_equals_string(gst_structure_get_string(w, "sink"), "sink");

    g_ptr_array_unref(windows);
    gst_object_unref(pipeline);
}
GST_END_TEST;

/* Every field of a latency_tracer_pipeline_interval record can be derived from the matching message. */
GST_START_TEST(test_window_messages_match_tracer_records) {
    GstElement *pipeline = make_pipeline("single", 30);
    GPtrArray *windows = play_to_eos(pipeline);
    GPtrArray *recs = records_for("single");

    fail_unless(windows->len >= 5, "only %u windows", windows->len);
    fail_unless_equals_int(windows->len, recs->len);

    guint frames = 0;
    for (guint i = 0; i < windows->len; i++) {
        const GstStructure *w = g_ptr_array_index(windows, i);
        const GstStructure *r = g_ptr_array_index(recs, i);
        guint count = get_uint(w, "count");
        gdouble window_ms = get_double(w, "window_ms");
        gdouble sum_ms = get_double(w, "sum_ms");
        gdouble min_ms = get_double(w, "min_ms");
        gdouble max_ms = get_double(w, "max_ms");
        frames += count;

        fail_unless(count > 0);
        fail_unless(window_ms >= INTERVAL_MS, "window %u spans %f ms", i, window_ms);
        fail_unless(min_ms > 1.0, "identity sleeps 2 ms per frame, min was %f", min_ms);
        fail_unless(min_ms <= sum_ms / count && sum_ms / count <= max_ms);

        assert_close(get_double(r, "interval"), window_ms, "interval");
        assert_close(get_double(r, "avg"), sum_ms / count, "avg");
        assert_close(get_double(r, "min"), min_ms, "min");
        assert_close(get_double(r, "max"), max_ms, "max");
        assert_close(get_double(r, "latency"), window_ms / count, "latency");
        assert_close(get_double(r, "fps"), 1000.0 / (window_ms / count), "fps");
    }
    fail_unless(frames <= 30, "windows counted %u frames for 30 buffers", frames);

    g_ptr_array_unref(recs);
    g_ptr_array_unref(windows);
    gst_object_unref(pipeline);
}
GST_END_TEST;

typedef struct {
    GstElement *pipeline;
    GPtrArray *windows;
} PipelineRun;

static gpointer play_in_thread(gpointer data) {
    PipelineRun *run = (PipelineRun *)data;
    run->windows = play_to_eos(run->pipeline);
    return NULL;
}

/* Each pipeline in a process receives only the windows of its own branches, on its own bus. */
GST_START_TEST(test_concurrent_pipelines_get_own_windows) {
    PipelineRun runs[] = {{make_pipeline("left", 15), NULL}, {make_pipeline("right", 15), NULL}};
    GThread *threads[G_N_ELEMENTS(runs)];
    for (guint i = 0; i < G_N_ELEMENTS(runs); i++)
        threads[i] = g_thread_new("pipeline", play_in_thread, &runs[i]);
    for (guint i = 0; i < G_N_ELEMENTS(runs); i++)
        g_thread_join(threads[i]);

    for (guint i = 0; i < G_N_ELEMENTS(runs); i++) {
        const gchar *name = GST_ELEMENT_NAME(runs[i].pipeline);
        GPtrArray *windows = runs[i].windows;
        fail_unless(windows->len > 0, "%s: no window was posted", name);
        guint frames = 0;
        for (guint j = 0; j < windows->len; j++) {
            const GstStructure *w = g_ptr_array_index(windows, j);
            fail_unless_equals_string(gst_structure_get_string(w, "pipeline"), name);
            frames += get_uint(w, "count");
        }
        fail_unless(frames <= 15, "%s: windows counted %u frames for 15 buffers", name, frames);
        g_ptr_array_unref(windows);
        gst_object_unref(runs[i].pipeline);
    }
}
GST_END_TEST;

static Suite *latency_tracer_metrics_suite(void) {
    Suite *s = suite_create("latency_tracer_metrics");
    TCase *tc = tcase_create("general");
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_window_message_fields);
    tcase_add_test(tc, test_window_messages_match_tracer_records);
    tcase_add_test(tc, test_concurrent_pipelines_get_own_windows);
    return s;
}

int main(int argc, char **argv) {
    /* Tracers are created by gst_init(), so they have to be requested before it runs. */
    g_setenv("GST_TRACERS", "latency_tracer(flags=pipeline,interval=100,post-metrics=true)", TRUE);
    gst_check_init(&argc, &argv);

    records = g_ptr_array_new_with_free_func((GDestroyNotify)gst_structure_free);
    gst_debug_remove_log_function(gst_debug_log_default);
    gst_debug_add_log_function(capture_tracer_records, NULL, NULL);
    gst_debug_set_threshold_for_name("GST_TRACER", GST_LEVEL_TRACE);

    int failed = gst_check_run_suite(latency_tracer_metrics_suite(), "latency_tracer_metrics", __FILE__);
    g_ptr_array_unref(records);
    return failed;
}
