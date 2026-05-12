/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "opencv_utils.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <opencv2/opencv.hpp>

namespace InferenceBackend {

namespace Utils {

int ImageToMat(const Image &src, cv::Mat &dst) {
    return CreateMat(src.planes, src.width, src.height, src.format, src.stride, src.offsets, dst);
}

int CreateMat(uint8_t *const *planes, uint32_t src_width, uint32_t src_height, int format, const uint32_t *stride,
              const uint32_t * /*offset*/, cv::Mat &dst) {
    if (!planes)
        throw std::invalid_argument("Invalid planes data pointer");
    if (!stride)
        throw std::invalid_argument("Invalid stride data pointer");

    switch (format) {
    case FOURCC_BGRX:
    case FOURCC_BGRA:
        dst = cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC4, planes[0], stride[0]);
        return FOURCC_BGRA;
    case FOURCC_RGBX:
    case FOURCC_RGBA:
        dst = cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC4, planes[0], stride[0]);
        return FOURCC_RGBA;
    case FOURCC_BGR:
        dst = cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC3, planes[0], stride[0]);
        return FOURCC_BGR;
    case FOURCC_BGRP: {
        cv::Mat channelB =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[0], stride[0]);
        cv::Mat channelG =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[1], stride[1]);
        cv::Mat channelR =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[2], stride[2]);
        std::vector<cv::Mat> channels{channelB, channelG, channelR};
        cv::merge(channels, dst);
        return FOURCC_BGRP;
    }
    case FOURCC_RGBP: {
        cv::Mat channelR =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[0], stride[0]);
        cv::Mat channelG =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[1], stride[1]);
        cv::Mat channelB =
            cv::Mat(safe_convert<int>(src_height), safe_convert<int>(src_width), CV_8UC1, planes[2], stride[2]);
        std::vector<cv::Mat> channels{channelB, channelG, channelR};
        cv::merge(channels, dst);
        return FOURCC_BGRP;
    }
    case FOURCC_I420: {
        const int32_t height = safe_convert<int32_t>((src_height % 2 == 0) ? src_height : src_height - 1);
        const int32_t width = safe_convert<int32_t>((src_width % 2 == 0) ? src_width : src_width - 1);
        const int32_t size = safe_mul(height, width);

        const int32_t half_height = height / 2;
        const int32_t half_width = width / 2;
        const int32_t quarter_size = safe_mul(half_height, half_width);

        cv::Mat yuv420;
        if (planes[1] == (planes[0] + size) and planes[2] == (planes[1] + quarter_size)) {
            // If image is provided by libav decoder, YUV planes are stored sequentially with zero strides
            yuv420 = cv::Mat(safe_add(height, half_height), width, CV_8UC1, planes[0]);
        } else {
            // If image is provided by vaapi decoder/postprocessing, YUV planes are stored with non-zero strides
            yuv420 = cv::Mat(safe_add(height, half_height), width, CV_8UC1);

            cv::Mat raw_y = cv::Mat(height, width, CV_8UC1, planes[0], stride[0]);
            cv::Mat y = cv::Mat(height, width, CV_8UC1, yuv420.data);
            raw_y.copyTo(y);

            cv::Mat raw_u = cv::Mat(half_height, half_width, CV_8UC1, planes[1], stride[1]);
            cv::Mat u = cv::Mat(half_height, half_width, CV_8UC1, yuv420.data + size);
            raw_u.copyTo(u);

            cv::Mat raw_v = cv::Mat(half_height, half_width, CV_8UC1, planes[2], stride[2]);
            cv::Mat v = cv::Mat(half_height, half_width, CV_8UC1, yuv420.data + size + quarter_size);
            raw_v.copyTo(v);
        }
        cv::cvtColor(yuv420, dst, cv::COLOR_YUV2BGR_I420);
        return FOURCC_BGR;
    }
    case FOURCC_NV12: {
        const int32_t height = safe_convert<int32_t>((src_height % 2 == 0) ? src_height : src_height - 1);
        const int32_t width = safe_convert<int32_t>((src_width % 2 == 0) ? src_width : src_width - 1);
        const int32_t size = safe_mul(height, width);

        const int32_t half_height = height / 2;
        const int32_t half_width = width / 2;

        cv::Mat yuv12 = cv::Mat(safe_add(height, half_height), width, CV_8UC1);

        cv::Mat raw_y = cv::Mat(height, width, CV_8UC1, planes[0], stride[0]);
        cv::Mat y12 = cv::Mat(height, width, CV_8UC1, yuv12.data);
        raw_y.copyTo(y12);

        cv::Mat raw_uv = cv::Mat(half_height, half_width, CV_8UC2, planes[1], stride[1]);
        cv::Mat uv12 = cv::Mat(half_height, half_width, CV_8UC2, yuv12.data + size);
        raw_uv.copyTo(uv12);

        cv::cvtColor(yuv12, dst, cv::COLOR_YUV2BGR_NV12);
        return FOURCC_BGR;
    }
    default:
        throw std::invalid_argument("Failed to create cv::Mat from image: unsupported image format.");
    }
}

