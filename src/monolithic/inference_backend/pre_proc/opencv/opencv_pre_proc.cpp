/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#include "opencv_pre_proc.h"
#include "inference_backend/logger.h"
#include "opencv_utils.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include <dlfcn.h> // For dynamic loading on Unix-like systems
#include <iostream>
#include <opencv2/opencv.hpp>

namespace GlobUtils = Utils;
using namespace InferenceBackend;
using namespace InferenceBackend::Utils;

ImagePreprocessor *InferenceBackend::CreatePreProcOpenCV(const std::string custom_preproc_lib) {
    return new OpenCV_VPP(custom_preproc_lib);
}

OpenCV_VPP::OpenCV_VPP(const std::string &user_library_path) {
    // Load the user-defined library and set the callback if a path is provided
    if (!user_library_path.empty()) {
        ImageProcessingCallback user_callback = loadUserLibrary(user_library_path);
        if (user_callback) {
            setUserCallback(user_callback);
        }
    }
}

OpenCV_VPP::~OpenCV_VPP() {
    if (library_handle) {
        dlclose(library_handle);
    }
}

void OpenCV_VPP::setUserCallback(const ImageProcessingCallback &callback) {
    user_callback = callback;
}

cv::Rect OpenCV_VPP::centralCropROI(const cv::Mat &image) {
    int height = image.rows;
    int width = image.cols;
    int cropSize = std::min(height, width);
    int startX = (width - cropSize) / 2;
    int startY = (height - cropSize) / 2;
    return cv::Rect(startX, startY, cropSize, cropSize);
}

void OpenCV_VPP::CopyImage(const Image &src, Image &dst) {
    uint32_t planes_count = ::Utils::GetPlanesCount(src.format);
    for (uint32_t i = 0; i < planes_count; i++) {
        uint32_t dst_stride = src.width;
        uint32_t src_stride = (src.width == src.stride[i]) ? src.width : src.stride[i];
        for (uint32_t row = 0; row < src.height; ++row) {
            memcpy(dst.planes[i] + row * dst_stride, src.planes[i] + row * src_stride, dst_stride);
        }
        dst.stride[i] = dst_stride;
    }
}

