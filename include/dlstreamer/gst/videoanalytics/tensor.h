/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file tensor.h
 * @brief This file contains GVA::Tensor class which contains and describes neural network inference result
 */

#ifndef __TENSOR_H__
#define __TENSOR_H__

#include "../metadata/gstanalyticskeypointsmtd.h"
#include "../metadata/gva_tensor_meta.h"

#include <gst/analytics/analytics.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace GVA {

/**
 * @brief This class represents tensor - map-like storage for inference result information, such as output blob
 * description (output layer dims, layout, rank, precision, etc.), inference result in a raw and interpreted forms.
 * Tensor is based on GstStructure and, in general, can contain arbitrary (user-defined) fields of simplest data types,
 * like integers, floats & strings.
 * Tensor can contain raw inference result (such Tensor is produced by gvainference in Gstreamer pipeline),
 * detection result (such Tensor is produced by gvadetect in Gstreamer pipeline and it's called detection Tensor),
 * or both raw & interpreted inference results (such Tensor is produced by gvaclassify in Gstreamer pipeline).
 * Tensors can be created and used on their own, or they can be created within RegionOfInterest or VideoFrame instances.
 * Usually, in Gstreamer pipeline with GVA elements (gvadetect, gvainference, gvaclassify) Tensor objects will be
 * available for access and modification from RegionOfInterest and VideoFrame instances
 */
class Tensor {
    friend class VideoFrame;
#ifdef AUDIO
    friend class AudioFrame;
#endif

  public:
    /**
     * @brief Describes tensor precision
     */
    enum class Precision {
        UNSPECIFIED = GVA_PRECISION_UNSPECIFIED, /**< default value */
        FP32 = GVA_PRECISION_FP32,               /**< 32bit floating point value */
        FP16 = GVA_PRECISION_FP16,    /**< 16bit floating point value, 5 bit for exponent, 10 bit for mantisa */
        BF16 = GVA_PRECISION_BF16,    /**< 16bit floating point value, 8 bit for exponent, 7 bit for mantisa*/
        FP64 = GVA_PRECISION_FP64,    /**< 64bit floating point value */
        Q78 = GVA_PRECISION_Q78,      /**< 16bit specific signed fixed point precision */
        I16 = GVA_PRECISION_I16,      /**< 16bit signed integer value */
        U4 = GVA_PRECISION_U4,        /**< 4bit unsigned integer value */
        U8 = GVA_PRECISION_U8,        /**< unsigned 8bit integer value */
        I4 = GVA_PRECISION_I4,        /**< 4bit signed integer value */
        I8 = GVA_PRECISION_I8,        /**< 8bit signed integer value */
        U16 = GVA_PRECISION_U16,      /**< 16bit unsigned integer value */
        I32 = GVA_PRECISION_I32,      /**< 32bit signed integer value */
        U32 = GVA_PRECISION_U32,      /**< 32bit unsigned integer value */
        I64 = GVA_PRECISION_I64,      /**< 64bit signed integer value */
        U64 = GVA_PRECISION_U64,      /**< 64bit unsigned integer value */
        BIN = GVA_PRECISION_BIN,      /**< 1bit integer value */
        BOOL = GVA_PRECISION_BOOL,    /**< 8bit bool type */
        CUSTOM = GVA_PRECISION_CUSTOM /**< custom precision has it's own name and size of elements */
    };

    /**
     * @brief Describes tensor layout
     */
    enum class Layout {
        ANY = GVA_LAYOUT_ANY,   /**< unspecified layout */
        NCHW = GVA_LAYOUT_NCHW, /**< NCHW layout */
        NHWC = GVA_LAYOUT_NHWC, /**< NHWC layout */
        NC = GVA_LAYOUT_NC      /**< NC layout */
    };

    /**
     * @brief Get raw inference output blob data
     * @tparam T type to interpret blob data
     * @return vector of values of type T representing raw inference data, empty vector if data can't be read
     */
    template <class T>
    const std::vector<T> data() const {
        gsize size = 0;
        const void *data = gva_get_tensor_data(_structure, &size);
        if (!data || !size)
            return std::vector<T>();
        return std::vector<T>((T *)data, (T *)((char *)data + size));
    }

