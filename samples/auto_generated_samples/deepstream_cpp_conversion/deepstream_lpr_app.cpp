/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

//
// DL Streamer License Plate Recognition — DeepStream LPR conversion.
//
// Converted from:
//   NVIDIA DeepStream deepstream_lpr_app (C++)
//   https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps/tree/master/apps/tao_others/deepstream_lpr_app
//
// Pipeline:
//     filesrc → decodebin3 →
//     gvadetect (license plate detection) → queue →
//     gvatrack (object tracking) →
//     gvaclassify (PaddleOCR text recognition) → queue →
//     gvafpscounter → gvawatermark →
//     gvametaconvert → gvametapublish (JSON Lines) →
//     videoconvert → vah264enc → h264parse → mp4mux → filesink
//

#include <gst/analytics/analytics.h>
#include <gst/gst.h>

#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static GstElement *pipeline = nullptr;

// ── Performance measurement (mirrors DeepStream perf_measure) ───────────────

struct PerfMeasure {
    gint64 pre_time = -1;
    gint64 total_time = 0;
    guint count = 0;
};

// ── Counters (mirrors DeepStream frame_number / total_plate_number) ─────────

static gint frame_number = 0;
static gint total_plate_number = 0;

// ── CLI argument parsing (replaces DeepStream YAML config parsing) ──────────

struct AppArgs {
    std::string input;
    std::string device;
    std::string output_video;
    std::string output_json;
    float threshold;
    bool display;
};

static AppArgs parse_args(int argc, char *argv[]) {
    AppArgs args;
    args.device = "GPU";
    args.threshold = 0.5f;
    args.display = false;

    fs::path script_dir = fs::path(argv[0]).parent_path();
    if (script_dir.empty())
        script_dir = ".";
    args.output_video = (script_dir / "results" / "output.mp4").string();
    args.output_json = (script_dir / "results" / "results.jsonl").string();

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--input" && i + 1 < argc) {
            args.input = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            args.device = argv[++i];
        } else if (arg == "--output-video" && i + 1 < argc) {
            args.output_video = argv[++i];
        } else if (arg == "--output-json" && i + 1 < argc) {
            args.output_json = argv[++i];
        } else if (arg == "--threshold" && i + 1 < argc) {
            args.threshold = std::stof(argv[++i]);
        } else if (arg == "--display") {
            args.display = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --input <path>         Video file path (required)\n"
                      << "  --device <device>      Inference device (default: GPU)\n"
                      << "  --output-video <path>  Output video path\n"
                      << "  --output-json <path>   Output JSON Lines path\n"
                      << "  --threshold <float>    Detection confidence (default: 0.5)\n"
                      << "  --display              Show output in a display window\n";
            std::exit(0);
        }
    }

    if (args.input.empty()) {
        std::cerr << "Error: --input is required\n";
        std::exit(1);
    }
    return args;
}

static std::string validate_input(const std::string &source) {
    if (source.rfind("rtsp://", 0) == 0)
        return source;
    if (!fs::exists(source)) {
        std::cerr << "Error: file not found: " << source << "\n";
        std::exit(1);
    }
    return fs::absolute(source).string();
}

static std::string find_model(const fs::path &models_dir, const std::string &glob_pattern, const std::string &label) {
    for (auto &entry : fs::recursive_directory_iterator(models_dir)) {
        std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".xml" && name.find(glob_pattern) != std::string::npos) {
            return entry.path().string();
        }
    }
    std::cerr << "Error: " << label << " model not found in " << models_dir << ". Run: python3 export_models.py\n";
    std::exit(1);
}

static std::string check_device(const std::string &requested, const std::string &label) {
    std::string device = requested;
    if (device == "NPU" && !fs::exists("/dev/accel/accel0")) {
        std::cout << "Warning: NPU not available for " << label << ", falling back to GPU\n";
        device = "GPU";
    }
    if (device == "GPU" && !fs::exists("/dev/dri/renderD128")) {
        std::cout << "Warning: GPU not available for " << label << ", falling back to CPU\n";
        device = "CPU";
    }
    return device;
}

static std::string build_source(const std::string &src) {
    if (src.rfind("rtsp://", 0) == 0)
        return "rtspsrc location=" + src + " latency=100";
    return "filesrc location=\"" + src + "\"";
}

// ── Pad probe callback ─────────────────────────────────────────────────────
// Mirrors DeepStream osd_sink_pad_buffer_probe: counts vehicles, plates,
// and prints recognized license plate text.

static GstPadProbeReturn watermark_sink_pad_buffer_probe(GstPad * /*pad*/, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;

    PerfMeasure *perf = static_cast<PerfMeasure *>(user_data);
    gint64 now = g_get_monotonic_time();

    if (perf->pre_time < 0) {
        perf->pre_time = now;
    } else {
        perf->total_time += (now - perf->pre_time);
        perf->pre_time = now;
        perf->count++;
    }

    guint plate_count = 0;

    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
    if (rmeta) {
        gpointer state = NULL;
        GstAnalyticsODMtd od_mtd;

        while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(), &od_mtd)) {
            GQuark obj_type = gst_analytics_od_mtd_get_obj_type(&od_mtd);
            const gchar *label = g_quark_to_string(obj_type);

            if (label) {
                plate_count++;
            }

            // Check for classification results (OCR text) attached to this detection
            GstAnalyticsClsMtd cls_mtd;
            gpointer cls_state = NULL;
            if (gst_analytics_relation_meta_get_direct_related(rmeta, od_mtd.id, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                               gst_analytics_cls_mtd_get_mtd_type(), &cls_state,
                                                               &cls_mtd)) {
                if (gst_analytics_cls_mtd_get_length(&cls_mtd) > 0) {
                    gfloat confidence = gst_analytics_cls_mtd_get_level(&cls_mtd, 0);
                    GQuark cls_quark = gst_analytics_cls_mtd_get_quark(&cls_mtd, 0);
                    const gchar *plate_text = g_quark_to_string(cls_quark);
                    if (plate_text && strlen(plate_text) > 0) {
                        g_print("Plate License: %s (confidence: %.2f)\n", plate_text, confidence);
                    }
                }
            }
        }
    }

    g_print("Frame Number = %d  License Plate Count = %d\n", frame_number, plate_count);
    frame_number++;
    total_plate_number += plate_count;

    return GST_PAD_PROBE_OK;
}

