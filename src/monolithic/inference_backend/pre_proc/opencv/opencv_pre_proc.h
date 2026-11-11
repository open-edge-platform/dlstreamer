/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#ifndef OPENCV_PRE_PROC_H
#define OPENCV_PRE_PROC_H

#include "inference_backend/pre_proc.h"
#include <functional>
#include <opencv2/opencv.hpp>

namespace InferenceBackend {

class OpenCV_VPP : public ImagePreprocessor {
  public:
    using ImageProcessingCallback = std::function<cv::Mat(const cv::Mat &)>;
    using ImageProcessingFunctionPtr = cv::Mat (*)(const cv::Mat &); // Function pointer type

    // Constructor with optional user library path
    OpenCV_VPP(const std::string &user_library_path = "");
    ~OpenCV_VPP();

    // PreProc interface
    void Convert(const Image &src, Image &dst, const InputImageLayerDesc::Ptr &pre_proc_info,
                 const ImageTransformationParams::Ptr &image_transform_info, bool make_planar = true,
                 bool allocate_destination = false);
    void ReleaseImage(const Image &);

    // Method to set user-defined callback
    void setUserCallback(const ImageProcessingCallback &callback);

  private:
    ImageProcessingCallback user_callback; // Store user callback
    void *library_handle = nullptr;

    // Private member functions
    cv::Mat CustomImageConvert(const cv::Mat &orig_image, const int src_color_format, const cv::Size &input_size,
                               const InputImageLayerDesc::Ptr &pre_proc_info,
                               const ImageTransformationParams::Ptr &image_transform_info);

    void CopyImage(const Image &src, Image &dst);

    // Static utility function to load user library
    ImageProcessingCallback loadUserLibrary(const std::string &library_path);
};

} // namespace InferenceBackend

#endif // OPENCV_PRE_PROC_H