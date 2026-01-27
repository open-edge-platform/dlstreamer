/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/memory_mapper.h"
#include "dlstreamer/d3d11/tensor.h"
#include "dlstreamer/openvino/context.h"
#include "dlstreamer/openvino/tensor.h"

#include <openvino/runtime/intel_gpu/remote_properties.hpp>

namespace dlstreamer {

class MemoryMapperD3D11ToOpenVINO : public BaseMemoryMapper {
  public:
    MemoryMapperD3D11ToOpenVINO(const ContextPtr &input_context, const ContextPtr &output_context)
        : BaseMemoryMapper(input_context, output_context) {
        _ov_context = *ptr_cast<OpenVINOContext>(output_context);
    }

    FramePtr map(FramePtr src, AccessMode /*mode*/) override {
        // Only NV12 supported currently
        DLS_CHECK(static_cast<ImageFormat>(src->format()) == ImageFormat::NV12);

        ov::TensorVector y_ov_tensors, uv_ov_tensors;
        y_ov_tensors.reserve(src->num_tensors());
        uv_ov_tensors.reserve(src->num_tensors());

        // Convert DLS tensors w/D3D11 texture to OV remote tensors
        for (auto dls_tensor : src) {
            auto d3d11_tensor = ptr_cast<D3D11Tensor>(dls_tensor);
            // Skip planes other than y - we don't need them for creating OV tensors.
            if (d3d11_tensor->plane_index() != 0)
                continue;
            auto [yt, uvt] = convert_to_ov_tensors(*d3d11_tensor);
            y_ov_tensors.push_back(yt);
            uv_ov_tensors.push_back(uvt);
        }

        TensorPtr res_y_tensor, res_uv_tensor;
        if (y_ov_tensors.size() > 1) {
            res_y_tensor = std::make_shared<OpenVINOTensorBatch>(std::move(y_ov_tensors), _output_context);
            res_uv_tensor = std::make_shared<OpenVINOTensorBatch>(std::move(uv_ov_tensors), _output_context);
        } else {
            res_y_tensor = std::make_shared<OpenVINOTensor>(y_ov_tensors.front(), _output_context);
            res_uv_tensor = std::make_shared<OpenVINOTensor>(uv_ov_tensors.front(), _output_context);
        }

        auto res_frame =
            std::make_shared<BaseFrame>(src->media_type(), src->format(), TensorVector{res_y_tensor, res_uv_tensor});
        res_frame->set_parent(src);
        return res_frame;
    }

  private:
    std::pair<ov::RemoteTensor, ov::RemoteTensor> convert_to_ov_tensors(D3D11Tensor &d3d11_tensor) {
        auto d3d11_texture = d3d11_tensor.d3d11_texture();
        auto &info = d3d11_tensor.info();
        dlstreamer::ImageInfo image_info(info);
        auto width = image_info.width();
        auto height = image_info.height();

        ov::AnyMap tensor_params = {{ov::intel_gpu::shared_mem_type.name(), "VA_SURFACE"},
                                    {ov::intel_gpu::dev_object_handle.name(), static_cast<void *>(d3d11_texture)},
                                    {ov::intel_gpu::va_plane.name(), uint32_t(0)}};
        auto y_tensor = _ov_context.create_tensor(ov::element::u8, {1, 1, height, width}, tensor_params);

        // FIXME: should we get width/height from second tensor ?
        tensor_params[ov::intel_gpu::va_plane.name()] = uint32_t(1);
        auto uv_tensor = _ov_context.create_tensor(ov::element::u8, {1, 2, height / 2, width / 2}, tensor_params);

        return {y_tensor, uv_tensor};
    }

  private:
    ov::RemoteContext _ov_context;
};

} // namespace dlstreamer
