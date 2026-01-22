/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "model_api_converters.h"

#include "utils.h"
#include <fstream>
#include <regex>

namespace ModelApiConverters {

// a helper function to convert YAML file to JSON format
// This is a basic conversion for common YAML formats used in YOLO model metadata files
// For full YAML support, consider using a dedicated YAML library
bool yaml2Json(const std::string yaml_file, nlohmann::json &yaml_json) {
    std::ifstream file(yaml_file);
    if (!file.is_open()) {
        GST_ERROR("Failed to open yaml file: %s", yaml_file.c_str());
        return false;
    }

    try {
        // Parse YAML file
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string yaml_content = buffer.str();

        std::istringstream yaml_stream(yaml_content);
        std::string line;
        std::string current_key;

        while (std::getline(yaml_stream, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
                continue;

            // Remove leading/trailing whitespace
            size_t first = line.find_first_not_of(" \t");
            size_t last = line.find_last_not_of(" \t\r\n");
            if (first == std::string::npos)
                continue;
            line = line.substr(first, last - first + 1);

            // Parse key-value pairs
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);

                // Read value
                size_t val_start = value.find_first_not_of(" \t");
                if (val_start != std::string::npos) {
                    value = value.substr(val_start);
                    yaml_json[key] = value;
                } else {
                    // Check if value is empty (array follows)
                    if (value.empty()) {
                        nlohmann::json array = nlohmann::json::array();
                        // Read array items
                        while (std::getline(yaml_stream, line)) {
                            size_t item_first = line.find_first_not_of(" \t");
                            if (item_first == std::string::npos || line[item_first] != '-')
                                break;

                            size_t item_start = line.find_first_not_of(" \t", item_first + 1);
                            if (item_start != std::string::npos) {
                                std::string item = line.substr(item_start);
                                size_t item_end = item.find_last_not_of(" \t\r\n");
                                if (item_end != std::string::npos)
                                    item = item.substr(0, item_end + 1);
                                array.push_back(item);
                            }
                        }
                        yaml_json[key] = array;
                    }
                }
            }
        }

    } catch (const std::exception &e) {
        GST_ERROR("Failed to parse YAML file: %s", e.what());
        return false;
    }

    return true;
}

// Convert input YOLO metadata file into Model API format
bool convertYoloMeta2ModelApi(const std::string model_file, ov::AnyMap &modelConfig) {
    const std::vector<std::pair<std::string, std::string>> model_types = {{"YOLOv8", "yolo_v8"},
                                                                          {"YOLOv9", "yolo_v8"},
                                                                          {"YOLOv10", "yolo_v10"},
                                                                          {"YOLO11", "yolo_v8"},
                                                                          {"YOLO26", "yolo_v26"}};
    const std::vector<std::pair<std::string, std::string>> task_types = {
        {"detect", ""}, {"segment", "_seg"}, {"pose", "_pose"}, {"obb", "_obb"}};

    std::filesystem::path metadata_file(model_file);
    metadata_file.replace_filename("metadata.yaml");
    nlohmann::json yaml_json;

    if (!std::filesystem::exists(metadata_file))
        return false;

    if (!yaml2Json(metadata_file.string(), yaml_json))
        return false;

    GST_INFO("Parsing YOLO metadata file: %s", metadata_file.c_str());

    // derive model type from description and model task
    std::string model_type = "";
    bool type_found = false;
    for (const auto &model_type_pair : model_types) {
        std::string description = yaml_json.contains("description") && yaml_json["description"].is_string()
                                      ? yaml_json["description"].get<std::string>()
                                      : "";
        if (!description.empty() && description.find(model_type_pair.first) != std::string::npos) {
            model_type = model_type_pair.second;
            type_found = true;
            break;
        }
    }
    if (!type_found && yaml_json.contains("description") && yaml_json["description"].is_string()) {
        throw std::runtime_error("Unsupported YOLO model type: " + yaml_json["description"].get<std::string>());
        return false;
    }

    bool task_found = false;
    for (const auto &task_type_pair : task_types) {
        std::string task =
            yaml_json.contains("task") && yaml_json["task"].is_string() ? yaml_json["task"].get<std::string>() : "";
        if (!task.empty() && task.find(task_type_pair.first) != std::string::npos) {
            model_type = model_type + task_type_pair.second;
            task_found = true;
            break;
        }
    }
    if (!task_found && yaml_json.contains("task") && yaml_json["task"].is_string()) {
        throw std::runtime_error("Unsupported YOLO model task: " + yaml_json["task"].get<std::string>());
        return false;
    }

    if (!model_type.empty()) {
        modelConfig["model_type"] = ov::Any(model_type);
    }

    // set reshape size if model is dynamic
    std::string dynamic = yaml_json.contains("dynamic") && yaml_json["dynamic"].is_string()
                              ? yaml_json["dynamic"].get<std::string>()
                              : "";
    if (dynamic == "true") {
        std::vector<int> imgsz;
        if (yaml_json.contains("imgsz") && yaml_json["imgsz"].is_array()) {
            for (const auto &val : yaml_json["imgsz"]) {
                if (val.is_string()) {
                    int value = std::stoi(val.get<std::string>());
                    imgsz.push_back(value);
                }
            }
        }
        if (imgsz.size() == 2)
            modelConfig["reshape"] = ov::Any(imgsz);
        else
            GST_ERROR("Unexpected reshape size: %ld", imgsz.size());
    }

    return true;
}

