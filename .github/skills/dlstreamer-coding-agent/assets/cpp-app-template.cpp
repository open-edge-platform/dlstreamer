/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

//
// DL Streamer <APPLICATION_NAME> pipeline.
//
// Pipeline:
//     filesrc → decodebin3 →
//     gvadetect → gvafpscounter → gvawatermark →
//     gvametaconvert → gvametapublish (JSON Lines) →
//     videoconvert → vah264enc → h264parse → mp4mux → filesink
//
// Supports file and RTSP IP camera inputs.
//

#include <gst/gst.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <string>

namespace fs = std::filesystem;

static GstElement *pipeline = nullptr;

static const char *DEFAULT_VIDEO = "<VIDEO_PATH>";

// ── helpers ──────────────────────────────────────────────────────────────────

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
    args.input = DEFAULT_VIDEO;
    args.device = "GPU";
    args.threshold = 0.5f;
    args.display = false;

    fs::path script_dir = fs::path(argv[0]).parent_path();
    if (script_dir.empty())
        script_dir = ".";
    args.output_video = (script_dir / "results" / "output.mp4").string();
    args.output_json = (script_dir / "results" / "results.jsonl").string();

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--input" && i + 1 < argc) {
            args.input = argv[++i];
        } else if (std::string(argv[i]) == "--device" && i + 1 < argc) {
            args.device = argv[++i];
        } else if (std::string(argv[i]) == "--output-video" && i + 1 < argc) {
            args.output_video = argv[++i];
        } else if (std::string(argv[i]) == "--output-json" && i + 1 < argc) {
            args.output_json = argv[++i];
        } else if (std::string(argv[i]) == "--threshold" && i + 1 < argc) {
            args.threshold = std::stof(argv[++i]);
        } else if (std::string(argv[i]) == "--display") {
            args.display = true;
        } else if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --input <path|rtsp://URI>  Video input (default: " << DEFAULT_VIDEO << ")\n"
                      << "  --device <device>          Inference device (default: GPU)\n"
                      << "  --output-video <path>      Output video path\n"
                      << "  --output-json <path>       Output JSON path\n"
                      << "  --threshold <float>        Detection confidence threshold (default: 0.5)\n"
                      << "  --display                  Show output in a display window\n";
            std::exit(0);
        }
    }
    return args;
}

static std::string validate_input(const std::string &source) {
    if (source.rfind("rtsp://", 0) == 0) {
        return source;
    }
    if (!fs::exists(source)) {
        std::cerr << "Error: file not found: " << source << "\n";
        std::exit(1);
    }
    return fs::absolute(source).string();
}

static std::string find_model(const fs::path &models_dir, const std::string &extension, const std::string &label) {
    for (auto &entry : fs::recursive_directory_iterator(models_dir)) {
        if (entry.path().extension() == extension) {
            return entry.path().string();
        }
    }
    std::cerr << "Error: " << label << " model not found. Run: python3 export_models.py\n";
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
    if (src.rfind("rtsp://", 0) == 0) {
        return "rtspsrc location=" + src + " latency=100";
    }
    return "filesrc location=\"" + src + "\"";
}

// ── SIGINT → EOS ────────────────────────────────────────────────────────────

static void sigint_handler(int /*signum*/) {
    if (pipeline) {
        gst_element_send_event(pipeline, gst_event_new_eos());
    }
}

// ── pipeline event loop ─────────────────────────────────────────────────────

static void run_pipeline(GstElement *pipe) {
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
            std::cout << "Pipeline complete.\n";
            running = false;
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    gst_element_set_state(pipe, GST_STATE_NULL);
    signal(SIGINT, SIG_DFL);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    AppArgs args = parse_args(argc, argv);

    // Validate input
    std::string input_src = validate_input(args.input);

    // Locate models
    fs::path script_dir = fs::path(argv[0]).parent_path();
    if (script_dir.empty())
        script_dir = ".";
    fs::path models_dir = script_dir / "models";
    std::string model_xml = find_model(models_dir, ".xml", "detection");

    // Output dirs
    fs::create_directories(fs::path(args.output_video).parent_path());
    fs::create_directories(fs::path(args.output_json).parent_path());

    // Device fallback
    std::string device = check_device(args.device, "inference");

    // Build pipeline string
    gst_init(&argc, &argv);

    std::string source_el = build_source(input_src);

    std::string sink_str;
    if (args.display) {
        sink_str = "videoconvert ! autovideosink";
    } else {
        std::string encoder;
        if (device == "CPU") {
            encoder = "videoconvert ! x264enc tune=zerolatency";
        } else {
            encoder = "videoconvert ! vah264enc";
        }
        sink_str = encoder +
                   " ! h264parse ! mp4mux ! "
                   "filesink location=\"" +
                   args.output_video + "\"";
    }

    std::string pipe_str = source_el +
                           " ! decodebin3 caps=\"video/x-raw(ANY)\" ! "
                           "gvadetect model=\"" +
                           model_xml + "\" device=" + device +
                           " batch-size=4 threshold=" + std::to_string(args.threshold) +
                           " ! queue ! "
                           "gvafpscounter ! gvawatermark ! "
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

    run_pipeline(pipeline);

    std::cout << "\nOutput video: " << args.output_video << "\n";
    std::cout << "Output JSON:  " << args.output_json << "\n";

    gst_object_unref(pipeline);
    pipeline = nullptr;

    return 0;
}
