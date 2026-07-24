/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <algorithm>
#ifdef __linux__
#include <dirent.h>
#else
#include <filesystem>
#endif
#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <gst/gst.h>
#include <opencv2/opencv.hpp>

#include "dlstreamer/gst/videoanalytics/video_frame.h"
#include "draw_axes.h"

#include <iostream>

#if _MSC_VER
#define popen _popen
#define pclose _pclose
#endif

using namespace std;

#define UNUSED(x) (void)(x)

#ifdef _WIN32
const char os_pathsep(';');
#else
const char os_pathsep(':');
#endif

std::string ToUpperCase(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string FixPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::vector<std::string> SplitString(const std::string input, char delimiter = os_pathsep) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(input);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

#ifdef __linux__
void ExploreDir(const std::string &search_dir, const std::string &model_name, std::vector<std::string> &result) {
    if (auto dir_handle = opendir(search_dir.c_str())) {
        while (auto file_handle = readdir(dir_handle)) {
            if ((!file_handle->d_name) || (file_handle->d_name[0] == '.'))
                continue;
            if (file_handle->d_type == DT_DIR)
                ExploreDir(search_dir + file_handle->d_name + "/", model_name, result);
            if (file_handle->d_type == DT_REG) {
                std::string name(file_handle->d_name);
                if (name == model_name)
                    result.push_back(search_dir + "/" + name);
            }
        }
        closedir(dir_handle);
    }
}
#else
void ExploreDir(const std::string &search_dir, const std::string &model_name, std::vector<std::string> &result) {
    for (const auto &dir_entry : std::filesystem::recursive_directory_iterator(search_dir)) {
        if (dir_entry.path().filename() == model_name)
            result.push_back(dir_entry.path().string());
    }
}
#endif

std::vector<std::string> FindModel(const std::vector<std::string> &search_dirs, const std::string &model_name) {
    std::vector<std::string> result = {};
    for (std::string dir : search_dirs) {
        ExploreDir(dir + "/", model_name, result);
    }
    return result;
}

std::string FindEncoder() {
    const char *inspect_cmd = "gst-inspect-1.0 va | grep vah264enc";
    auto pipe = std::unique_ptr<FILE, int (*)(FILE *)>(popen(inspect_cmd, "r"), pclose);
    if (!pipe) {
        std::cerr << "Error while checking: gst-inspect-1.0 va" << std::endl;
        return "";
    }
    std::array<char, 128> buffer;
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    if (std::strlen(result.c_str()) > 0) {
        return "vah264enc";
    } else {
        return "vah264lpenc";
    }
}

std::map<std::string, std::string> FindModels(const std::vector<std::string> &search_dirs,
                                              const std::vector<std::string> &model_names,
                                              const std::string &precision) {
    std::map<std::string, std::string> result;
    for (std::string model_name : model_names) {
        std::vector<std::string> model_paths = FindModel(search_dirs, model_name);
        if (model_paths.empty())
            throw std::runtime_error("Can't find file for model: " + model_name);
        result[model_name] = model_paths.front();
        // The path to the model must contain the precision (/FP32/ or /INT8/)
        for (auto &model_path : model_paths) {
            // TODO extract precision from xml file
            if (ToUpperCase(model_path).find(ToUpperCase(precision)) != std::string::npos) {
                result[model_name] = model_path;
                break;
            }
        }
    }
    return result;
}

const std::string env_models_path =
    std::string() + (getenv("MODELS_PATH") != NULL ? getenv("MODELS_PATH")
                     : getenv("INTEL_CVSDK_DIR") != NULL
                         ? std::string() + getenv("INTEL_CVSDK_DIR") + "/deployment_tools/intel_models" + "/"
                         : "");

const std::vector<std::string> default_detection_model_names = {"centerface"};

const std::vector<std::string> default_classification_model_names = {
    "dima806_facial_age_image_detection.xml", "dima806_face_emotions_image_detection.xml",
    "dima806_fairface_gender_image_detection.xml"};

gchar const *detection_model = nullptr;
gchar const *classification_models = nullptr;

gchar const *input_file = nullptr;
gchar const *extension = nullptr;
gchar const *device = "CPU";
gchar const *model_precision = "FP32";
gint batch_size = 1;
gdouble threshold = 0.4;
gboolean no_display = FALSE;
gchar const *output_type = "display";

// This structure will be used to pass user data (such as memory type) to the
// callback function.
static GOptionEntry opt_entries[] = {
    {"input", 'i', 0, G_OPTION_ARG_STRING, &input_file, "Path to input video file", NULL},
    {"precision", 'p', 0, G_OPTION_ARG_STRING, &model_precision, "Models precision. Default: FP32", NULL},
    {"detection", 'm', 0, G_OPTION_ARG_STRING, &detection_model, "Path to detection model file", NULL},
    {"classification", 'c', 0, G_OPTION_ARG_STRING, &classification_models,
     "Path to classification models as ',' separated list", NULL},
    {"extension", 'e', 0, G_OPTION_ARG_STRING, &extension, "Path to custom layers extension library", NULL},
    {"device", 'd', 0, G_OPTION_ARG_STRING, &device, "Device to run inference", NULL},
    {"batch", 'b', 0, G_OPTION_ARG_INT, &batch_size, "Batch size", NULL},
    {"threshold", 't', 0, G_OPTION_ARG_DOUBLE, &threshold, "Confidence threshold for detection (0 - 1)", NULL},
    {"no-display", 'n', 0, G_OPTION_ARG_NONE, &no_display, "Run without display", NULL},
    {"output", 'o', 0, G_OPTION_ARG_STRING, &output_type, "Mode of output", NULL},
    GOptionEntry()};

// This structure will be used to pass user data (such as memory type) to the callback function.
// Printing classification results on a frame
// Gets called to notify about the current blocking type
static GstPadProbeReturn pad_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    UNUSED(user_data);

    // Create buffer with data from GstPadProbeInfo
    auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    // Making a buffer writable can fail (for example if it cannot be copied and is used more than once)
    // buffer = gst_buffer_make_writable(buffer);
    // If pad does not contain data then do nothing

    if (buffer == NULL)
        return GST_PAD_PROBE_OK;

    // Get capabilities describing media types currently configured on pad
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        throw std::runtime_error("Can't get current caps");
    // Construct VideoFrame instance from GstBuffer and GstCaps
    // GVA::VideoFrame controls particular inferenced frame and attached
    // GVA::RegionOfInterest and GVA::Tensor instances
    GVA::VideoFrame video_frame(buffer, caps);
    // Get size of region of interest
    gint width = video_frame.video_info()->width;
    gint height = video_frame.video_info()->height;

    // Map buffer and create OpenCV image
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        return GST_PAD_PROBE_OK;
    cv::Mat mat(height, width, CV_8UC4, map.data);

    // Iterate detected objects and all attributes (tensors)
    for (GVA::RegionOfInterest &roi : video_frame.regions()) {
        // Get GstVideoRegionOfInterestMeta from region
        string label;
        float head_angle_r = 0, head_angle_p = 0, head_angle_y = 0;
        auto rect = roi.rect();
        for (auto tensor : roi.tensors()) {
            string model_name = tensor.model_name();
            string layer_name = tensor.layer_name();
            vector<float> data = tensor.data<float>();
            if (layer_name == "align_fc3") {
                static const auto lm_color = cv::Scalar(0, 255, 255);
                for (guint i = 0; i < data.size() / 2; i++) {
                    int x_lm = rect.x + rect.w * data[2 * i];
                    int y_lm = rect.y + rect.h * data[2 * i + 1];
                    cv::circle(mat, cv::Point(x_lm, y_lm), 1 + static_cast<int>(0.012 * rect.w), lm_color, -1);
                }
            }
            if (layer_name == "prob") {
                label += (data[1] > 0.5) ? " M " : " F ";
            }
            if (layer_name == "age_conv3") {
                label += to_string((int)(data[0] * 100));
            }
            if (layer_name == "prob_emotion") {
                static const vector<string> emotionsDesc = {"neutral", "happy", "sad", "surprise", "anger"};
                int index = max_element(begin(data), end(data)) - begin(data);
                label += " " + emotionsDesc[index];
            }
            // Get info for drawing axes
            if (layer_name.find("angle_r") != string::npos) {
                head_angle_r = data[0];
            }
            if (layer_name.find("angle_p") != string::npos) {
                head_angle_p = data[0];
            }
            if (layer_name.find("angle_y") != string::npos) {
                head_angle_y = data[0];
            }
        }
        // Write attributes
        if (!label.empty()) {
            auto pos = cv::Point2f(rect.x, rect.y + rect.h + 30);
            cv::putText(mat, label, pos, cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
        }
        // Draw axes
        if (head_angle_r != 0 && head_angle_p != 0 && head_angle_y != 0) {
            cv::Point3f center(rect.x + rect.w / 2, rect.y + rect.h / 2, 0);
            drawAxes(mat, center, head_angle_r, head_angle_p, head_angle_y, 50);
        }
    }

    // Release the memory previously mapped with gst_buffer_map
    gst_buffer_unmap(buffer, &map);
    // Unref a GstCaps and and free all its structures and the structures' values
    gst_caps_unref(caps);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;

    return GST_PAD_PROBE_OK;
}

// The entry point for the GVA draw_face_attributes sample application
// Sample recieves video with faces as an argument
// If video file is not passed as an argument obviously, an attempt will be made
// to use camera
int main(int argc, char *argv[]) {
    // Parse arguments
    GOptionContext *context = g_option_context_new("sample");
    g_option_context_add_main_entries(context, opt_entries, "sample");
    g_option_context_add_group(context, gst_init_get_option_group());
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        return 1;
    }
    // Construct the pipeline

    // If video file is not passed as an argument, an attempt will be made to use
    // camera
    gchar const *video_source = NULL;
    if (input_file) {
        std::string input_str = input_file;
#ifdef __linux__
        if (input_str.find("/dev/video") != std::string::npos) {
            video_source = g_strdup_printf("v4l2src device=%s", input_file);
#else
        if (input_str.find("?\\\\usb\\#") != std::string::npos) {
            video_source = g_strdup_printf("ksvideosrc device-path=%s", input_file);
#endif
        } else if (input_str.find("://") != std::string::npos) {
            video_source = g_strdup_printf("urisourcebin buffer-size=4096 uri=%s", input_file);
        } else {
            video_source = g_strdup_printf("filesrc location=%s", FixPath(input_str).c_str());
        }
    } else {
#ifdef __linux__
        video_source = "v4l2src device=/dev/video0";
#else
        video_source = "ksvideosrc";
#endif
    }

    if (env_models_path.empty()) {
        std::cerr << "Enviroment variable MODELS_PATH is not set" << std::endl;
        return -1;
    }

    std::map<std::string, std::string> model_paths;
    if (detection_model == NULL) {
        try {
            for (const auto &model_to_path :
                 FindModels(SplitString(env_models_path), default_detection_model_names, model_precision))
                model_paths.emplace(model_to_path);
        } catch (const std::runtime_error &e) {
            std::cerr << e.what() << '\n';
        }
        detection_model = g_strdup(FixPath(model_paths["centerface.xml"]).c_str());
    }
    std::vector<std::string> classification_model_paths;
    if (classification_models == NULL) {
        try {
            for (const auto &model_to_path :
                 FindModels(SplitString(env_models_path), default_classification_model_names, model_precision))
                classification_model_paths.push_back(model_to_path.second);
        } catch (const std::runtime_error &e) {
            std::cerr << e.what() << '\n';
        }

    } else {
        classification_model_paths = SplitString(classification_models, ',');
    }

    std::string classify_str = "";
    for (const auto &path : classification_model_paths)
        classify_str += "gvainference model=" + FixPath(path) + " device=" + device +
                        " batch-size=" + std::to_string(batch_size) + " inference-region=roi-list ! queue ! ";

    gchar const *preprocess_pipeline = "decodebin ! videoconvert n-threads=4 ! videoscale n-threads=4 ";
    gchar const *capfilter = "video/x-raw,format=BGRA";
    gchar const *sink;
    std::string output_type_str = output_type;
    if (output_type_str == "display")
        sink = "autovideosink sync=false";
    else if (output_type_str == "display-and-json")
        sink =
            "gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! autovideosink sync=false";
    else if (output_type_str == "json")
        sink = "gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false";
    else if (output_type_str == "file") {
        std::string encoder = FindEncoder();
        std::string output_location = "cpp_draw_attributes_output_" + std::string(device) + "_" + encoder + ".mp4";
        sink = g_strdup_printf("gvawatermark ! gvafpscounter ! %s ! h264parse ! mp4mux ! filesink location=%s",
                               encoder.c_str(), output_location.c_str());
    } else {
        std::cerr << "Unsupported output type: " << output_type_str << std::endl;
        return -1;
    }

    // Build the pipeline
    auto launch_str = g_strdup_printf(
        "%s ! %s ! capsfilter caps=\"%s\" ! "
        "gvadetect model=%s device=%s batch-size=%d ! queue ! "
        "%s"
        "gvawatermark name=gvawatermark ! capsfilter caps=\"%s\" ! videoconvert n-threads=4 ! gvafpscounter ! %s",
        video_source, preprocess_pipeline, capfilter, detection_model, device, batch_size, classify_str.c_str(),
        capfilter, sink);
    g_print("PIPELINE: %s \n", launch_str);
    GstElement *pipeline = gst_parse_launch(launch_str, NULL);
    g_free(launch_str);

    // set probe callback
    auto gvawatermark = gst_bin_get_by_name(GST_BIN(pipeline), "gvawatermark");
    auto pad = gst_element_get_static_pad(gvawatermark, "src");
    // The provided callback 'pad_probe_callback' is called for every state that
    // matches GST_PAD_PROBE_TYPE_BUFFER to probe buffers
    try {
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, nullptr, nullptr);
    } catch (const std::runtime_error &e) {
        std::cerr << e.what() << '\n';
    }
    gst_object_unref(pad);

    // Start playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Wait until error or EOS
    GstBus *bus = gst_element_get_bus(pipeline);

    int ret_code = 0;

    GstMessage *msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), -1);

    if (msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *err = nullptr;
        gchar *dbg_info = nullptr;

        gst_message_parse_error(msg, &err, &dbg_info);
        g_printerr("ERROR from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
        g_printerr("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");

        g_error_free(err);
        g_free(dbg_info);
        ret_code = -1;
    }

    if (msg)
        gst_message_unref(msg);

    // Free resources
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return ret_code;
}