// Convert third-party input metadata config files into Model API format
bool convertThirdPartyModelConfig(const std::string model_file, ov::AnyMap &modelConfig) {
    bool updated = false;

    if (!modelConfig.empty()) {
        if (modelConfig["model_type"] == "YOLO") {
            updated = convertYoloMeta2ModelApi(model_file, modelConfig);
        }
    }

    return updated;
}

// Helper function to extract all numbers from a string
std::vector<std::string> extractNumbers(const std::string &s) {
    // Regular expression to match numbers, including negative and floating-point numbers
    std::regex re(R"([-+]?\d*\.?\d+)");
    std::sregex_iterator begin(s.begin(), s.end(), re);
    std::sregex_iterator end;

    std::vector<std::string> numbers;
    for (std::sregex_iterator i = begin; i != end; ++i) {
        numbers.push_back(i->str());
    }

    return numbers;
}

// Helper function to split a string by multiple delimiters
std::vector<std::string> split(const std::string &s, const std::string &delimiters) {
    std::regex re("[" + delimiters + "]+");
    std::sregex_token_iterator first{s.begin(), s.end(), re, -1}, last;
    return {first, last};
}

// Parse Model API metadata and return pre-processing GstStructure
std::map<std::string, GstStructure *> get_model_info_preproc(const std::shared_ptr<ov::Model> model,
                                                             const std::string model_file,
                                                             const gchar *pre_proc_config) {
    std::map<std::string, GstStructure *> res;
    std::string layer_name("ANY");
    GstStructure *s = nullptr;
    ov::AnyMap modelConfig;

    // Warn if model quantization runtime does not match current runtime
    if (model->has_rt_info({"nncf"})) {
        const ov::AnyMap nncfConfig = model->get_rt_info<const ov::AnyMap>("nncf");
        const std::string modelVersion = model->get_rt_info<const std::string>("Runtime_version");
        const std::string runtimeVersion = ov::get_openvino_version().buildNumber;

        if (nncfConfig.count("quantization") && (modelVersion != runtimeVersion))
            g_warning("Model quantization runtime (%s) does not match current runtime (%s). Results may be "
                      "inaccurate. Please re-quantize the model with the current runtime version.",
                      modelVersion.c_str(), runtimeVersion.c_str());
    }

    if (model->has_rt_info({"model_info"})) {
        modelConfig = model->get_rt_info<ov::AnyMap>("model_info");
        s = gst_structure_new_empty(layer_name.data());
    }

    // override model config with command line pre-processing parameters if provided
    auto pre_proc_params = Utils::stringToMap(pre_proc_config);
    for (auto &item : pre_proc_params) {
        if (modelConfig.find(item.first) != modelConfig.end()) {
            modelConfig[item.first] = item.second;
        }
    }

    // override model config with third-party config files (if found)
    convertThirdPartyModelConfig(model_file, modelConfig);

    // the parameter parsing loop may use locale-dependent floating point conversion
    // save current locale and restore after the loop
    std::string oldlocale = std::setlocale(LC_ALL, nullptr);
    std::setlocale(LC_ALL, "C");

    for (auto &element : modelConfig) {
        if (element.first == "scale_values") {
            std::vector<std::string> values = extractNumbers(element.second.as<std::string>());
            if (values.size() == 1) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "scale", &gvalue);
                GST_INFO("[get_model_info_preproc] scale: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            } else if (values.size() == 3) {

                std::vector<double> scale_values;
                // If there are three values, use them directly
                for (const std::string &valueStr : values) {
                    scale_values.push_back(std::stod(valueStr));
                }
                // Create a GST_TYPE_ARRAY to hold the scale values
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, GST_TYPE_ARRAY);
                for (double scale_value : scale_values) {
                    GValue item = G_VALUE_INIT;
                    g_value_init(&item, G_TYPE_DOUBLE);
                    g_value_set_double(&item, scale_value);
                    gst_value_array_append_value(&gvalue, &item);
                    GST_INFO("[get_model_info_preproc] scale_values: %f", scale_value);
                    g_value_unset(&item);
                }

                // Set the array in the GstStructure
                gst_structure_set_value(s, "std", &gvalue);
                g_value_unset(&gvalue);
            } else {
                throw std::runtime_error("Invalid number of scale values. Expected 1 or 3 values.");
            }
        }
        if (element.first == "mean_values") {
            std::vector<std::string> values = extractNumbers(element.second.as<std::string>());
            std::vector<double> scale_values;

            if (values.size() == 3) {
                // If there are three values, use them directly
                for (const std::string &valueStr : values) {
                    scale_values.push_back(std::stod(valueStr));
                }
            } else {
                throw std::runtime_error("Invalid number of mean values. Expected 3 values.");
            }

            // Create a GST_TYPE_ARRAY to hold the scale values
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, GST_TYPE_ARRAY);
            for (double scale_value : scale_values) {
                GValue item = G_VALUE_INIT;
                g_value_init(&item, G_TYPE_DOUBLE);
                g_value_set_double(&item, scale_value);
                gst_value_array_append_value(&gvalue, &item);
                g_value_unset(&item);
            }

            // Set the array in the GstStructure
            gst_structure_set_value(s, "mean", &gvalue);
            GST_INFO("[get_model_info_preproc] mean: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if (element.first == "resize_type") {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);

            if (element.second.as<std::string>() == "crop") {
                g_value_set_string(&gvalue, "central-resize");
                gst_structure_set_value(s, "crop", &gvalue);
            }
            if (element.second.as<std::string>() == "fit_to_window_letterbox") {
                g_value_set_string(&gvalue, "aspect-ratio");
                gst_structure_set_value(s, "resize", &gvalue);
            }
            if (element.second.as<std::string>() == "fit_to_window") {
                g_value_set_string(&gvalue, "aspect-ratio-pad");
                gst_structure_set_value(s, "resize", &gvalue);
            }
            if (element.second.as<std::string>() == "standard") {
                g_value_set_string(&gvalue, "no-aspect-ratio");
                gst_structure_set_value(s, "resize", &gvalue);
            }
            GST_INFO("[get_model_info_preproc] resize_type: %s", element.second.as<std::string>().c_str());
            GST_INFO("[get_model_info_preproc] resize: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if (element.first == "color_space") {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);
            g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
            gst_structure_set_value(s, "color_space", &gvalue);
            GST_INFO("[get_model_info_preproc] reverse_input_channels: %s", element.second.as<std::string>().c_str());
            GST_INFO("[get_model_info_preproc] color_space: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if (element.first == "reverse_input_channels") {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_INT);
            g_value_set_int(&gvalue, gint(false));

            std::transform(element.second.as<std::string>().begin(), element.second.as<std::string>().end(),
                           element.second.as<std::string>().begin(), ::tolower);

            if (element.second.as<std::string>() == "yes" || element.second.as<std::string>() == "true")
                g_value_set_int(&gvalue, gint(true));

            gst_structure_set_value(s, "reverse_input_channels", &gvalue);
            GST_INFO("[get_model_info_preproc] reverse_input_channels: %s", element.second.as<std::string>().c_str());
            g_value_unset(&gvalue);
        }
        if (element.first == "reshape") {
            std::vector<int> size_values = element.second.as<std::vector<int>>();
            if (size_values.size() == 2) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, GST_TYPE_ARRAY);

                for (const int &size_value : size_values) {
                    GValue item = G_VALUE_INIT;
                    g_value_init(&item, G_TYPE_INT);
                    g_value_set_int(&item, size_value);
                    gst_value_array_append_value(&gvalue, &item);
                    GST_INFO("[get_model_info_preproc] reshape: %d", size_value);
                    g_value_unset(&item);
                }

                gst_structure_set_value(s, "reshape_size", &gvalue);
                g_value_unset(&gvalue);
            }
        }
    }

    // restore system locale
    std::setlocale(LC_ALL, oldlocale.c_str());

    if (s != nullptr)
        res[layer_name] = s;

    return res;
}

