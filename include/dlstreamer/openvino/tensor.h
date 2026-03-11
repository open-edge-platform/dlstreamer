/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/tensor.h"
#include "dlstreamer/openvino/utils.h"
#include <functional>
#include <vector>

namespace dlstreamer {

namespace tensor::key {
static constexpr auto ov_tensor = "ov_tensor"; // (ov::Tensor*)
};

class OpenVINOTensor : public BaseTensor {
  public:
    OpenVINOTensor(ov::Tensor tensor, ContextPtr context, std::function<void()> wait_function = nullptr)
        : BaseTensor(MemoryType::OpenVINO, ov_tensor_to_tensor_info(tensor), tensor::key::ov_tensor, context),
          _ov_tensor(tensor), _wait_function(wait_function) {
        set_handle(tensor::key::ov_tensor, reinterpret_cast<handle_t>(&_ov_tensor));
    }

    void *data() const {
        if (_wait_function)
            _wait_function();
        return const_cast<void *>(_ov_tensor.data());
    }

    ov::Tensor ov_tensor() const {
        return _ov_tensor;
    }

    operator ov::Tensor() const {
        return _ov_tensor;
    }

  protected:
    ov::Tensor _ov_tensor;
    std::function<void()> _wait_function;
};

using OpenVINOTensorPtr = std::shared_ptr<OpenVINOTensor>;

// FIXME: Introduce OpenVINOTensorBase?
// Holds several ov::Tensor for batched input
class OpenVINOTensorBatch : public OpenVINOTensor {
  public:
    OpenVINOTensorBatch(ov::TensorVector tensors, ContextPtr context)
        : OpenVINOTensor(tensors.front(), context), _ov_tensors_vec(std::move(tensors)) {

        // All tensors must have the same shape
        const auto &shape = _ov_tensors_vec.front().get_shape();
        for (const auto &t : _ov_tensors_vec) {
            if (t.get_shape() != shape)
                throw std::runtime_error("OpenVINOTensorBatch: all tensors must have the same shape");
        }

        // FIXME: assume that first dimension is batch (N)
        assert(_info.shape.front() == 1);
        _info.shape.front() = _ov_tensors_vec.size();
        _info = TensorInfo(_info.shape, _info.dtype);
    }

    void *data() const override {
        // exception
        return BaseTensor::data();
    }

    const ov::TensorVector &tensors() const {
        return _ov_tensors_vec;
    }

  protected:
    ov::TensorVector _ov_tensors_vec;
};

} // namespace dlstreamer
