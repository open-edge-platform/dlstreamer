/*******************************************************************************
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/context.h"
#include "dlstreamer/tensor.h"

#ifdef _WIN32
#include <gst/d3d11/gstd3d11device.h>
#include <openvino/runtime/intel_gpu/ocl/dx.hpp>
#else
#include <openvino/runtime/intel_gpu/ocl/va.hpp>
#endif

namespace dlstreamer {

class OpenVINOContext : public BaseContext {
  public:
    struct key {
        static constexpr auto ov_remote_context = "ov_remote_context"; // ov::RemoteContext*
        static constexpr auto cl_context = BaseContext::key::cl_context;
    };

    OpenVINOContext() : BaseContext(MemoryType::OpenVINO) {
    }

    OpenVINOContext(ov::Core core, const std::string &device) : BaseContext(MemoryType::OpenVINO) {
        //_remote_context = core.create_context(device, {});
        _remote_context = core.get_default_context(device);
    }

    OpenVINOContext(ov::Core core, const std::string &device, ContextPtr context) : BaseContext(MemoryType::OpenVINO) {
        ov::AnyMap context_params;
        if (device.find("GPU") != std::string::npos && context) {
#ifdef _WIN32
            auto d3d_device = context->handle(BaseContext::key::d3d_device);
            if (d3d_device) {
                auto gst_device = static_cast<GstD3D11Device *>(d3d_device);
                _remote_context = ov::intel_gpu::ocl::D3DContext(core, gst_d3d11_device_get_device_handle(gst_device));
            }
#else
            auto va_display = context->handle(BaseContext::key::va_display);
            if (va_display) {
                int tile_id =
                    static_cast<int>(reinterpret_cast<intptr_t>(context->handle(BaseContext::key::va_tile_id)));
                _remote_context = ov::intel_gpu::ocl::VAContext(core, static_cast<void *>(va_display), tile_id);
            }
#endif
        }
        if (!_remote_context)
            _remote_context = core.get_default_context(device);
    }

    OpenVINOContext(ov::CompiledModel compiled_model) : BaseContext(MemoryType::OpenVINO) {
        try {
            _remote_context = compiled_model.get_context();
        } catch (...) {
            // Remote context not available for CPU device
        }
    }

    ov::RemoteContext remote_context() {
        return _remote_context;
    }

    template <typename T>
    T remote_context() {
        return _remote_context.as<T>();
    }

    operator ov::RemoteContext() {
        return _remote_context;
    }

    inline MemoryMapperPtr get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) override;

    std::vector<std::string> keys() const override {
        return {key::cl_context};
    }

    handle_t handle(std::string_view key) const noexcept override {
        if (key == key::ov_remote_context || key.empty())
            return handle_t(&_remote_context);
        if (key == key::cl_context) {
            auto ctx_params = _remote_context.get_params();
            auto ocl_context = ctx_params.find(ov::intel_gpu::ocl_context.name());
            if (ocl_context != ctx_params.end())
                return ocl_context->second.as<void *>();
        }
        return nullptr;
    }

  protected:
    ov::RemoteContext _remote_context;
};

using OpenVINOContextPtr = std::shared_ptr<OpenVINOContext>;

} // namespace dlstreamer

/////////////////////////////////////////////////////////////////////////////
// Mappers

#include "dlstreamer/openvino/mappers/cpu_to_openvino.h"
#include "dlstreamer/openvino/mappers/opencl_to_openvino.h"
#include "dlstreamer/openvino/mappers/openvino_to_cpu.h"
#include "dlstreamer/openvino/mappers/vaapi_to_openvino.h"

namespace dlstreamer {

MemoryMapperPtr OpenVINOContext::get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) {
    auto mapper = BaseContext::get_mapper(input_context, output_context);
    if (mapper)
        return mapper;

    auto input_type = input_context ? input_context->memory_type() : MemoryType::CPU;
    auto output_type = output_context ? output_context->memory_type() : MemoryType::CPU;
    if (input_type == MemoryType::CPU && output_type == MemoryType::OpenVINO)
        return std::make_shared<MemoryMapperCPUToOpenVINO>(input_context, output_context);
    if (input_type == MemoryType::VAAPI && output_type == MemoryType::OpenVINO)
        return std::make_shared<MemoryMapperVAAPIToOpenVINO>(input_context, output_context);
    if (input_type == MemoryType::OpenCL && output_type == MemoryType::OpenVINO)
        return std::make_shared<MemoryMapperOpenCLToOpenVINO>(input_context, output_context);
    if (input_type == MemoryType::OpenVINO && output_type == MemoryType::CPU)
        return std::make_shared<MemoryMapperOpenVINOToCPU>(input_context, output_context);

    BaseContext::attach_mapper(mapper);
    return mapper;
}

} // namespace dlstreamer