cv::Mat OpenCV_VPP::CustomImageConvert(const cv::Mat &orig_image, const int src_color_format,
                                       const cv::Size &input_size, const InputImageLayerDesc::Ptr &pre_proc_info,
                                       const ImageTransformationParams::Ptr &image_transform_info) {
    try {
        cv::Mat processed_image = orig_image;

        // Invoke user-defined callback if it exists
        if (user_callback) {
            processed_image = user_callback(orig_image);
            // cv::imwrite("output_image.jpg", processed_image); // testing
        }

        if (!pre_proc_info)
            throw std::runtime_error("Pre-processor info for custom image pre-processing is null.");

        // Padding
        int padding_x = 0;
        int padding_y = 0;
        std::vector<double> fill_value;
        if (pre_proc_info->doNeedPadding()) {
            const auto &padding = pre_proc_info->getPadding();
            padding_x = safe_convert<int>(padding.stride_x);
            padding_y = safe_convert<int>(padding.stride_y);
            fill_value = padding.fill_value;
        }

        if (padding_x > std::numeric_limits<int>::max() / 2 || padding_y > std::numeric_limits<int>::max() / 2 ||
            input_size.width < 0 || input_size.height < 0) {
            throw std::range_error("Invalid padding or range");
        }

        cv::Size input_size_except_padding(input_size.width - (padding_x * 2), input_size.height - (padding_y * 2));

        // Resize
        cv::Mat image_to_insert = processed_image;
        if (pre_proc_info->doNeedResize() && processed_image.size() != input_size_except_padding) {
            double additional_crop_scale_param = 1;
            if (pre_proc_info->doNeedCrop() && pre_proc_info->doNeedResize()) {
                additional_crop_scale_param = 1.125;
            }

            double resize_scale_param_x =
                safe_convert<double>(input_size_except_padding.width) / processed_image.size().width;
            double resize_scale_param_y =
                safe_convert<double>(input_size_except_padding.height) / processed_image.size().height;

            if ((pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO) ||
                (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO_PAD)) {
                resize_scale_param_x = resize_scale_param_y = std::min(resize_scale_param_x, resize_scale_param_y);
            }

            resize_scale_param_x *= additional_crop_scale_param;
            resize_scale_param_y *= additional_crop_scale_param;

            cv::Size after_resize(processed_image.size().width * resize_scale_param_x,
                                  processed_image.size().height * resize_scale_param_y);

            ITT_TASK("cv::resize");
            cv::resize(processed_image, image_to_insert, after_resize);

            if (image_transform_info)
                image_transform_info->ResizeHasDone(resize_scale_param_x, resize_scale_param_y);
        }

        // Crop
        if (pre_proc_info->doNeedCrop() && image_to_insert.size() != input_size_except_padding) {
            size_t cropped_border_x = 0;
            size_t cropped_border_y = 0;
            if (image_to_insert.size().width > input_size_except_padding.width)
                cropped_border_x =
                    safe_convert<size_t>(safe_add(safe_convert<int64_t>(image_to_insert.size().width),
                                                  0 - safe_convert<int64_t>(input_size_except_padding.width)));
            if (image_to_insert.size().height > input_size_except_padding.height)
                cropped_border_y =
                    safe_convert<size_t>(safe_add(safe_convert<int64_t>(image_to_insert.size().height),
                                                  0 - safe_convert<int64_t>(input_size_except_padding.height)));

            cv::Size crop_rect_size(image_to_insert.size().width - safe_convert<int>(cropped_border_x),
                                    image_to_insert.size().height - safe_convert<int>(cropped_border_y));
            cv::Point2f top_left_rect_point;

            if (pre_proc_info->getCropType() == InputImageLayerDesc::Crop::CENTRAL_RESIZE) {
                cv::Rect crop_rect = centralCropROI(processed_image);
                image_to_insert = processed_image;
                Crop(image_to_insert, crop_rect, image_transform_info);
                cv::resize(image_to_insert, image_to_insert, crop_rect_size, cv::INTER_CUBIC);
            } else {
                switch (pre_proc_info->getCropType()) {
                case InputImageLayerDesc::Crop::CENTRAL:
                    top_left_rect_point = cv::Point2f(cropped_border_x / 2, cropped_border_y / 2);
                    break;
                case InputImageLayerDesc::Crop::TOP_LEFT:
                    top_left_rect_point = cv::Point2f(0, 0);
                    break;
                case InputImageLayerDesc::Crop::TOP_RIGHT:
                    top_left_rect_point = cv::Point2f(cropped_border_x, 0);
                    break;
                case InputImageLayerDesc::Crop::BOTTOM_LEFT:
                    top_left_rect_point = cv::Point2f(0, cropped_border_y);
                    break;
                case InputImageLayerDesc::Crop::BOTTOM_RIGHT:
                    top_left_rect_point = cv::Point2f(cropped_border_x, cropped_border_y);
                    break;
                default:
                    throw std::runtime_error("Unknown crop format.");
                }

                cv::Rect crop_rect(top_left_rect_point, crop_rect_size);
                Crop(image_to_insert, crop_rect, image_transform_info);
            }
        }

        // Color Space Conversion
        if (pre_proc_info->doNeedColorSpaceConversion(src_color_format)) {
            ColorSpaceConvert(image_to_insert, image_to_insert, src_color_format, pre_proc_info->getTargetColorSpace());
        }

        // Set background color
        cv::Scalar background_color;
        if (fill_value.empty()) {
            fill_value.resize(image_to_insert.channels());
            std::fill(fill_value.begin(), fill_value.end(), 0);
        }
        try {
            switch (image_to_insert.channels()) {
            case 4:
                background_color = cv::Scalar(fill_value.at(0), fill_value.at(1), fill_value.at(2), fill_value.at(3));
                break;
            case 3:
                background_color = cv::Scalar(fill_value.at(0), fill_value.at(1), fill_value.at(2));
                break;
            case 2:
                background_color = cv::Scalar(fill_value.at(0), fill_value.at(1));
                break;
            case 1:
                background_color = cv::Scalar(fill_value.at(0));
                break;
            default:
                throw std::runtime_error("Image has unsupported number of channels: " +
                                         std::to_string(image_to_insert.channels()));
            }
        } catch (const std::out_of_range &e) {
            std::throw_with_nested(std::runtime_error("Failed to get values from padding's field \"fill_value\"."));
        }

        int shift_x = (input_size.width - image_to_insert.size().width) / 2;
        int shift_y = (input_size.height - image_to_insert.size().height) / 2;

        if (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO_PAD) {
            shift_x = 0;
            shift_y = 0;
        }

        cv::Rect region_to_insert(shift_x, shift_y, image_to_insert.size().width, image_to_insert.size().height);

        cv::Mat result(input_size, image_to_insert.type(), background_color);
        cv::Mat region_image_to_insert(result, region_to_insert);
        image_to_insert.copyTo(region_image_to_insert);

        if (image_transform_info)
            image_transform_info->PaddingHasDone(safe_convert<size_t>(shift_x), safe_convert<size_t>(shift_y));

        return result;
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed custom image pre-processing."));
    }
    return cv::Mat();
}