template <typename T>
void MatToMultiPlaneImageTyped(const cv::Mat &src, uint32_t dst_width, uint32_t dst_height,
                               uint8_t *const *dst_planes) {
    ITT_TASK(__FUNCTION__);
    if (!dst_planes)
        throw std::invalid_argument("Invalid destination planes data pointer");

    try {
        if (src.size().height < 0 or src.size().width < 0) {
            throw std::invalid_argument("Unsupported cv::Mat size.");
        }
        const auto src_height = safe_convert<uint32_t>(src.size().height);
        const auto src_width = safe_convert<uint32_t>(src.size().width);

        if (src_height != dst_height or src_width != dst_width) {
            throw std::invalid_argument("MatToMultiPlaneImageTyped: Different height/width in cv::Mat and Image.");
        }

        int channels = src.channels();
        switch (channels) {
        case 1: {
            ITT_TASK("1-channel MatToMultiPlaneImage");
            cv::Mat_<T> wrapped_mat(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                    reinterpret_cast<T *>(dst_planes[0]));
            src.copyTo(wrapped_mat);
            break;
        }
        case 3: {
            ITT_TASK("3-channel MatToMultiPlaneImage");
            std::vector<cv::Mat_<T>> mats{cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[0])),
                                          cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[1])),
                                          cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[2]))};
            cv::split(src, mats);
            break;
        }
        case 4: {
            ITT_TASK("4-channel MatToMultiPlaneImage");
            std::vector<cv::Mat_<T>> mats{cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[0])),
                                          cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[1])),
                                          cv::Mat_<T>(safe_convert<int>(dst_height), safe_convert<int>(dst_width),
                                                      reinterpret_cast<T *>(dst_planes[2]))};
            // Get only first 3 channels
            cv::mixChannels({src}, mats, {0, 0, 1, 1, 2, 2});
            break;
        }
        default: {
            throw std::invalid_argument(
                "Failed to parse multi-plane image from cv::Mat: unsupported number of channels " +
                std::to_string(channels));
            break;
        }
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to transform one-plane cv::Mat to multi-plane cv::Mat."));
    }
}

void MatToMultiPlaneImage(const cv::Mat &src, Image &dst) {
    MatToMultiPlaneImage(src, dst.format, dst.width, dst.height, dst.planes);
}

void MatToMultiPlaneImage(const cv::Mat &src, int dst_format, uint32_t dst_width, uint32_t dst_height,
                          uint8_t *const *dst_planes) {
    switch (dst_format) {
    case FOURCC_RGBP: {
        if (src.depth() != CV_8U)
            throw std::runtime_error("Image\'s depth should be CV_8U.");

        MatToMultiPlaneImageTyped<uint8_t>(src, dst_width, dst_height, dst_planes);
        break;
    }
    case FOURCC_RGBP_F32: {
        if (src.depth() != CV_32F)
            throw std::runtime_error("Image\'s depth should be CV_FP32.");

        MatToMultiPlaneImageTyped<float>(src, dst_width, dst_height, dst_planes);
        break;
    }
    default:
        throw std::invalid_argument(
            "Failed to parse multi-plane image from cv::Mat: unsupported image format (only U8 and F32 supported).");
    }
}

cv::Mat ResizeMat(const cv::Mat &orig_image, const size_t height, const size_t width) {
    cv::Mat resized_image(orig_image);
    if (width != safe_convert<size_t>(orig_image.size().width) ||
        height != safe_convert<size_t>(orig_image.size().height)) {
        ITT_TASK("cv::resize");
        cv::resize(orig_image, resized_image, cv::Size(safe_convert<int>(width), safe_convert<int>(height)));
    }
    return resized_image;
}

