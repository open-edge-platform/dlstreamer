/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "attachroi.h"
#include <exception>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "inference_backend/logger.h"
#include "region_of_interest.h"
#include "utils.h"
#include <safe_arithmetic.hpp>

using json = nlohmann::json;

void attachJsonTensorToTensor(GVA::Tensor &tensor, const nlohmann::json::value_type &jsonTensor);

AttachRoi::AttachRoi(const char *filepath, const char *roi_str, Mode mode) : _mode(mode) {
    if (filepath) {
        loadJsonFromFile(Utils::fixPath(filepath).c_str());
    }

    if (roi_str)
        setRoiFromString(roi_str);
}

void AttachRoi::attachMetas(GVA::VideoFrame &vframe, GstClockTime timestamp) {
    _frame_num++;

    if (!_roi.empty())
        addStaticRoi(vframe);

    // TODO: implement handling tensors attached to buffer
    if (!_roi_json.empty()) {
        addRoiFromJson(vframe, timestamp);
        // Full-frame
        addTensorFromJson(vframe, timestamp);
    }

    // Empty region
    if (_roi_json.empty() && _roi.empty()) {
        vframe.add_region(0., 0., 1., 1., std::string(), 0., true);
    }
}

void AttachRoi::loadJsonFromFile(const char *filepath) {
    assert(filepath);

    // unique_ptr  + custom deleter
    auto file = std::unique_ptr<FILE, int (*)(FILE *)>(fopen(filepath, "rb"), &fclose);
    if (!file)
        throw std::runtime_error("Failed to open JSON file: " + std::string(filepath));

    // JSON keys we're interested in
    std::unordered_set<json> jkeys = {json("x"),
                                      json("y"),
                                      json("w"),
                                      json("h"),
                                      json("objects"),
                                      json("detection"),
                                      json("label_id"),
                                      json("confidence"),
                                      json("bounding_box"),
                                      json("x_max"),
                                      json("x_min"),
                                      json("y_max"),
                                      json("y_min"),
                                      json("timestamp"),
                                      json("tensors"),
                                      json("label"),
                                      json("converter"),
                                      json("data"),
                                      json("dims"),
                                      json("layer_name"),
                                      json("model_name"),
                                      json("name"),
                                      json("point_connections"),
                                      json("point_names"),
                                      json("precision"),
                                      json("format"),
                                      json("type")};

    auto parse_cb = [&jkeys](int /*depth*/, json::parse_event_t event, json &parsed) {
        if (event != json::parse_event_t::key)
            return true;
        return jkeys.find(parsed) != jkeys.end();
    };

    try {
        _roi_json = json::parse(file.get(), parse_cb);

        if (_mode == Mode::ByTimestamp) {
            // Fill hashmap for search by timestamps
            _ts_map.reserve(_roi_json.size());
            for (auto it = _roi_json.begin(); it != _roi_json.end(); ++it) {
                const guint64 timestamp = it.value().at("timestamp").get<guint64>();
                const size_t json_index = safe_convert<size_t>(std::distance(_roi_json.begin(), it));
                _ts_map.emplace(timestamp, json_index);
            }
        }
    } catch (std::exception &e) {
        std::throw_with_nested(std::runtime_error("Error during parsing JSON"));
    }
}

void AttachRoi::setRoiFromString(const char *roi_str) {
    assert(roi_str);

    try {
        guint *coords[] = {&_roi.x_top_left, &_roi.y_top_left, &_roi.x_bottom_right, &_roi.y_bottom_right};

        const auto tokens = Utils::splitString(roi_str, ',');
        if (tokens.size() != std::extent<decltype(coords)>::value)
            throw std::runtime_error("Invalid ROI string format! Please specify ROI in format: " ROI_FORMAT_STRING);

        for (size_t i = 0; i < tokens.size(); i++)
            *coords[i] = safe_convert<guint>(std::stoul(tokens[i]));

        // Sanity x_bottom_right > x_top_left and y_bottom_right > y_top_left
        if (!_roi.valid())
            throw std::runtime_error("Invalid ROI coordinates");

        // TODO: check image boundaries

    } catch (std::exception &e) {
        std::throw_with_nested(std::runtime_error("Error parsing ROI string:" + std::string(roi_str)));
    }
}

