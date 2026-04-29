/*******************************************************************************
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image_inference.h"

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

// TODO: Do we need OV wrappers?
class OpenvinoInputTensor : public OpenvinoInputBlob {
    ov::Tensor _tensor;
    mutable ov::Shape _shape;

  public:
    OpenvinoInputTensor(ov::Tensor tensor) : _tensor(std::move(tensor)) {
    }

    OpenvinoInputTensor(ov::Tensor tensor, size_t batch_index) : OpenvinoInputBlob(batch_index), _tensor(std::move(tensor)) {
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
        switch (_tensor.get_element_type()) {
        case ov::element::u8:
            return InferenceBackend::Blob::Precision::U8;
        case ov::element::f32:
            return InferenceBackend::Blob::Precision::FP32;
        case ov::element::f16:
            return InferenceBackend::Blob::Precision::FP16;
        case ov::element::bf16:
            return InferenceBackend::Blob::Precision::BF16;
        case ov::element::f64:
            return InferenceBackend::Blob::Precision::FP64;
        case ov::element::i16:
            return InferenceBackend::Blob::Precision::I16;
        case ov::element::i32:
            return InferenceBackend::Blob::Precision::I32;
        case ov::element::i64:
            return InferenceBackend::Blob::Precision::I64;
        case ov::element::u4:
            return InferenceBackend::Blob::Precision::U4;
        case ov::element::u16:
            return InferenceBackend::Blob::Precision::U16;
        case ov::element::u32:
            return InferenceBackend::Blob::Precision::U32;
        case ov::element::u64:
            return InferenceBackend::Blob::Precision::U64;

        default:
            throw std::runtime_error(std::string("unsupported element type: ") +
                                     _tensor.get_element_type().get_type_name().c_str());
        }
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
        switch (_tensor.get_element_type()) {
        case ov::element::u8:
            return InferenceBackend::Blob::Precision::U8;
        case ov::element::f32:
            return InferenceBackend::Blob::Precision::FP32;
        case ov::element::f16:
            return InferenceBackend::Blob::Precision::FP16;
        case ov::element::bf16:
            return InferenceBackend::Blob::Precision::BF16;
        case ov::element::f64:
            return InferenceBackend::Blob::Precision::FP64;
        case ov::element::i16:
            return InferenceBackend::Blob::Precision::I16;
        case ov::element::i32:
            return InferenceBackend::Blob::Precision::I32;
        case ov::element::i64:
            return InferenceBackend::Blob::Precision::I64;
        case ov::element::u4:
            return InferenceBackend::Blob::Precision::U4;
        case ov::element::u16:
            return InferenceBackend::Blob::Precision::U16;
        case ov::element::u32:
            return InferenceBackend::Blob::Precision::U32;
        case ov::element::u64:
            return InferenceBackend::Blob::Precision::U64;

        default:
            throw std::runtime_error(std::string("unsupported element type: ") +
                                     _tensor.get_element_type().get_type_name().c_str());
        }
    }

    const void *GetData() const override {
        return _tensor.data();
    }
};