void ResizeAspectRatio(cv::Mat &image, const cv::Size &dst_size,
                       const ImageTransformationParams::Ptr &image_transform_info, const size_t scale_param,
                       bool strict) {
    try {
        if (dst_size == image.size())
            return;

        ITT_TASK("ResizeAspectRatio");
        cv::Size target_dst_size(dst_size.width, dst_size.height);
        if (scale_param) {
            target_dst_size.width = safe_add(target_dst_size.width, dst_size.width / safe_convert<int>(scale_param));
            target_dst_size.height = safe_add(target_dst_size.height, dst_size.height / safe_convert<int>(scale_param));
        }
        auto orig_width = image.size().width;
        auto orig_height = image.size().height;
        double scale = 1.0;

        if (strict) {
            scale = std::min(static_cast<double>(target_dst_size.width) / orig_width,
                             static_cast<double>(target_dst_size.height) / orig_height);
        } else {
            if (orig_width <= orig_height) {
                scale = static_cast<double>(target_dst_size.width) / orig_width;
            } else {
                scale = static_cast<double>(target_dst_size.height) / orig_height;
            }
        }

        auto width = orig_width * scale;
        auto height = orig_height * scale;

        if (strict) {
            cv::Mat resized_image;
            cv::resize(image, resized_image, cv::Size(width, height));

            // background
            image = cv::Mat(target_dst_size, image.type(), {0, 0, 0});
            cv::Rect place_to_insert((target_dst_size.width - width) / 2, (target_dst_size.height - height) / 2, width,
                                     height);
            cv::Mat insertPos(image, place_to_insert);
            // inplace insertion
            resized_image.copyTo(insertPos);

            // need for post-processing
            if (image_transform_info)
                image_transform_info->AspectRatioResizeHasDone(safe_convert<size_t>(place_to_insert.x),
                                                               safe_convert<size_t>(place_to_insert.y), scale, scale);
        } else {
            Resize(image, cv::Size(width, height));
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during ResizeAspectRatio image pre-processing."));
    }
}

void Resize(cv::Mat &image, const cv::Size &dst_size) {
    try {
        if (dst_size == image.size())
            return;
        ITT_TASK("cv::resize");
        cv::resize(image, image, dst_size);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during Resize image pre-processing."));
    }
}

void Crop(cv::Mat &image, const cv::Rect &roi, const ImageTransformationParams::Ptr &image_transform_info) {
    try {
        if (roi.size() == image.size())
            return;

        ITT_TASK("Crop");
        image = image(roi);

        // need for post-processing
        if (image_transform_info)
            image_transform_info->CropHasDone(safe_convert<size_t>(roi.x), safe_convert<size_t>(roi.y));
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during Crop image pre-processing"));
    }
}

void AddPadding(cv::Mat &image, const cv::Size dst_size, size_t stride_x, size_t stride_y,
                const std::vector<double> &fill_value, const ImageTransformationParams::Ptr &image_transform_info) {
    try {
        cv::Scalar fill_value_scalar;
        try {
            switch (image.channels()) {
            case 4:
                fill_value_scalar = cv::Scalar(fill_value.at(0), fill_value.at(1), fill_value.at(2), fill_value.at(3));
                break;
            case 3:
                fill_value_scalar = cv::Scalar(fill_value.at(0), fill_value.at(1), fill_value.at(2));
                break;
            case 2:
                fill_value_scalar = cv::Scalar(fill_value.at(0), fill_value.at(1));
                break;
            case 1:
                fill_value_scalar = cv::Scalar(fill_value.at(0));
                break;
            default:
                throw std::runtime_error("Image has unsupported number of channels: " +
                                         std::to_string(image.channels()));
            }
        } catch (const std::out_of_range &e) {
            std::throw_with_nested(std::runtime_error("Failed to get values from padding's field \"fill_value\"."));
        }

        cv::Mat dst_image(dst_size, image.type(), fill_value_scalar);
        cv::Rect place_to_insert(safe_convert<int>(stride_x), safe_convert<int>(stride_y), image.size().width,
                                 image.size().height);
        cv::Mat insertPos(dst_image, place_to_insert);
        // inplace insertion
        image.copyTo(insertPos);
        image = dst_image;

        if (image_transform_info)
            image_transform_info->PaddingHasDone(stride_x, stride_y);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during AddPadding image pre-processing"));
    }
}

void Normalization(cv::Mat &image, double mean, double std) {
    ITT_TASK("cv::convertTo");
    switch (image.depth()) {
    case CV_32F:
    case CV_64F:
    case CV_16F:
        break;
    default:
        throw std::runtime_error("model_proc file specifies 'mean' and 'std' parameters, but the input data is not in "
                                 "a floating point format. You should use 'range' parameter instead.");
    }
    image.convertTo(image, CV_MAKETYPE(CV_32F, image.channels()), (1 / std), (0 - mean));
}

void Normalization(cv::Mat &image, const std::vector<double> &mean, const std::vector<double> &std) {
    ITT_TASK(__FUNCTION__);
    assert(std.size() == mean.size());
    const size_t channels_num = safe_convert<size_t>(image.channels());
    if (channels_num != mean.size())
        throw std::runtime_error("Image\'s channels number does not match with size of mean/std parameters.");

    switch (image.depth()) {
    case CV_32F:
        break;
    case CV_64F:
    case CV_16F:
        // TODO: eliminate need for conversion?
        image.convertTo(image, CV_MAKETYPE(CV_32F, image.channels()));
        break;
    default:
        throw std::runtime_error("model_proc file specifies 'mean' and 'std' parameters, but the input data is not in "
                                 "a floating point format. You should use 'range' parameter instead.");
    }

    switch (channels_num) {
    case 1:
        image.forEach<cv::Vec<float, 1>>([&mean, &std](cv::Vec<float, 1> &pixel, const int *position) -> void {
            (void)(position);

            pixel[0] = (pixel[0] - mean[0]) / std[0];
        });
        break;
    case 3:
        image.forEach<cv::Vec<float, 3>>([&mean, &std](cv::Vec<float, 3> &pixel, const int *position) -> void {
            (void)(position);

            pixel[0] = (pixel[0] - mean[0]) / std[0];
            pixel[1] = (pixel[1] - mean[1]) / std[1];
            pixel[2] = (pixel[2] - mean[2]) / std[2];
        });
        break;
    case 4:
        image.forEach<cv::Vec<float, 4>>([&mean, &std](cv::Vec<float, 4> &pixel, const int *position) -> void {
            (void)(position);

            pixel[0] = (pixel[0] - mean[0]) / std[0];
            pixel[1] = (pixel[1] - mean[1]) / std[1];
            pixel[2] = (pixel[2] - mean[2]) / std[2];
            pixel[3] = (pixel[3] - mean[3]) / std[3];
        });
        break;

    default:
        throw std::runtime_error("Unsupported image channels number.");
    }
}

void ColorSpaceConvert(const cv::Mat &orig_image, cv::Mat &result_img, const int src_color_format,
                       InputImageLayerDesc::ColorSpace target_color_format) {
    try {
        ITT_TASK(__FUNCTION__);

        switch (target_color_format) {
        case InputImageLayerDesc::ColorSpace::BGR:
            switch (src_color_format) {
            case FOURCC_RGB:
                cv::cvtColor(orig_image, result_img, cv::COLOR_RGB2BGR);
                break;
            case FOURCC_RGBA:
            case FOURCC_RGBX:
            case FOURCC_RGBP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_RGBA2BGR);
                break;
            case FOURCC_BGRA:
            case FOURCC_BGRX:
            case FOURCC_BGRP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_BGRA2BGR);
                break;
            default:
                throw std::runtime_error("Color-space conversion for your format has not been implemented yet.");
                break;
            }
            break;
        case InputImageLayerDesc::ColorSpace::RGB:
            switch (src_color_format) {
            case FOURCC_BGR:
                cv::cvtColor(orig_image, result_img, cv::COLOR_BGR2RGB);
                break;
            case FOURCC_RGBA:
            case FOURCC_RGBX:
            case FOURCC_RGBP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_RGBA2RGB);
                break;
            case FOURCC_BGRA:
            case FOURCC_BGRX:
            case FOURCC_BGRP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_BGRA2RGB);
                break;
            default:
                throw std::runtime_error("Color-space conversion for your format has not been implemented yet.");
                break;
            }
            break;
        case InputImageLayerDesc::ColorSpace::GRAYSCALE:
            switch (src_color_format) {
            case FOURCC_BGR:
                cv::cvtColor(orig_image, result_img, cv::COLOR_BGR2GRAY);
                break;
            case FOURCC_RGBA:
            case FOURCC_RGBX:
            case FOURCC_RGBP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_RGBA2GRAY);
                break;
            case FOURCC_BGRA:
            case FOURCC_BGRX:
            case FOURCC_BGRP:
                cv::cvtColor(orig_image, result_img, cv::COLOR_BGRA2GRAY);
                break;
            default:
                throw std::runtime_error("Color-space conversion for your format has not been implemented yet.");
                break;
            }
            break;
        case InputImageLayerDesc::ColorSpace::YUV:
            throw std::runtime_error("Color-space conversion to YUV has not been implemented yet.");
            break;
        default:
            throw std::runtime_error("Color-space conversion for your format has not been implemented yet.");
            break;
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed during ColorSpaceConvert image pre-processing."));
    }
}

} // namespace Utils

} // namespace InferenceBackend