void AttachRoi::addStaticRoi(GVA::VideoFrame &vframe) const {
    assert(!_roi.empty());
    assert(_roi.valid());

    vframe.add_region(_roi.x_top_left, _roi.y_top_left, _roi.width(), _roi.height());
}

namespace {

// Helper function to parse single item from "objects" array from JSON and attach meta from it to the frame.
void addRoiFromJsonObjNode(const json &jsonRoi, GVA::VideoFrame &vframe) {
    GVA::Rect<gdouble> rect;
    gfloat confidence = 0.f;
    bool normalized = false;
    std::string label;

    auto it_det = jsonRoi.find("detection");
    if (it_det != jsonRoi.end()) {
        confidence = it_det->value("confidence", confidence);
        label = it_det->value("label", std::string());

        auto it_bbox = it_det->find("bounding_box");
        if (it_bbox != it_det->end()) {
            auto x_min = it_bbox->at("x_min").get<gdouble>();
            auto x_max = it_bbox->at("x_max").get<gdouble>();
            auto y_min = it_bbox->at("y_min").get<gdouble>();
            auto y_max = it_bbox->at("y_max").get<gdouble>();

            rect = {x_min, y_min, x_max - x_min, y_max - y_min};
            normalized = true;
        }
    }

    // If node doesn't contain nomalized coordinates then use absolute.
    if (!normalized) {
        rect.x = jsonRoi.at("x").get<guint>();
        rect.y = jsonRoi.at("y").get<guint>();
        rect.w = jsonRoi.at("w").get<guint>();
        rect.h = jsonRoi.at("h").get<guint>();
    }

    auto roi = vframe.add_region(rect.x, rect.y, rect.w, rect.h, label, confidence, normalized);

    // Add label_id if present in node
    if (it_det != jsonRoi.end()) {
        auto it = it_det->find("label_id");
        if (it != it_det->end())
            roi.detection().set_int("label_id", it->get<gint>());
    }

    auto tensorsIterator = jsonRoi.find("tensors");
    if (tensorsIterator != jsonRoi.end()) {
        for (const nlohmann::json::value_type &jsonTensor : *tensorsIterator) {
            auto nameValueIter = jsonTensor.find("name");
            if (nameValueIter == jsonTensor.end())
                continue;
            GstStructure *gst_structure = gst_structure_new_empty(nameValueIter->get<std::string>().c_str());
            GVA::Tensor tensor(gst_structure);
            attachJsonTensorToTensor(tensor, jsonTensor);
            roi.add_tensor(tensor);
        }
    }
}

} // namespace

void AttachRoi::addRoiFromJson(GVA::VideoFrame &vframe, GstClockTime timestamp) const {
    assert(!_roi_json.empty());

    bool found;
    size_t idx;
    std::tie(found, idx) = findJsonIndex(timestamp);
    if (!found)
        return;
    assert(idx < _roi_json.size());

    try {
        auto &node = _roi_json[idx];
        // Skip if "objects" array-node doesn't present
        auto it_objs = node.find("objects");
        if (it_objs == node.end())
            return;

        for (auto &jsonRoi : *it_objs) {
            if (!jsonRoi.empty())
                addRoiFromJsonObjNode(jsonRoi, vframe);
        }
    } catch (std::exception &e) {
        std::throw_with_nested(
            std::runtime_error("Malformed object meta entry (JSON top-array index " + std::to_string(idx) + ")"));
    }
}

GValueArray *ConvertVectorToGValueArr(const std::vector<std::string> &vector) {
    GValueArray *g_arr = g_value_array_new(vector.size());
    GValue gvalue = G_VALUE_INIT;
    g_value_init(&gvalue, G_TYPE_STRING);
    for (guint i = 0; i < vector.size(); ++i) {
        g_value_set_string(&gvalue, vector[i].c_str());
        g_value_array_append(g_arr, &gvalue);
    }

    return g_arr;
}

