/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "custom_to_tensor.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"

#include <dlfcn.h>
#include <vector>

using namespace post_processing;

TensorsTable CustomToTensorConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        GstStructure *network_structure = gst_structure_copy(getModelProcOutputInfo().get());
        GVA::Tensor network_tensor(network_structure);
        network_tensor.set_name("network");
        network_tensor.set_model_name(getModelName());
        network_tensor.set_vector<std::string>("labels", getLabels());

        network_tensor.set_uint64("image_width", model_input_image_info.width);
        network_tensor.set_uint64("image_height", model_input_image_info.height);

        GstStructure *params_structure = gst_structure_new_empty("params");
        GVA::Tensor params_tensor(params_structure);

        void *handle = dlopen(custom_postproc_lib.c_str(), RTLD_LAZY);
        if (!handle) {
            throw std::runtime_error("Failed to load library: " + std::string(dlerror()));
        }

        ConvertFunc convert_func = (ConvertFunc)dlsym(handle, "Convert");
        if (!convert_func) {
            dlclose(handle);
            throw std::runtime_error("Failed to find symbol 'Convert': " + std::string(dlerror()));
        }

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &frame_tensors = tensors_table[batch_number];

            GstBuffer *buf = gst_buffer_new();
            GstTensorMeta *tmeta = gst_buffer_add_tensor_meta(buf);
            GstTensor **tensors = g_new(GstTensor *, output_blobs.size());

            int i = 0;
            for (const auto &blob_iter : output_blobs) {
                GQuark tensor_id = g_quark_from_string(blob_iter.first.c_str());
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (not blob)
                    throw std::invalid_argument("Output blob is nullptr.");

                size_t unbatched_size = blob->GetSize() / batch_size;
                GstTensorDataType tensor_type;
                size_t elem_size = 0;

                switch (blob->GetPrecision()) {
                case InferenceBackend::Blob::Precision::U8: {
                    tensor_type = GST_TENSOR_DATA_TYPE_UINT8;
                    elem_size = sizeof(uint8_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::FP32: {
                    tensor_type = GST_TENSOR_DATA_TYPE_FLOAT32;
                    elem_size = sizeof(float);
                    break;
                }
                case InferenceBackend::Blob::Precision::FP16: {
                    tensor_type = GST_TENSOR_DATA_TYPE_FLOAT16;
                    elem_size = sizeof(uint16_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::BF16: {
                    tensor_type = GST_TENSOR_DATA_TYPE_BFLOAT16;
                    elem_size = sizeof(uint16_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::FP64: {
                    tensor_type = GST_TENSOR_DATA_TYPE_FLOAT64;
                    elem_size = sizeof(double);
                    break;
                }
                case InferenceBackend::Blob::Precision::I16: {
                    tensor_type = GST_TENSOR_DATA_TYPE_INT16;
                    elem_size = sizeof(int16_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::I32: {
                    tensor_type = GST_TENSOR_DATA_TYPE_INT32;
                    elem_size = sizeof(int32_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::I64: {
                    tensor_type = GST_TENSOR_DATA_TYPE_INT64;
                    elem_size = sizeof(int64_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::U16: {
                    tensor_type = GST_TENSOR_DATA_TYPE_UINT16;
                    elem_size = sizeof(uint16_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::U32: {
                    tensor_type = GST_TENSOR_DATA_TYPE_UINT32;
                    elem_size = sizeof(uint32_t);
                    break;
                }
                case InferenceBackend::Blob::Precision::U64: {
                    tensor_type = GST_TENSOR_DATA_TYPE_UINT64;
                    elem_size = sizeof(uint64_t);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported tensor precision for data pointer casting.");
                }

                std::vector<gsize> dims = std::vector<gsize>(blob->GetDims().begin(), blob->GetDims().end());
                if (!dims.empty())
                    dims[0] = 1;
                gsize num_dims = dims.size();

                gsize max_size = blob->GetSize() * elem_size;
                gsize tensor_size = unbatched_size * elem_size;
                gsize offset = batch_number * tensor_size;

                GstBuffer *tensor_data =
                    gst_buffer_new_wrapped_full((GstMemoryFlags)0, const_cast<void *>(blob->GetData()), max_size,
                                                offset, tensor_size, nullptr, nullptr);

                GstTensor *tensor = gst_tensor_new_simple(tensor_id, tensor_type, tensor_data,
                                                          GST_TENSOR_DIM_ORDER_ROW_MAJOR, num_dims, dims.data());

                tensors[i] = tensor;
                i++;
            }

            gst_tensor_meta_set(tmeta, output_blobs.size(), tensors);

            GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buf);

            convert_func(tmeta, network_structure, params_structure, relation_meta);

            gpointer state = nullptr;
            GstAnalyticsMtd tensor_mtd;
            while (
                gst_analytics_relation_meta_iterate(relation_meta, &state, GST_ANALYTICS_MTD_TYPE_ANY, &tensor_mtd)) {
                GstStructure *s = GVA::Tensor::convert_to_tensor(tensor_mtd);
                if (s != nullptr) {
                    std::vector<GstStructure *> result_tensor{s};
                    frame_tensors.push_back(result_tensor);
                }
            }
            gst_buffer_unref(buf);
        }

        gst_structure_free(network_structure);
        gst_structure_free(params_structure);

        dlclose(handle);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do \"CustomToTensorConverter\" post-processing"));
    }
    return tensors_table;
}
