/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/opencv/elements/opencv_tensor_normalize.h"
#include "dlstreamer/base/transform.h"
#include "dlstreamer/cpu/frame_alloc.h"
#include "dlstreamer/memory_mapper_factory.h"
#include "dlstreamer/opencv/tensor.h"
#include "safe_arithmetic.hpp"

namespace dlstreamer {

namespace param {
static constexpr auto range = "range";
static constexpr auto mean = "mean";
static constexpr auto std = "std";
}; // namespace param

static ParamDescVector params_desc = {
    {param::range, "Normalization range MIN, MAX. Example: <0,1>", std::vector<double>()},
    {param::mean, "Mean values per channel. Example: <0.485,0.456,0.406>", std::vector<double>()},
    {param::std, "Standard deviation values per channel. Example: <0.229,0.224,0.225>", std::vector<double>()},
};

class OpencvTensorNormalize : public BaseTransform {
  public:
    OpencvTensorNormalize(DictionaryCPtr params, const ContextPtr &app_context) : BaseTransform(app_context) {
        _range = params->get<std::vector<double>>(param::range, {});
        _mean = params->get<std::vector<double>>(param::mean, {});
        _std = params->get<std::vector<double>>(param::std, {});
    }

    FrameInfoVector get_input_info() override {
        if (_output_info.tensors.empty()) {
            return opencv_tensor_normalize.input_info();
        } else {
            FrameInfo info = _output_info;
            info.tensors[0].dtype = DataType::UInt8;
            if (!info.tensors[0].shape.empty())
                info.tensors[0].stride = dlstreamer::contiguous_stride(info.tensors[0].shape, info.tensors[0].dtype);
            return {info};
        }
    }

    FrameInfoVector get_output_info() override {
        if (_input_info.tensors.empty()) {
            return opencv_tensor_normalize.output_info();
        } else {
            FrameInfo info = _input_info;
            info.tensors[0].dtype = DataType::Float32;
            if (!info.tensors[0].shape.empty())
                info.tensors[0].stride = dlstreamer::contiguous_stride(info.tensors[0].shape, info.tensors[0].dtype);
            return {info};
        }
    }

    std::function<FramePtr()> get_output_allocator() override {
        return [this]() { return std::make_shared<CPUFrameAlloc>(_output_info); };
    }

    bool process(TensorPtr src, TensorPtr dst) override {
        auto src_tensor = src.map(AccessMode::Read);
        auto dst_tensor = dst.map(AccessMode::Write);
        ImageInfo src_info(src_tensor->info());
        ImageInfo dst_info(dst_tensor->info());
        int w = src_info.width();
        int h = src_info.height();
        size_t mat_size = safe_convert<size_t>(w) * safe_convert<size_t>(h);
        int channels = src_info.channels();
        int batch = src_info.batch();

        uint8_t *src_data = src_tensor->data<uint8_t>();
        float *dst_data = dst_tensor->data<float>();
        size_t src_w_stride = src_info.width_stride();
        size_t dst_w_stride = dst_info.width_stride();
        size_t src_c_stride = (batch > 1) ? src_info.channels_stride() : 0;
        size_t dst_c_stride = (batch > 1) ? dst_info.channels_stride() : 0;

        for (int j = 0; j < batch; j++) {
            for (int i = 0; i < channels; i++) {
                double alpha = 1;
                double beta = 0;
                if (!_range.empty()) {
                    alpha *= (_range[1] - _range[0]) / 255.f;
                    beta += _range[0];
                }
                if (!_std.empty()) {
                    alpha *= _std[i];
                }
                if (!_mean.empty()) {
                    beta += _mean[i];
                }
                cv::Mat src_mat = cv::Mat(h, w, CV_8UC1, src_data + i * mat_size, src_w_stride);
                cv::Mat dst_mat = cv::Mat(h, w, CV_32FC1, dst_data + i * mat_size, dst_w_stride);
                src_mat.convertTo(dst_mat, dst_mat.type(), alpha, beta);
            }
            src_data += src_c_stride;
            dst_data += dst_c_stride;
        }
        return true;
    }

  private:
    std::vector<double> _range;
    std::vector<double> _mean;
    std::vector<double> _std;
};

extern "C" {
ElementDesc opencv_tensor_normalize = {
    .name = "opencv_tensor_normalize",
    .description = "Convert U8 tensor to F32 tensor with normalization",
    .author = "Intel Corporation",
    .params = &params_desc,
    .input_info = MAKE_FRAME_INFO_VECTOR({FrameInfo(MediaType::Tensors, MemoryType::CPU, {{{}, DataType::UInt8}})}),
    .output_info = MAKE_FRAME_INFO_VECTOR({FrameInfo(MediaType::Tensors, MemoryType::CPU, {{{}, DataType::Float32}})}),
    .create = create_element<OpencvTensorNormalize>,
    .flags = 0};
}

} // namespace dlstreamer