    /**
     * @brief Set raw data buffer as inference output data
     * @param buffer with data element
     * @param size of data buffer in bytes
     */
    void set_data(const void *buffer, size_t size) {
        if (!_structure || !buffer)
            throw std::invalid_argument("Failed to copy buffer to structure: null arguments");

        GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, buffer, size, 1);
        if (not v)
            throw std::invalid_argument("Failed to create GVariant array");
        gsize n_elem;
        gst_structure_set(_structure, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                          g_variant_get_fixed_array(v, &n_elem, 1), NULL);
    }

    /**
     * @brief Get inference result blob dimensions info
     * @return vector of dimensions. Empty vector if dims are not set
     */
    std::vector<guint> dims() const {
        GValueArray *arr = NULL;
        gst_structure_get_array(_structure, "dims", &arr);
        std::vector<guint> dims;
        if (arr) {
            for (guint i = 0; i < arr->n_values; ++i)
                dims.push_back(g_value_get_uint(g_value_array_get_nth(arr, i)));
            g_value_array_free(arr);
        }
        return dims;
    }

    /**
     * @brief Set inference result blob dimensions info
     * @param dims vector of dimensions
     */
    void set_dims(const std::vector<guint> &dims) {
        set_vector("dims", dims);
    }

    /**
     * @brief Get inference output blob precision
     * @return Enum Precision, Precision::UNSPECIFIED if can't be read
     */
    Precision precision() const {
        if (has_field("precision"))
            return (Precision)get_int("precision");
        else
            return Precision::UNSPECIFIED;
    }

    /**
     * @brief Set inference output blob precision
     * @param precision of inference data buffer
     */
    void set_precision(const Precision precision) {
        set_int("precision", static_cast<int>(precision));
    }

    /**
     * @brief Get inference result blob layout
     * @return Enum Layout, Layout::ANY if can't be read
     */
    Layout layout() const {
        if (has_field("layout"))
            return (Layout)get_int("layout");
        else
            return Layout::ANY;
    }

    /**
     * @brief Set layout of inference output blob
     * @param layout of inference data buffer
     */
    void set_layout(const Layout layout) {
        set_int("layout", static_cast<int>(layout));
    }

    /**
     * @brief Get inference result blob layer name
     * @return layer name as a string, empty string if failed to get
     */
    std::string layer_name() const {
        return get_string("layer_name");
    }

    /**
     * @brief Set name of output blob layer
     * @param name of output blob layer
     */
    void set_layer_name(const std::string &name) {
        set_string("layer_name", name);
    }

    /**
     * @brief Get model name which was used for inference
     * @return model name as a string, empty string if failed to get
     */
    std::string model_name() const {
        return get_string("model_name");
    }

    /**
     * @brief Set model name of output blob
     * @param name of output blob model
     */
    void set_model_name(const std::string &name) {
        set_string("model_name", name);
    }

    /**
     * @brief Get data format as specified in model pre/post-processing json configuration
     * @return format as a string, empty string if failed to get
     */
    std::string format() const {
        return get_string("format");
    }

    /**
     * @brief Set data format
     * @param format of inference data buffer
     */
    void set_format(const std::string &format) {
        set_string("format", format);
    }

    /**
     * @brief Get tensor type as a string
     * @return Tensor instance's type
     */
    std::string type() const {
        return get_string("type");
    }

    /**
     * @brief Set tensor type as a string
     * @param type of tensor data buffer
     */
    void set_type(const std::string &type) {
        set_string("type", type);
    }

    /**
     * @brief Get tensor name as a string
     * @return Tensor instance's name
     */
    std::string name() const {
        const gchar *name = gst_structure_get_name(_structure);
        if (name)
            return std::string(name);
        return std::string{};
    }

    /**
     * @brief Set Tensor instance's name
     * @param name of tensor instance
     */
    void set_name(const std::string &name) {
        gst_structure_set_name(_structure, name.c_str());
    }

    /**
     * @brief Get confidence of detection or classification result extracted from the tensor
     * @return confidence of inference result as a double, 0 if failed to get
     */
    double confidence() const {
        return get_double("confidence");
    }

    /**
     * @brief Set confidence of detection or classification result
     * @param confidence of inference result
     */
    void set_confidence(const double confidence) {
        set_double("confidence", confidence);
    }

    /**
     * @brief Get label. This label is set for Tensor instances produced by gvaclassify element. It will throw an
     * exception if called for detection Tensor. To get detection class label, use RegionOfInterest::label
     * @return label as a string, empty string if failed to get
     */
    std::string label() const {
        if (!this->is_detection())
            return get_string("label");
        else
            throw std::runtime_error("Detection GVA::Tensor can't have label.");
    }

    /**
     * @brief Set label. It will throw an exception if called for detection Tensor
     * @param label label name as a string
     */
    void set_label(const std::string &label) {
        if (!this->is_detection())
            set_string("label", label);
        else
            throw std::runtime_error("Detection GVA::Tensor can't have label.");
    }

    /**
     * @brief Get vector of fields contained in Tensor instance
     * @return vector of fields contained in Tensor instance
     */
    std::vector<std::string> fields() const {
        std::vector<std::string> fields;
        int fields_count = gst_structure_n_fields(_structure);
        if (fields_count <= 0)
            return fields;

        fields.reserve(fields_count);
        for (int i = 0; i < fields_count; ++i)
            fields.emplace_back(gst_structure_nth_field_name(_structure, i));
        return fields;
    }

    /**
     * @brief Check if Tensor instance has field
     * @param field_name field name
     * @return True if field with this name is found, False otherwise
     */
    bool has_field(const std::string &field_name) const {
        return gst_structure_has_field(_structure, field_name.c_str());
    }

    /**
     * @brief Get string contained in value stored at field_name
     * @param field_name field name
     * @param default_value default value
     * @return string value stored at field_name if field_name is found and contains a string, default_value string
     * otherwise
     */
    std::string get_string(const std::string &field_name, const std::string &default_value = std::string()) const {
        const gchar *val = gst_structure_get_string(_structure, field_name.c_str());
        return (val) ? std::string(val) : default_value;
    }

    /**
     * @brief Get int contained in value stored at field_name
     * @param field_name field name
     * @param default_value default value
     * @return int value stored at field_name if field_name is found and contains an int, default_value otherwise
     */
    int get_int(const std::string &field_name, int32_t default_value = 0) const {
        gint val = default_value;
        gst_structure_get_int(_structure, field_name.c_str(), &val);
        return val;
    }

    /**
     * @brief Get double contained in value stored at field_name
     * @param field_name field name
     * @param default_value default value
     * @return double value stored at field_name if field_name is found and contains an double, default_value otherwise
     */
    double get_double(const std::string &field_name, double default_value = 0) const {
        double val = default_value;
        gst_structure_get_double(_structure, field_name.c_str(), &val);
        return val;
    }

    /**
     * @brief Get vector stored as  GST_TYPE_ARRAY field
     * @param field_name name of GST_TYPE_ARRAY field to get
     * @return vector with data of type T
     */
    template <typename T>
    std::vector<T> get_vector(const char *field_name) const {
        const GValue *garray = gst_structure_get_value(_structure, field_name);
        guint size = gst_value_array_get_size(garray);
        std::vector<T> result;
        result.resize(size);

        for (guint i = 0; i < size; i++) {
            const GValue *element = gst_value_array_get_value(garray, i);
            if constexpr (std::is_same<T, uint32_t>::value || std::is_same<T, guint>::value) {
                guint value = g_value_get_uint(element);
                result[i] = static_cast<T>(value);
            } else if constexpr (std::is_same<T, float>::value) {
                gfloat value = g_value_get_float(element);
                result[i] = static_cast<T>(value);
            } else if constexpr (std::is_same<T, std::string>::value) {
                std::string value = g_value_get_string(element);
                result[i] = value;
            } else {
                throw std::invalid_argument("Unsupported data type");
            }
        }

        return result;
    }

    /**
     * @brief Set vector as GST_TYPE_ARRAY field
     * @param field_name name of GST_TYPE_ARRAY field to set
     * @param data vector to set
     */
    template <typename T>
    void set_vector(const char *field_name, const std::vector<T> &data) {
        GValue gvalue = G_VALUE_INIT;
        GValue garray = G_VALUE_INIT;
        gst_value_array_init(&garray, data.size());

        for (size_t i = 0; i < data.size(); i++) {
            if constexpr (std::is_same<T, uint32_t>::value || std::is_same<T, guint>::value) {
                g_value_init(&gvalue, G_TYPE_UINT);
                g_value_set_uint(&gvalue, data[i]);
            } else if constexpr (std::is_same<T, float>::value) {
                g_value_init(&gvalue, G_TYPE_FLOAT);
                g_value_set_float(&gvalue, data[i]);
            } else if constexpr (std::is_same<T, std::string>::value) {
                g_value_init(&gvalue, G_TYPE_STRING);
                g_value_set_string(&gvalue, data[i].c_str());
            } else {
                throw std::invalid_argument("Unsupported data type");
            }

            // append GValue to GST Array
            gst_value_array_append_value(&garray, &gvalue);
            g_value_unset(&gvalue);
        }

        gst_structure_set_value(_structure, field_name, &garray);
        g_value_unset(&garray);
    }

    /**
     * @brief Set field_name with string value
     * @param field_name field name
     * @param value value to set
     */
    void set_string(const std::string &field_name, const std::string &value) {
        gst_structure_set(_structure, field_name.c_str(), G_TYPE_STRING, value.c_str(), NULL);
    }

    /**
     * @brief Set field_name with int value
     * @param field_name field name
     * @param value value to set
     */
    void set_int(const std::string &field_name, int value) {
        gst_structure_set(_structure, field_name.c_str(), G_TYPE_INT, value, NULL);
    }

    /**
     * @brief Set field_name with uint64 value
     * @param field_name field name
     * @param value value to set
     */
    void set_uint64(const std::string &field_name, uint64_t value) {
        gst_structure_set(_structure, field_name.c_str(), G_TYPE_UINT64, value, NULL);
    }

    /**
     * @brief Set field_name with double value
     * @param field_name field name
     * @param value value to set
     */
    void set_double(const std::string &field_name, double value) {
        gst_structure_set(_structure, field_name.c_str(), G_TYPE_DOUBLE, value, NULL);
    }

    /**
     * @brief Set field_name with bool value
     * @param field_name field name
     * @param value value to set
     */
    void set_bool(const std::string &field_name, bool value) {
        gst_structure_set(_structure, field_name.c_str(), G_TYPE_BOOLEAN, value, NULL);
    }

    /**
     * @brief Get inference result blob precision as a string
     * @return precision as a string, "ANY" if can't be read
     */
    std::string precision_as_string() const {
        Precision precision_value = precision();
        switch (precision_value) {
        case Precision::FP32:
            return "FP32";
        case Precision::FP16:
            return "FP16";
        case Precision::BF16:
            return "BF16";
        case Precision::FP64:
            return "FP64";
        case Precision::Q78:
            return "Q78";
        case Precision::I16:
            return "I16";
        case Precision::U4:
            return "U4";
        case Precision::U8:
            return "U8";
        case Precision::I4:
            return "I4";
        case Precision::I8:
            return "I8";
        case Precision::U16:
            return "U16";
        case Precision::I32:
            return "I32";
        case Precision::U32:
            return "U32";
        case Precision::I64:
            return "I64";
        case Precision::U64:
            return "U64";
        case Precision::BIN:
            return "BIN";
        case Precision::BOOL:
            return "BOOL";
        case Precision::CUSTOM:
            return "CUSTOM";
        default:
            return "UNSPECIFIED";
        }
    }

    /**
     * @brief Get inference result blob layout as a string
     * @return layout as a string, "ANY" if can't be read
     */
    std::string layout_as_string() const {
        Layout layout_value = layout();
        switch (layout_value) {
        case Layout::NCHW:
            return "NCHW";
        case Layout::NHWC:
            return "NHWC";
        case Layout::NC:
            return "NC";
        default:
            return "ANY";
        }
    }

    /**
     * @brief Get inference-id property value of GVA element from which this Tensor came
     * @return inference-id property value of GVA element from which this Tensor came, empty string if failed to get
     */
    std::string element_id() const {
        return get_string("element_id");
    }

    /**
     * @brief Get label id
     * @return label id as an int, 0 if failed to get
     */
    int label_id() const {
        return get_int("label_id");
    }

    /**
     * @brief Check if this Tensor is detection Tensor (contains detection results)
     * @return True if tensor contains detection results, False otherwise
     */
    bool is_detection() const {
        return name() == "detection";
    }

    /**
     * @brief Construct Tensor instance from GstStructure. Tensor does not own structure, so if you use this
     * constructor, free structure after Tensor's lifetime, if needed
     * @param structure GstStructure to create Tensor instance from.
     */
    Tensor(GstStructure *structure) : _structure(structure) {
        if (not _structure)
            throw std::invalid_argument("GVA::Tensor: structure is nullptr");
    }

    /**
     * @brief Get ptr to underlying GstStructure
     * @return ptr to underlying GstStructure
     */
    GstStructure *gst_structure() const {
        return _structure;
    }

    /**
     * @brief Returns a string representation of the underlying GstStructure.
     * @return String with GstStructure contents.
     */
    std::string to_string() const {
        gchar *str = gst_structure_to_string(_structure);
        if (!str)
            return {};
        std::string result(str);
        g_free(str);
        return result;
    }

    /**
     * @brief Convert tensor to GST analytic metadata
     * @return if conversion succesfull, 'mtd' is a handle to created metadata
     */
    bool convert_to_meta(GstAnalyticsMtd *mtd, GstAnalyticsODMtd *od_mtd, GstAnalyticsRelationMeta *meta) const {

        if (name() == "keypoints") {
            GstAnalyticsKeypointGroupMtd *keypoint_group_mtd = reinterpret_cast<GstAnalyticsKeypointGroupMtd *>(mtd);
            const std::vector<guint> dimensions = dims();
            const std::vector<float> positions = data<float>();
            const std::vector<float> confidence = get_vector<float>("confidence");
            const gsize keypoint_count = dimensions[0];
            const gsize keypoint_dimension = dimensions[1];

            // get screen space coordinates of the parent bounding box
            gint x, y, w, h;
            gfloat c;
            if (!gst_analytics_od_mtd_get_location(od_mtd, &x, &y, &w, &h, &c)) {
                throw std::runtime_error("Failed to read object detection meta");
            }

            // create metadata for individual keypoints
            std::vector<GstAnalyticsKeypointMtd> keypoint_mtd;
            keypoint_mtd.resize(keypoint_count);
            for (gsize k = 0; k < keypoint_count; k++) {
                GstAnalyticsKeypoint keypoint;
                keypoint.x = x + w * positions[k * keypoint_dimension];
                keypoint.y = y + h * positions[k * keypoint_dimension + 1];
                keypoint.z = (keypoint_dimension == 3) ? positions[k * keypoint_dimension + 2] : 0;
                keypoint.v = confidence[k];

                if (!gst_analytics_relation_meta_add_keypoint_mtd(meta, &keypoint, &keypoint_mtd[k]))
                    throw std::runtime_error("Failed to create keypoint meta");
            }

            // create metadata for keypoint group
            if (!gst_analytics_relation_meta_add_keypointgroup_mtd(meta, keypoint_count, keypoint_mtd.data(),
                                                                   keypoint_group_mtd))
                throw std::runtime_error("Failed to create keypoint meta");

            // parse keypoint names and skeleton if defined
            gsize skeleton_count = 0;
            std::vector<GstAnalyticsKeypointPair> skeletons;
            std::vector<GQuark> names;
            if ((gst_structure_has_field(gst_structure(), "point_names") and
                 gst_structure_has_field(gst_structure(), "point_connections"))) {
                GValueArray *point_connections = nullptr;
                gst_structure_get_array(gst_structure(), "point_connections", &point_connections);
                GValueArray *point_names = nullptr;
                gst_structure_get_array(gst_structure(), "point_names", &point_names);

                auto cleanup = [&]() {
                    if (point_connections) {
                        g_value_array_free(point_connections);
                    }
                    if (point_names) {
                        g_value_array_free(point_names);
                    }
                };

                try {
                    if (point_names->n_values != keypoint_count) {
                        cleanup();
                        throw std::runtime_error("Mismatch between keypoint count and keypoint names");
                    }

                    names.resize(keypoint_count);
                    for (gsize n = 0; n < keypoint_count; n++) {
                        const gchar *name = g_value_get_string(point_names->values + n);
                        names[n] = g_quark_from_string(name);
                    }

                    skeleton_count = point_connections->n_values / 2;
                    skeletons.resize(skeleton_count);
                    for (gsize s = 0; s < skeleton_count; s++) {
                        const gchar *point_name_1 = g_value_get_string(point_connections->values + s * 2);
                        const gchar *point_name_2 = g_value_get_string(point_connections->values + s * 2 + 1);
                        for (gsize n = 0; n < point_names->n_values; n++) {
                            const gchar *name = g_value_get_string(point_names->values + n);
                            if (g_strcmp0(name, point_name_1) == 0) {
                                skeletons[s].kp1 = n;
                            }
                            if (g_strcmp0(name, point_name_2) == 0) {
                                skeletons[s].kp2 = n;
                            }
                        }
                    }

                    cleanup();
                } catch (...) {
                    cleanup();
                    throw;
                }
            }

            // find or create classification meta for point names
            GstAnalyticsClsMtd point_names_mtd;
            if (names.size() > 0) {
                gpointer state = NULL;
                bool found = false;

                // find classification meta in a frame
                while (gst_analytics_relation_meta_iterate(meta, &state, gst_analytics_cls_mtd_get_mtd_type(),
                                                           &point_names_mtd)) {
                    if (gst_analytics_cls_mtd_get_length(&point_names_mtd) == keypoint_count) {
                        found = true;
                        break;
                    }
                }

                // create keypoint classification meta if one does not exist
                if (!found) {
                    // we are not using confidence data, fill it with 1.0
                    std::vector<float> confidence;
                    confidence.resize(keypoint_count);
                    for (gsize k = 0; k < keypoint_count; k++)
                        confidence[k] = 1.0f;

                    if (!gst_analytics_relation_meta_add_cls_mtd(meta, keypoint_count, confidence.data(), names.data(),
                                                                 &point_names_mtd))
                        throw std::runtime_error("Failed to create point names");
                }
            }

            // find or create skeleton metadata
            GstAnalyticsKeypointSkeletonMtd skeleton_mtd = {0, nullptr};
            if (skeletons.size() > 0) {
                bool found = false;
                gpointer state = NULL;

                // check if skeleton meta already exists
                while (gst_analytics_relation_meta_iterate(meta, &state,
                                                           gst_analytics_keypoint_skeleton_mtd_get_mtd_type(),
                                                           reinterpret_cast<GstAnalyticsMtd *>(&skeleton_mtd))) {
                    if (gst_analytics_keypoint_skeleton_mtd_get_count(&skeleton_mtd) == skeleton_count) {
                        found = true;
                        break;
                    }
                }

                // create keypoint skeleton if one does not exists
                if (!found) {
                    if (!gst_analytics_relation_meta_add_keypoint_skeleton_mtd(meta, skeleton_count, skeletons.data(),
                                                                               &skeleton_mtd))
                        throw std::runtime_error("Failed to create skeleton metadata");
                }
            }

            // link keypoint group, keypoint names and skeleton info
            if (!gst_analytics_relation_meta_set_keypointgroup_relations(meta, keypoint_group_mtd, &point_names_mtd,
                                                                         &skeleton_mtd))
                throw std::runtime_error("Failed to set relation between keypoint group and keypoint names/skeleton");

            return true;
        } else if (type() == "classification_result") {
            GstAnalyticsClsMtd *cls_mtd = mtd;
            gfloat confidence = this->confidence();
            GQuark label = g_quark_from_string(this->label().c_str());

            if (!gst_analytics_relation_meta_add_one_cls_mtd(meta, confidence, label, cls_mtd)) {
                throw std::runtime_error("Failed to create classification meta");
            }

            return true;
        }

        return false;
    }

    /**
     * @brief Convert GST analytic metadata to tensor structure
     * @param mtd GST analytics metadata to convert
     * @return pointer to GstStructure representing the tensor, nullptr if conversion failed
     */
    static GstStructure *convert_to_tensor(GstAnalyticsMtd mtd) {

        if (gst_analytics_mtd_get_mtd_type(&mtd) == gst_analytics_keypointgroup_mtd_get_mtd_type()) {

            // read keypoint metadata
            GstAnalyticsKeypointGroupMtd *keypoint_group_mtd = reinterpret_cast<GstAnalyticsKeypointGroupMtd *>(&mtd);
            gsize keypoint_count = gst_analytics_keypointgroup_mtd_get_count(keypoint_group_mtd);
            gsize keypoint_dimension = 2;
            std::vector<GstAnalyticsKeypoint> keypoints;
            keypoints.resize(keypoint_count);

            // find parent bounding box
            gint x = 0;
            gint y = 0;
            gint w = 0;
            gint h = 0;
            gfloat c;
            GstAnalyticsODMtd od_mtd;
            if (gst_analytics_relation_meta_get_direct_related(keypoint_group_mtd->meta, keypoint_group_mtd->id,
                                                               GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                               gst_analytics_od_mtd_get_mtd_type(), nullptr, &od_mtd)) {
                if (!gst_analytics_od_mtd_get_location(&od_mtd, &x, &y, &w, &h, &c)) {
                    throw std::runtime_error("Failed to read object detection meta");
                }
            }

            // detect dimension of position tensor
            for (size_t k = 0; k < keypoint_count; ++k) {
                GstAnalyticsKeypointMtd keypoint_mtd;
                gst_analytics_keypointgroup_mtd_get_keypoint_mtd(keypoint_group_mtd, &keypoint_mtd, k);
                gst_analytics_keypoint_mtd_get(&keypoint_mtd, &keypoints[k]);
                if (keypoints[k].z != 0)
                    keypoint_dimension = 3;
            }

            // read position and confidence data
            std::vector<float> positions, confidences;
            positions.resize(keypoint_count * keypoint_dimension);
            confidences.resize(keypoint_count);

            for (size_t k = 0; k < keypoint_count; ++k) {
                positions[k * keypoint_dimension] = (w > 0) ? float(keypoints[k].x - x) / float(w) : 0.0f;
                positions[k * keypoint_dimension + 1] = (h > 0) ? float(keypoints[k].y - y) / float(h) : 0.0f;
                if (keypoint_dimension == 3)
                    positions[k * keypoint_dimension + 2] = keypoints[k].z;
                confidences[k] = keypoints[k].v;
            }

            // read point names
            std::vector<std::string> point_names;
            point_names.resize(keypoint_count);

            GstAnalyticsClsMtd point_names_mtd;
            if (gst_analytics_relation_meta_get_direct_related(
                    keypoint_group_mtd->meta, keypoint_group_mtd->id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                    gst_analytics_cls_mtd_get_mtd_type(), nullptr, &point_names_mtd)) {

                if (gst_analytics_cls_mtd_get_length(&point_names_mtd) == keypoint_count) {
                    for (size_t k = 0; k < keypoint_count; k++)
                        point_names[k] = g_quark_to_string(gst_analytics_cls_mtd_get_quark(&point_names_mtd, k));
                }
            }

            // convert skeleton info to point connections
            std::vector<std::string> point_connections;

            GstAnalyticsKeypointSkeletonMtd skeleton_mtd;
            if (gst_analytics_relation_meta_get_direct_related(
                    keypoint_group_mtd->meta, keypoint_group_mtd->id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                    gst_analytics_keypoint_skeleton_mtd_get_mtd_type(), nullptr,
                    reinterpret_cast<GstAnalyticsMtd *>(&skeleton_mtd))) {

                gsize skeleton_count = gst_analytics_keypoint_skeleton_mtd_get_count(&skeleton_mtd);
                point_connections.resize(2 * skeleton_count);

                // convert skeleton metadata to point connections
                for (size_t s = 0; s < skeleton_count; s++) {
                    // read point indices for a skeleton segment
                    GstAnalyticsKeypointPair segment;
                    gst_analytics_keypoint_skeleton_mtd_get(&skeleton_mtd, &segment, s);
                    // store name of points in a segment
                    point_connections[2 * s] = point_names[segment.kp1];
                    point_connections[2 * s + 1] = point_names[segment.kp2];
                }
            }

            // create keypoint tensor
            GstStructure *gst_structure = gst_structure_new_empty("keypoints");
            Tensor tensor(gst_structure); // use Tensor wrapper

            tensor.set_precision(Precision::FP32);
            tensor.set_format(std::string("keypoints"));

            // set tensor data and its dimensions
            tensor.set_dims({static_cast<guint>(keypoint_count), 2});
            tensor.set_data(reinterpret_cast<const void *>(positions.data()),
                            keypoint_count * keypoint_dimension * sizeof(float));

            // set keypoint confidence, point names and point connections
            tensor.set_vector<float>("confidence", confidences);
            tensor.set_vector<std::string>("point_names", point_names);
            tensor.set_vector<std::string>("point_connections", point_connections);

            return tensor.gst_structure();

        } else if (gst_analytics_mtd_get_mtd_type(&mtd) == gst_analytics_cls_mtd_get_mtd_type()) {
            GstAnalyticsClsMtd *cls_mtd = &mtd;
            gsize class_count = gst_analytics_cls_mtd_get_length(cls_mtd);

            GstStructure *tensor = gst_structure_new_empty("classification");
            gst_structure_set(tensor, "type", G_TYPE_STRING, "classification_result", NULL);

            gfloat result_confidence = 0;
            std::string result_label;
            for (size_t i = 0; i < class_count; i++) {
                gfloat confidence = gst_analytics_cls_mtd_get_level(cls_mtd, i);
                GQuark quark_label = gst_analytics_cls_mtd_get_quark(cls_mtd, i);
                std::string label = quark_label ? std::string(g_quark_to_string(quark_label)) : "";

                if (!label.empty()) {
                    if (!result_label.empty() and !isspace(result_label.back()))
                        result_label += " ";
                    result_label += label;
                }

                if (confidence > result_confidence) {
                    result_confidence = confidence;
                }
            }
            gst_structure_set(tensor, "label", G_TYPE_STRING, result_label.c_str(), NULL);
            gst_structure_set(tensor, "confidence", G_TYPE_DOUBLE, result_confidence, NULL);

            GstAnalyticsClsMtd cls_descriptor_mtd = {0, nullptr};
            if (class_count == 1 && gst_analytics_relation_meta_get_direct_related(
                                        cls_mtd->meta, cls_mtd->id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                        gst_analytics_cls_mtd_get_mtd_type(), nullptr, &cls_descriptor_mtd)) {
                gint label_id = gst_analytics_cls_mtd_get_index_by_quark(&cls_descriptor_mtd,
                                                                         g_quark_from_string(result_label.c_str()));
                if (label_id >= 0) {
                    gst_structure_set(tensor, "label_id", G_TYPE_INT, label_id, NULL);
                }
            }

            return tensor;
        }

        return nullptr;
    }

  protected:
    /**
     * @brief ptr to GstStructure that contains all tensor (inference results) data & info.
     */
    GstStructure *_structure;
};

} // namespace GVA

#endif // __TENSOR_H__
