/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "convert_tensor.h"

using json = nlohmann::json;

template <typename T>
inline void add_array_object(const std::string &name, T &&array, json &jobject) {
    json jarray;

    for (const auto &item : array)
        jarray += item;

    jobject.push_back(json::object_t::value_type(name, jarray));
}

void attach_gvaluearray_to_json(const GVA::Tensor &tensor, const std::string &fieldname, json &jobject) {
    const GValue *garray = gst_structure_get_value(tensor.gst_structure(), fieldname.c_str());
    if (!garray)
        return;

    guint size = gst_value_array_get_size(garray);
    if (size == 0)
        return;

    json data_array;
    for (guint i = 0; i < size; ++i) {
        const GValue *val = gst_value_array_get_value(garray, i);
        if (G_VALUE_HOLDS_STRING(val)) {
            const gchar *str = g_value_get_string(val);
            data_array += std::string(str ? str : "");
        } else if (G_VALUE_HOLDS_UINT(val)) {
            data_array += g_value_get_uint(val);
        } else if (G_VALUE_HOLDS_INT(val)) {
            data_array += g_value_get_int(val);
        } else if (G_VALUE_HOLDS_FLOAT(val)) {
            data_array += g_value_get_float(val);
        }
    }

    if (!data_array.is_null())
        jobject.push_back(json::object_t::value_type(fieldname, data_array));
}

void convert_keypoints_fields(const GVA::Tensor &tensor, json &jobject) {
    attach_gvaluearray_to_json(tensor, "point_connections", jobject);
    attach_gvaluearray_to_json(tensor, "point_names", jobject);
}

json convert_tensor(const GVA::Tensor &s_tensor) {
    json jobject = json::object();
    std::string precision_value = s_tensor.precision_as_string();
    if (!precision_value.empty()) {
        jobject.push_back(json::object_t::value_type("precision", precision_value));
    }
    std::string layout_value = s_tensor.layout_as_string();
    if (!layout_value.empty()) {
        jobject.push_back(json::object_t::value_type("layout", layout_value));
    }
    if (s_tensor.has_field("dims")) {
        add_array_object("dims", s_tensor.dims(), jobject);
    }
    std::string name_value = s_tensor.name();
    if (!name_value.empty()) {
        jobject.push_back(json::object_t::value_type("name", name_value));
    }
    std::string model_name_value = s_tensor.model_name();
    if (!model_name_value.empty()) {
        jobject.push_back(json::object_t::value_type("model_name", model_name_value));
    }
    std::string layer_name_value = s_tensor.layer_name();
    if (!layer_name_value.empty()) {
        jobject.push_back(json::object_t::value_type("layer_name", layer_name_value));
    }
    std::string format_value = s_tensor.format();
    if (!format_value.empty()) {
        jobject.push_back(json::object_t::value_type("format", format_value));
    }

    if (!s_tensor.is_detection()) {
        std::string label_value = s_tensor.label();
        if (!label_value.empty()) {
            jobject.push_back(json::object_t::value_type("label", label_value));
        }
    }
    if (s_tensor.has_field("confidence")) {
        jobject.push_back(json::object_t::value_type("confidence", s_tensor.confidence()));
    }
    if (s_tensor.has_field("label_id")) {
        jobject.push_back(json::object_t::value_type("label_id", s_tensor.get_int("label_id")));
    }

    json data_array;
    if (s_tensor.precision() == GVA::Tensor::Precision::U8) {
        const std::vector<uint8_t> data = s_tensor.data<uint8_t>();
        for (const auto &val : data) {
            data_array += val;
        }
    } else if (s_tensor.precision() == GVA::Tensor::Precision::I64) {
        const std::vector<int64_t> data = s_tensor.data<int64_t>();
        for (const auto &val : data) {
            data_array += val;
        }
    } else {
        const std::vector<float> data = s_tensor.data<float>();
        for (const auto &val : data) {
            data_array += val;
        }
    }
    if (!data_array.is_null()) {
        jobject.push_back(json::object_t::value_type("data", data_array));
    }
    convert_keypoints_fields(s_tensor, jobject);

    return jobject;
}
