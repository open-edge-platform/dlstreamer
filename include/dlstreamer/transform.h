/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/element.h"

namespace dlstreamer {

/**
 * @brief Abstract interface for transform elements. Transform element has one input and one output. Transform element
 * is responsible for allocating output frame/tensor.
 */
class Transform : public Element {
  public:
    /**
     * @brief The function notifies element about input information. Subsequent call to get_output_info() should
     * take into consideration input information passed via this function.
     * @param info Input frames information
     */
    virtual void set_input_info(const FrameInfo &info) = 0;

    /**
     * @brief The function notifies element about output information. Subsequent call to get_input_info() should
     * take into consideration output information passed via this function.
     * @param info Output frames information
     */
    virtual void set_output_info(const FrameInfo &info) = 0;

    /**
     * @brief Returns input information supported by element. It may depend on output information previously set by
     * set_output_info().
     */
    virtual FrameInfoVector get_input_info() = 0;

    /**
     * @brief Returns output information supported by element. It may depend on input information previously set by
     * set_input_info().
     */
    virtual FrameInfoVector get_output_info() = 0;

    /**
     * @brief Run processing on input and output tensors.
     * @param src Input tensor
     * @param dst Output tensor
     */
    virtual bool process(TensorPtr src, TensorPtr dst) = 0;

    /**
     * @brief Run processing on input and output frames.
     * @param src Input frame
     * @param dst Output frame
     */
    virtual bool process(FramePtr src, FramePtr dst) = 0;

    /**
     * @brief Process input tensor and return output tensor.
     * @param src Input frame
     * @param dst Output frame
     */
    virtual TensorPtr process(TensorPtr src) = 0;

    /**
     * @brief Process input frame and return output frame.
     * @param src Input frame
     * @param dst Output frame
     */
    virtual FramePtr process(FramePtr src) = 0;
};

/**
 * @brief Abstract interface for in-place transform elements. Transform element doesn't allocate new frames/tensors,
 * it modified input frames/tensors.
 */
class TransformInplace : public Element {
  public:
    /**
     * @brief The function notifies element about frame information.
     * @param info Frame information
     */
    virtual void set_info(const FrameInfo &info) = 0;

    /**
     * @brief Process tensor.
     * @param src Tensor
     */
    virtual bool process(TensorPtr src) = 0;

    /**
     * @brief Process frame.
     * @param src Frame
     */
    virtual bool process(FramePtr src) = 0;
};

} // namespace dlstreamer
