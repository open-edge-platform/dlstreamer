/*******************************************************************************
 * Copyright (C) 2023-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Wrappers that present an ov::Tensor through the backend-neutral InferenceBackend::InputBlob / OutputBlob interfaces:
// OpenvinoInputTensor for the pre-processing functions, OpenvinoOutputTensor for the post-processing converters.

#pragma once

#include "inference_backend/image_inference.h"

#include <openvino/openvino.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace InferenceBackend {
namespace OpenVino {

// Throws for element types that have no Blob::Precision.
inline Blob::Precision to_blob_precision(const ov::element::Type &type) {
    switch (type) {
    case ov::element::u8:
        return Blob::Precision::U8;
    case ov::element::f32:
        return Blob::Precision::FP32;
    case ov::element::f16:
        return Blob::Precision::FP16;
    case ov::element::bf16:
        return Blob::Precision::BF16;
    case ov::element::f64:
        return Blob::Precision::FP64;
    case ov::element::i16:
        return Blob::Precision::I16;
    case ov::element::i32:
        return Blob::Precision::I32;
    case ov::element::i64:
        return Blob::Precision::I64;
    case ov::element::u4:
        return Blob::Precision::U4;
    case ov::element::u16:
        return Blob::Precision::U16;
    case ov::element::u32:
        return Blob::Precision::U32;
    case ov::element::u64:
        return Blob::Precision::U64;

    default:
        throw std::runtime_error(std::string("unsupported element type: ") + type.get_type_name().c_str());
    }
}

} // namespace OpenVino
} // namespace InferenceBackend

class OpenvinoInputBlob : public InferenceBackend::InputBlob {
  public:
    OpenvinoInputBlob() : index(0) {
    }
    OpenvinoInputBlob(size_t batch_index) : index(batch_index) {
    }
    ~OpenvinoInputBlob() = default;

    size_t GetIndexInBatch() const override {
        return index;
    }

  protected:
    size_t index;
};

// TODO: the InputBlob/OutputBlob interfaces are only implemented by OpenVINO now; consider using ov::Tensor directly
// in the pre- and post-processing code.
class OpenvinoInputTensor : public OpenvinoInputBlob {
    ov::Tensor _tensor;
    mutable ov::Shape _shape;

  public:
    OpenvinoInputTensor(ov::Tensor tensor) : _tensor(std::move(tensor)) {
    }

    OpenvinoInputTensor(ov::Tensor tensor, size_t batch_index)
        : OpenvinoInputBlob(batch_index), _tensor(std::move(tensor)) {
    }

    const std::vector<size_t> &GetDims() const override {
        if (_shape.empty())
            _shape = _tensor.get_shape();
        return _shape;
    }

    InferenceBackend::Blob::Layout GetLayout() const override {
        // FIXME
        return InferenceBackend::Blob::Layout::ANY;
    }

    InferenceBackend::Blob::Precision GetPrecision() const override {
        return InferenceBackend::OpenVino::to_blob_precision(_tensor.get_element_type());
    }

    void *GetData() {
        return _tensor.data();
    }
};

class OpenvinoOutputTensor : public InferenceBackend::OutputBlob {
    ov::Tensor _tensor;
    mutable ov::Shape _shape;

  public:
    OpenvinoOutputTensor(ov::Tensor tensor) : _tensor(std::move(tensor)) {
    }

    const std::vector<size_t> &GetDims() const override {
        if (_shape.empty())
            _shape = _tensor.get_shape();
        return _shape;
    }

    InferenceBackend::Blob::Layout GetLayout() const override {
        // FIXME
        return InferenceBackend::Blob::Layout::ANY;
    }

    InferenceBackend::Blob::Precision GetPrecision() const override {
        return InferenceBackend::OpenVino::to_blob_precision(_tensor.get_element_type());
    }

    const void *GetData() const override {
        return _tensor.data();
    }
};