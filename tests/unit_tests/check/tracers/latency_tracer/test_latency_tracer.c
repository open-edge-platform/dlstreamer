/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * latency_tracer must log at most one latency_tracer_pipeline record per buffer that reaches a sink,
 * no matter how many pipelines in the process have reached PLAYING before. Every check compares the
 * number of records for one pipeline run against the buffers its fakesink received.
 *
 * The tracer discovers a pipeline's elements when the pipeline itself has changed to PLAYING, which
 * is after its children have started streaming, so the first buffer or two of a run may not be
 * recorded. Buffers are therefore paced by the clock and the lower bound is loose; the upper bound,
 * which is what catches double counting, is exact.
 */

#include <gst/check/gstcheck.h>

#define NUM_BUFFERS 30
#define PIPELINE_RECORD "latency_tracer_pipeline"

static GMutex records_lock;
static GHashTable *records; /* pipeline name -> number of latency_tracer_pipeline records */

/* Counts the tracer's per-frame records and hands every other message to the default handler. */
static void count_tracer_records(GstDebugCategory *category, GstDebugLevel level, const gchar *file,
                                 const gchar *function, gint line, GObject *object, GstDebugMessage *message,
                                 gpointer user_data) {
    if (level == GST_LEVEL_TRACE && g_strcmp0(gst_debug_category_get_name(category), "GST_TRACER") == 0) {
        const gchar *text = gst_debug_message_get(message);
        if (text && g_str_has_prefix(text, PIPELINE_RECORD ",")) {
            GstStructure *s = gst_structure_from_string(text, NULL);
            if (s) {
                const gchar *name = gst_structure_get_string(s, "pipeline_name");
                g_mutex_lock(&records_lock);
                guint n = GPOINTER_TO_UINT(g_hash_table_lookup(records, name));
                g_hash_table_replace(records, g_strdup(name), GUINT_TO_POINTER(n + 1));
                g_mutex_unlock(&records_lock);
                gst_structure_free(s);
            }
        }
        return;
    }
    gst_debug_log_default(category, level, file, function, line, object, message, user_data);
}

static guint take_records(const gchar *pipeline_name) {
    g_mutex_lock(&records_lock);
    guint n = GPOINTER_TO_UINT(g_hash_table_lookup(records, pipeline_name));
    g_hash_table_remove(records, pipeline_name);
    g_mutex_unlock(&records_lock);
    return n;
}

static void count_handoff(GstElement *sink, GstBuffer *buffer, GstPad *pad, gpointer user_data) {
    (void)sink;
    (void)buffer;
    (void)pad;
    g_atomic_int_inc((gint *)user_data);
}

static GstElement *make_pipeline(const gchar *name, gboolean live, gint *handoffs) {
    gchar *desc = g_strdup_printf("videotestsrc is-live=%s num-buffers=%d ! "
                                  "video/x-raw,width=64,height=48,framerate=100/1 ! "
                                  "fakesink name=sink sync=true signal-handoffs=true",
                                  live ? "true" : "false", NUM_BUFFERS);
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    g_free(desc);
    fail_unless(pipeline != NULL, "parse failed: %s", error ? error->message : "?");
    gst_object_set_name(GST_OBJECT(pipeline), name);

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    g_signal_connect(sink, "handoff", G_CALLBACK(count_handoff), handoffs);
    gst_object_unref(sink);
    return pipeline;
}

/* Plays the pipeline to EOS, stops it in READY and checks the records logged during this run. */
static void run_and_check(GstElement *pipeline, gint *handoffs) {
    const gchar *name = GST_ELEMENT_NAME(pipeline);
    g_atomic_int_set(handoffs, 0);
    fail_if(gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    fail_unless(msg != NULL, "%s: no EOS within 10 s", name);
    fail_unless(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS, "%s: pipeline error", name);
    gst_message_unref(msg);
    gst_object_unref(bus);
    fail_if(gst_element_set_state(pipeline, GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

    guint buffers = (guint)g_atomic_int_get(handoffs);
    guint n = take_records(name);
    fail_unless_equals_int(buffers, NUM_BUFFERS);
    fail_unless(n <= buffers, "%s: %u tracer records for %u buffers, some buffers were counted twice", name, n,
                buffers);
    fail_unless(n >= buffers / 2, "%s: only %u tracer records for %u buffers", name, n, buffers);
}

/* A live pipeline goes PAUSED -> PLAYING twice per start: ASYNC first, then again after async-done. */
GST_START_TEST(test_live_pipeline_records_each_buffer_once) {
    gint handoffs = 0;
    GstElement *pipeline = make_pipeline("live", TRUE, &handoffs);
    run_and_check(pipeline, &handoffs);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}
GST_END_TEST;

/* Pipelines that ran earlier in the same process must not change how later ones are counted. */
GST_START_TEST(test_sequential_pipelines_record_each_buffer_once) {
    const gchar *names[] = {"first", "second", "third"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        gint handoffs = 0;
        GstElement *pipeline = make_pipeline(names[i], FALSE, &handoffs);
        run_and_check(pipeline, &handoffs);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}
GST_END_TEST;

/* Restarting a pipeline must not change how its buffers are counted. */
GST_START_TEST(test_restarted_pipeline_records_each_buffer_once) {
    gint handoffs = 0;
    GstElement *pipeline = make_pipeline("restarted", FALSE, &handoffs);
    run_and_check(pipeline, &handoffs);
    run_and_check(pipeline, &handoffs);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}
GST_END_TEST;

static Suite *latency_tracer_suite(void) {
    Suite *s = suite_create("latency_tracer");
    TCase *tc = tcase_create("general");
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_live_pipeline_records_each_buffer_once);
    tcase_add_test(tc, test_sequential_pipelines_record_each_buffer_once);
    tcase_add_test(tc, test_restarted_pipeline_records_each_buffer_once);
    return s;
}

int main(int argc, char **argv) {
    /* Tracers are created by gst_init(), so they have to be requested before it runs. */
    g_setenv("GST_TRACERS", "latency_tracer(flags=pipeline)", TRUE);
    gst_check_init(&argc, &argv);

    records = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gst_debug_remove_log_function(gst_debug_log_default);
    gst_debug_add_log_function(count_tracer_records, NULL, NULL);
    gst_debug_set_threshold_for_name("GST_TRACER", GST_LEVEL_TRACE);

    return gst_check_run_suite(latency_tracer_suite(), "latency_tracer", __FILE__);
}