void OpenCV_VPP::Convert(const Image &raw_src, Image &dst, const InputImageLayerDesc::Ptr &pre_proc_info,
                         const ImageTransformationParams::Ptr &image_transform_info, bool make_planar,
                         bool allocate_destination) {
    ITT_TASK("OpenCV_VPP");

    try {
        if (allocate_destination) {
            throw std::invalid_argument("allocate_destination set to true is not supported");
        }

        Image src = ApplyCrop(raw_src);
        if (!needPreProcessing(raw_src, dst)) {
            CopyImage(raw_src, dst);
        }

        cv::Mat src_mat_image;
        cv::Mat dst_mat_image;

        auto converted_format = ImageToMat(src, src_mat_image);

        if (needCustomImageConvert(pre_proc_info)) {
            if (dst.height > safe_convert<uint32_t>(std::numeric_limits<int>::max()) and
                dst.width > safe_convert<uint32_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Image size too large.");

            cv::Size dst_size(safe_convert<int>(dst.width), safe_convert<int>(dst.height));
            dst_mat_image =
                CustomImageConvert(src_mat_image, converted_format, dst_size, pre_proc_info, image_transform_info);
        } else {
            dst_mat_image = ResizeMat(src_mat_image, dst.height, dst.width);
        }

        if (make_planar) {
            MatToMultiPlaneImage(dst_mat_image, dst);
        } else {
            if (GlobUtils::GetPlanesCount(dst.format) != 1)
                throw std::runtime_error(
                    "Formats with more than one plane could not be processed with `make_planar=false`");
            auto channels_count = GlobUtils::GetChannelsCount(dst.format);
            if (channels_count > CV_DEPTH_MAX)
                throw std::range_error("Number of channels exceeds OpenCV maximum (8)");
            auto cv_type = CV_MAKE_TYPE(CV_8U, safe_convert<int>(channels_count));
            cv::Mat dst_mat(safe_convert<int>(dst.height), safe_convert<int>(dst.width), cv_type, dst.planes[0],
                            dst.stride[0]);
            dst_mat_image.copyTo(dst_mat);
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during OpenCV image pre-processing"));
    }
}

void OpenCV_VPP::ReleaseImage(const Image &) {
}

// Static utility function to load user library
OpenCV_VPP::ImageProcessingCallback OpenCV_VPP::loadUserLibrary(const std::string &library_path) {
    library_handle = dlopen(library_path.c_str(), RTLD_LAZY);
    if (!library_handle) {
        std::cerr << "Failed to load library: " << dlerror() << std::endl;
        return nullptr;
    }

    // Use a function pointer type to retrieve the function
    ImageProcessingFunctionPtr process_func = (ImageProcessingFunctionPtr)dlsym(library_handle, "process_image");
    if (!process_func) {
        std::cerr << "Failed to load function: " << dlerror() << std::endl;
        dlclose(library_handle);
        return nullptr;
    }

    // Wrap the function pointer in a std::function
    return ImageProcessingCallback(process_func);
}