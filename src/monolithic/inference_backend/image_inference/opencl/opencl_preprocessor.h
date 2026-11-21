/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenclPreprocessor: the OpenCL pre-processing backend (pre-process-backend=opencl) of OpenVinoImageInference.
//
// One OpenCL kernel does the color conversion, crop, resize and padding of a frame or ROI and writes the result
// straight into the model input, u8 NCHW in the model's channel order: OpenVINO™ pre-processing only converts the
// precision and applies mean/scale. The frames may be VA surfaces or DMA-BUFs (Linux), D3D11 textures (Windows) or
// system memory; the inference device may be any:
//   - GPU inference, frames on that GPU (or in system memory): the kernel runs in OpenVINO™'s OpenCL context and
//     writes into device memory that the infer request reads in place;
//   - other devices (CPU, NPU, AUTO, ...): the kernel runs on the GPU the frames are on, in a context of its own,
//     and writes into USM host memory that the infer request reads as a host tensor.
//
// Each infer request has one model input for its whole batch (OpenclBatch), bound to it once. Add() puts a frame
// into an element of the batch. The ROIs of a frame that follow each other in a batch are pre-processed by one
// kernel launch, issued when the next frame comes or the batch is finished; Finish() waits for all of them before
// the inference starts. Frames are imported (or uploaded) once and shared by their ROIs.
//
// Not thread-safe: the caller serializes all calls.

#pragma once

#include "opencl_frame_source.h"
#include "opencl_preproc_kernels.h"

#include "dlstreamer/context.h"
#include "inference_backend/image.h"
#include "inference_backend/input_image_layer_descriptor.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace InferenceBackend {

// The model input of one infer request: [batch, 3, height, width] u8, written by OpenclPreprocessor.
struct OpenclBatch {
    // The model input: device memory (cl_mem) when the outputs are on the device, otherwise USM host memory. On a
    // discrete GPU the kernels write host outputs into device memory too, copied to the host memory in one transfer.
    cl_mem device_memory = nullptr;
    void *host_memory = nullptr;

    // State of OpenclPreprocessor.
    cl_mem roi_table = nullptr;
    std::vector<opencl_preproc::RoiParams> rois; // one entry per batch element, never resized
    size_t num_rois = 0;                         // entries in use
    size_t group_first = 0;                      // first entry not launched yet
    OpenclSourcePlanesPtr group_planes;          // frame of the entries not launched yet
    uint32_t group_frame_width = 0;
    uint32_t group_frame_height = 0;
    std::vector<OpenclSourcePlanesPtr> launched; // frames read by the launched kernels, kept until they completed
};

class OpenclPreprocessor {
  public:
    struct Settings {
        uint32_t width = 0;  // model input
        uint32_t height = 0; // model input
        bool rgb = false;    // channel order of the model: RGB, otherwise BGR
        size_t batch_size = 1;
    };

    // device_context: OpenVINO™'s OpenCL context, for GPU inference, in which the outputs go to device memory. If
    // null, the preprocessor creates its own context on the GPU of app_context and the outputs go to USM host memory.
    // app_context: the VA display or D3D11 device of the frames, null for system memory frames.
    OpenclPreprocessor(cl_context device_context, dlstreamer::ContextPtr app_context, const Settings &settings);
    ~OpenclPreprocessor();

    OpenclPreprocessor(const OpenclPreprocessor &) = delete;
    OpenclPreprocessor &operator=(const OpenclPreprocessor &) = delete;

    bool OutputsOnDevice() const;
    const Settings &GetSettings() const;

    // A new model input, owned by the preprocessor.
    OpenclBatch &CreateBatch();

    // Enqueues the pre-processing of image->rect (the whole frame if empty) into batch element `index`. Returns
    // without waiting for the GPU. On failure nothing is added.
    void Add(OpenclBatch &batch, size_t index, const ImagePtr &image, const InputImageLayerDesc::Ptr &pre_proc_info,
             const ImageTransformationParams::Ptr &image_transform_info);

    // Enqueues what is not yet and waits until the whole batch is written. The batch is empty afterwards, also if
    // this throws.
    void Finish(OpenclBatch &batch);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace InferenceBackend
