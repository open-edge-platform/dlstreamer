/*******************************************************************************
 * Copyright (C) 2019-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "copy_blob_to_gststruct.h"

#include "safe_arithmetic.hpp"
#include <inference_backend/logger.h>

#include <cmath>

using namespace std;

namespace {

int GetUnbatchedSizeInBytes(InferenceBackend::OutputBlob::Ptr blob, size_t batch_size) {
    if (!blob)
        throw std::invalid_argument("GetUnbatchedSizeInBytes: blob is null");

    const std::vector<size_t> &dims = blob->GetDims();
    // On some type of models, such as SSD, batch-size at the output layer may differ from the input layer in case of
    // reshape. In the topology of such models, there is a decrease in dimension on hidden layers, which causes the
    // batch size to be lost. To correctly calculate size of blob you need to multiply all the dimensions and divide by
    // the batch size.
    if (dims.size() == 0)
        throw std::invalid_argument("Failed to get blob size for blob with 0 dimensions");
    size_t size = dims[0];
    for (size_t i = 1; i < dims.size(); ++i)
        size = safe_mul(size, dims[i]);
    size /= batch_size;

    switch (blob->GetPrecision()) {
    case InferenceBackend::OutputBlob::Precision::FP64:
    case InferenceBackend::OutputBlob::Precision::I64:
    case InferenceBackend::OutputBlob::Precision::U64:
        size *= sizeof(int64_t);
        break;
    case InferenceBackend::OutputBlob::Precision::FP32:
    case InferenceBackend::OutputBlob::Precision::I32:
    case InferenceBackend::OutputBlob::Precision::U32:
        size *= sizeof(int32_t);
        break;
    case InferenceBackend::OutputBlob::Precision::FP16:
    case InferenceBackend::OutputBlob::Precision::BF16:
    case InferenceBackend::OutputBlob::Precision::I16:
    case InferenceBackend::OutputBlob::Precision::Q78:
    case InferenceBackend::OutputBlob::Precision::U16:
        size *= sizeof(int16_t);
        break;
    case InferenceBackend::OutputBlob::Precision::U8:
    case InferenceBackend::OutputBlob::Precision::I8:
    case InferenceBackend::OutputBlob::Precision::BOOL:
        break;
    default:
        throw std::invalid_argument("Failed to get blob size for blob with " +
                                    std::to_string(static_cast<int>(blob->GetPrecision())) +
                                    " InferenceEngine::Precision");
        break;
    }
    return size;
}

GValueArray *ConvertVectorToGValueArr(const std::vector<size_t> &vector) {
    GValueArray *g_arr = g_value_array_new(vector.size());
    if (not g_arr)
        throw std::runtime_error("Failed to create GValueArray with " + std::to_string(vector.size()) + " elements");

    try {
        GValue gvalue = G_VALUE_INIT;
        g_value_init(&gvalue, G_TYPE_UINT);
        for (guint i = 0; i < vector.size(); ++i) {
            g_value_set_uint(&gvalue, safe_convert<unsigned int>(vector[i]));
            g_value_array_append(g_arr, &gvalue);
        }

        return g_arr;
    } catch (const std::exception &e) {
        if (g_arr)
            g_value_array_free(g_arr);
        std::throw_with_nested(std::runtime_error("Failed to convert std::vector to GValueArray"));
    }
}

} // namespace

void copy_buffer_to_structure(GstStructure *structure, const void *buffer, size_t size) {
    ITT_TASK(__FUNCTION__);
    if (!structure || !buffer)
        throw std::invalid_argument("Failed to copy buffer to structure: null arguments");

    GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, buffer, size, 1);
    if (not v)
        throw std::invalid_argument("Failed to create GVariant array");
    gsize n_elem;
    gst_structure_set(structure, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                      g_variant_get_fixed_array(v, &n_elem, 1), NULL);
}

void CopyOutputBlobToGstStructure(InferenceBackend::OutputBlob::Ptr blob, GstStructure *gst_struct,
                                  const char *model_name, const char *layer_name, int32_t batch_size,
                                  int32_t batch_index, int32_t size) {
    try {
        if (!blob)
            throw std::invalid_argument("Blob pointer is null");

        const uint8_t *data = reinterpret_cast<const uint8_t *>(blob->GetData());
        if (data == nullptr)
            throw std::invalid_argument("Failed to get blob data");

        size_t blob_size = GetUnbatchedSizeInBytes(blob, batch_size);

        // If size is -1, copy all data; otherwise, copy only the specified size
        size_t copy_size = (size == -1) ? blob_size : std::min(static_cast<size_t>(size), blob_size);

        // Copy the data
        copy_buffer_to_structure(gst_struct, data + batch_index * blob_size, copy_size);

        gst_structure_set(gst_struct, "layer_name", G_TYPE_STRING, layer_name, "model_name", G_TYPE_STRING, model_name,
                          "precision", G_TYPE_INT, static_cast<int>(blob->GetPrecision()), "layout", G_TYPE_INT,
                          static_cast<int>(blob->GetLayout()), NULL);

        // dimensions
        auto dims = blob->GetDims();
        if (dims.size() == 0)
            throw std::invalid_argument("Blob has 0 dimensions");

        GValueArray *g_arr = ConvertVectorToGValueArr(dims);
        gst_structure_set_array(gst_struct, "dims", g_arr);
        g_value_array_free(g_arr);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to copy model '" + std::string(model_name) +
                                                  "' output blob of layer '" + std::string(layer_name) +
                                                  "' to resulting Tensor"));
    }
}