// Parse Model API metadata and return post-processing GstStructure
std::map<std::string, GstStructure *> get_model_info_postproc(const std::shared_ptr<ov::Model> model,
                                                              const std::string model_file) {
    std::map<std::string, GstStructure *> res;
    std::string layer_name("ANY");
    GstStructure *s = nullptr;
    ov::AnyMap modelConfig;

    if (model->has_rt_info({"model_info"})) {
        modelConfig = model->get_rt_info<ov::AnyMap>("model_info");
        s = gst_structure_new_empty(layer_name.data());
    }

    // update model config with third-party config files (if found)
    convertThirdPartyModelConfig(model_file, modelConfig);

    // the parameter parsing loop may use locale-dependent floating point conversion
    // save current locale and restore after the loop
    std::string oldlocale = std::setlocale(LC_ALL, nullptr);
    std::setlocale(LC_ALL, "C");

    for (auto &element : modelConfig) {
        if (element.first.find("model_type") != std::string::npos) {
            std::cout << "model_type: " << element.second.as<std::string>() << std::endl;
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);
            g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
            gst_structure_set_value(s, "converter", &gvalue);
            GST_INFO("[get_model_info_postproc] model_type: %s", element.second.as<std::string>().c_str());
            GST_INFO("[get_model_info_postproc] converter: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if ((element.first.find("multilabel") != std::string::npos) &&
            (element.second.as<std::string>().find("True") != std::string::npos)) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);
            const gchar *oldvalue = gst_structure_get_string(s, "method");
            if ((oldvalue != nullptr) && (strcmp(oldvalue, "softmax") == 0))
                g_value_set_string(&gvalue, "softmax_multi");
            else
                g_value_set_string(&gvalue, "multi");
            gst_structure_set_value(s, "method", &gvalue);
            GST_INFO("[get_model_info_postproc] multilabel: %s", element.second.as<std::string>().c_str());
            GST_INFO("[get_model_info_postproc] method: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if ((element.first.find("output_raw_scores") != std::string::npos) &&
            (element.second.as<std::string>().find("True") != std::string::npos)) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);
            const gchar *oldvalue = gst_structure_get_string(s, "method");
            if ((oldvalue != nullptr) && (strcmp(oldvalue, "multi") == 0))
                g_value_set_string(&gvalue, "softmax_multi");
            else
                g_value_set_string(&gvalue, "softmax");
            gst_structure_set_value(s, "method", &gvalue);
            GST_INFO("[get_model_info_postproc] output_raw_scores: %s", element.second.as<std::string>().c_str());
            GST_INFO("[get_model_info_postproc] method: %s", g_value_get_string(&gvalue));
            g_value_unset(&gvalue);
        }
        if (element.first.find("confidence_threshold") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_DOUBLE);
            g_value_set_double(&gvalue, element.second.as<double>());
            gst_structure_set_value(s, "confidence_threshold", &gvalue);
            GST_INFO("[get_model_info_postproc] confidence_threshold: %f", element.second.as<double>());
            g_value_unset(&gvalue);
        }
        if (element.first.find("iou_threshold") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_DOUBLE);
            g_value_set_double(&gvalue, element.second.as<double>());
            gst_structure_set_value(s, "iou_threshold", &gvalue);
            GST_INFO("[get_model_info_postproc] iou_threshold: %f", element.second.as<double>());
            g_value_unset(&gvalue);
        }
        if (element.first.find("image_threshold") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_DOUBLE);
            g_value_set_double(&gvalue, element.second.as<double>());
            gst_structure_set_value(s, "image_threshold", &gvalue);
            GST_INFO("[get_model_info_postproc] image_threshold: %f", element.second.as<double>());
            g_value_unset(&gvalue);
        }
        if (element.first.find("pixel_threshold") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_DOUBLE);
            g_value_set_double(&gvalue, element.second.as<double>());
            gst_structure_set_value(s, "pixel_threshold", &gvalue);
            GST_INFO("[get_model_info_postproc] pixel_threshold: %f", element.second.as<double>());
            g_value_unset(&gvalue);
        }
        if (element.first.find("normalization_scale") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_DOUBLE);
            g_value_set_double(&gvalue, element.second.as<double>());
            gst_structure_set_value(s, "normalization_scale", &gvalue);
            GST_INFO("[get_model_info_postproc] normalization_scale: %f", element.second.as<double>());
            g_value_unset(&gvalue);
        }
        if (element.first.find("task") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_STRING);
            g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
            gst_structure_set_value(s, "anomaly_task", &gvalue);
            GST_INFO("[get_model_info_postproc] anomaly_task: %s", element.second.as<std::string>().c_str());
            g_value_unset(&gvalue);
        }
        if (element.first.find("labels") != std::string::npos) {
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, GST_TYPE_ARRAY);
            std::string labels_string = element.second.as<std::string>();
            std::vector<std::string> labels = split(labels_string, ",; ");
            for (auto &el : labels) {
                GValue label = G_VALUE_INIT;
                g_value_init(&label, G_TYPE_STRING);
                g_value_set_string(&label, el.c_str());
                gst_value_array_append_value(&gvalue, &label);
                g_value_unset(&label);
                GST_INFO("[get_model_info_postproc] label: %s", el.c_str());
            }
            gst_structure_set_value(s, "labels", &gvalue);
            g_value_unset(&gvalue);
        }
    }

    // restore system locale
    std::setlocale(LC_ALL, oldlocale.c_str());

    if (s != nullptr)
        res[layer_name] = s;

    return res;
}

} // namespace ModelApiConverters