void attachJsonTensorToTensor(GVA::Tensor &tensor, const nlohmann::json::value_type &jsonTensor) {
    auto nameValueIter = jsonTensor.find("name");
    if (nameValueIter != jsonTensor.end())
        tensor.set_name(nameValueIter->get<std::string>());

    const std::string string_keys[] = {"label", "format", "model_name", "layer_name", "converter", "type"};
    for (const std::string &key : string_keys) {
        auto stringValueIter = jsonTensor.find(key);
        if (stringValueIter != jsonTensor.end())
            tensor.set_string(key, stringValueIter->get<std::string>());
    }

    const std::string string_array_keys[] = {"point_names"};
    for (const std::string &key : string_array_keys) {
        auto stringArrayIter = jsonTensor.find(key);
        if (stringArrayIter != jsonTensor.end()) {
            std::vector<std::string> array;
            for (const auto &iter : *stringArrayIter) {
                array.push_back(iter.get<std::string>());
            }
            GValueArray *garray = ConvertVectorToGValueArr(array);
            gst_structure_set_array(tensor.gst_structure(), key.c_str(), garray);
            g_value_array_free(garray);
        }
    }
    const std::string double_array_keys[] = {"data"};

    for (const std::string &key : double_array_keys) {
        auto stringArrayIter = jsonTensor.find(key);
        if (stringArrayIter != jsonTensor.end()) {
            std::vector<float> array;
            for (const auto &iter : *stringArrayIter) {
                array.push_back(iter.get<float>());
            }
            if (array.empty())
                continue;
            GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, array.data(), array.size() * sizeof(float), 1);
            gsize n_elem;
            gst_structure_set(tensor.gst_structure(), "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                              g_variant_get_fixed_array(v, &n_elem, 1), NULL);
        }
    }

    const std::string uint_array_keys[] = {"dims", "point_connections"};
    for (const std::string &key : uint_array_keys) {
        auto uintArrayIter = jsonTensor.find(key);
        if (uintArrayIter != jsonTensor.end()) {
            GValueArray *g_arr = g_value_array_new(1);
            GValue gvalue = G_VALUE_INIT;
            g_value_init(&gvalue, G_TYPE_UINT);
            for (const auto &iter : *uintArrayIter) {
                g_value_set_uint(&gvalue, iter.get<guint>());
                g_value_array_append(g_arr, &gvalue);
            }
            gst_structure_set_array(tensor.gst_structure(), key.c_str(), g_arr);
            g_value_array_free(g_arr);
        }
    }
}

void AttachRoi::addTensorFromJson(GVA::VideoFrame &vframe, GstClockTime timestamp) const {
    assert(!_roi_json.empty());

    bool found = false;
    size_t idx = 0;
    std::tie(found, idx) = findJsonIndex(timestamp);
    if (!found)
        return;
    assert(idx < _roi_json.size());

    try {
        const nlohmann::json::value_type &node = _roi_json[idx];

        auto tensorsIterator = node.find("tensors");
        if (tensorsIterator != node.end()) {
            for (const nlohmann::json::value_type &jsonTensor : *tensorsIterator) {
                GVA::Tensor tensor = vframe.add_tensor();
                attachJsonTensorToTensor(tensor, jsonTensor);
            }
        }

        nlohmann::json::const_iterator it_objs = node.find("objects");
        if (it_objs == node.end())
            return;

        for (const nlohmann::json::value_type &obj : *it_objs) {
            auto it_tens = obj.find("tensors");
            if (it_tens == obj.end())
                continue;

            for (const nlohmann::json::value_type &jelement : *it_tens) {
                auto labelIter = jelement.find("label");
                if (labelIter != jelement.end()) {
                    auto tensor = vframe.add_tensor();
                    tensor.set_label(labelIter->get<std::string>());
                }
            }
        }
    } catch (std::exception &e) {
        std::throw_with_nested(
            std::runtime_error("Malformed object meta entry (JSON top-array index " + std::to_string(idx) + ")"));
    }
}

std::pair<bool, size_t> AttachRoi::findJsonIndex(GstClockTime timestamp) const {
    assert(_frame_num != 0);

    if (_mode == Mode::ByTimestamp) {
        auto it = _ts_map.find(timestamp);
        if (it == _ts_map.end())
            return {false, _roi_json.size()};
        return {true, it->second};
    }

    // In-Order or In-Loop modes
    assert(_mode == Mode::InOrder || _mode == Mode::InLoop);

    size_t ans = _frame_num - 1;
    if (_mode == Mode::InLoop && !_roi_json.empty())
        ans %= _roi_json.size();

    if (ans >= _roi_json.size()) {
        static bool warning_emitted = false;
        if (!warning_emitted) {
            GST_WARNING("The number of frames in pipeline is greater than the number of ROIs in JSON file! No more "
                        "ROIs will be attached.");
            warning_emitted = true;
        }
        return {false, _roi_json.size()};
    }

    return {true, ans};
}