// ── SIGINT → EOS (mirrors DeepStream bus_call GST_MESSAGE_EOS) ─────────────

static void sigint_handler(int /*signum*/) {
    if (pipeline)
        gst_element_send_event(pipeline, gst_event_new_eos());
}

// ── Pipeline event loop ─────────────────────────────────────────────────────

static void run_pipeline(GstElement *pipe, PerfMeasure *perf, guint src_cnt) {
    signal(SIGINT, sigint_handler);
    GstBus *bus = gst_element_get_bus(pipe);

    std::cout << "[pipeline] Compiling models, this may take some time...\n";
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    bool running = true;
    while (running) {
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
                                                     static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (!msg)
            continue;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::cerr << "Error from " << GST_OBJECT_NAME(msg->src) << ": " << err->message << "\n";
            if (dbg)
                std::cerr << "Debug: " << dbg << "\n";
            g_error_free(err);
            g_free(dbg);
            running = false;
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "End of stream\n";
            running = false;
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    gst_element_set_state(pipe, GST_STATE_NULL);

    // Print performance stats (mirrors DeepStream perf output)
    if (perf->count > 1) {
        double avg_fps = ((perf->count - 1) * src_cnt * 1000000.0) / perf->total_time;
        std::cout << "Average fps: " << avg_fps << "\n";
    }
    std::cout << "Totally " << total_plate_number << " plates are inferred\n";

    signal(SIGINT, SIG_DFL);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    AppArgs args = parse_args(argc, argv);

    std::string input_src = validate_input(args.input);

    // Locate models
    fs::path script_dir = fs::path(argv[0]).parent_path();
    if (script_dir.empty())
        script_dir = ".";
    fs::path models_dir = script_dir / "models";
    if (!fs::exists(models_dir)) {
        std::cerr << "Error: models/ directory not found. Run: python3 export_models.py\n";
        return 1;
    }

    std::string detect_model = find_model(models_dir, "license-plate-finetune", "detection");
    std::string ocr_model = find_model(models_dir, "PP-OCRv5_server_rec", "OCR");

    // Output directories
    fs::create_directories(fs::path(args.output_video).parent_path());
    fs::create_directories(fs::path(args.output_json).parent_path());

    // Device fallback
    std::string device = check_device(args.device, "inference");

    // Initialize GStreamer
    gst_init(&argc, &argv);

    // Build pipeline string
    std::string source_el = build_source(input_src);

    std::string sink_str;
    if (args.display) {
        sink_str = "videoconvert ! autovideosink";
    } else {
        sink_str = "videoconvert ! vah264enc ! h264parse ! "
                   "mp4mux fragment-duration=1000 ! "
                   "filesink location=\"" +
                   args.output_video + "\"";
    }

    std::string pipe_str = source_el +
                           " ! decodebin3 caps=\"video/x-raw(ANY)\" ! "
                           "gvadetect model=\"" +
                           detect_model + "\" device=" + device +
                           " batch-size=4 threshold=" + std::to_string(args.threshold) +
                           " ! queue ! "
                           "gvatrack tracking-type=zero-term-imageless ! "
                           "gvaclassify model=\"" +
                           ocr_model + "\" device=" + device +
                           " batch-size=4 ! queue ! "
                           "gvafpscounter ! gvawatermark name=watermark ! "
                           "gvametaconvert ! "
                           "gvametapublish file-format=json-lines file-path=\"" +
                           args.output_json + "\" ! " + sink_str;

    std::cout << "\nPipeline:\n" << pipe_str << "\n\n";

    GError *error = nullptr;
    pipeline = gst_parse_launch(pipe_str.c_str(), &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << (error ? error->message : "unknown error") << "\n";
        if (error)
            g_error_free(error);
        return 1;
    }
    if (error) {
        std::cerr << "Pipeline warning: " << error->message << "\n";
        g_error_free(error);
    }

    // Attach pad probe to watermark sink pad (mirrors DeepStream osd_sink_pad probe)
    PerfMeasure perf;
    GstElement *watermark = gst_bin_get_by_name(GST_BIN(pipeline), "watermark");
    if (watermark) {
        GstPad *sink_pad = gst_element_get_static_pad(watermark, "sink");
        if (sink_pad) {
            gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, watermark_sink_pad_buffer_probe, &perf, nullptr);
            gst_object_unref(sink_pad);
        }
        gst_object_unref(watermark);
    }

    run_pipeline(pipeline, &perf, 1);

    std::cout << "\nOutput video: " << args.output_video << "\n";
    std::cout << "Output JSON:  " << args.output_json << "\n";

    gst_object_unref(pipeline);
    pipeline = nullptr;

    return 0;
}
