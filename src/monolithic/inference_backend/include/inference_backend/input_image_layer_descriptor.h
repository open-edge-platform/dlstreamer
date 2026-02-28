/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "image.h"
#include "safe_arithmetic.hpp"

#include <memory>
#include <vector>

namespace InferenceBackend {
class InputImageLayerDesc {
  public:
    using Ptr = std::shared_ptr<InputImageLayerDesc>;

    enum class Resize { NO, NO_ASPECT_RATIO, ASPECT_RATIO, ASPECT_RATIO_PAD };
    enum class Crop { NO, CENTRAL, CENTRAL_RESIZE, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };
    enum class ColorSpace { NO, RGB, BGR, YUV, GRAYSCALE };

    enum class Normalization { NO, RANGE, DISTRIBUTION };
    struct RangeNormalization {
      private:
        const bool defined = false;

      public:
        const double min = 0;
        const double max = 1;

        RangeNormalization() = default;
        RangeNormalization(double min, double max) : defined(true), min(min), max(max) {
        }

        bool isDefined() const {
            return defined;
        }
    };
    struct DistribNormalization {
      private:
        const bool defined = false;

      public:
        // standart normalization values for models, which was pretrained on ImageNet dataset
        const std::vector<double> mean = {0.485, 0.456, 0.406};
        const std::vector<double> std = {0.229, 0.224, 0.225};

        DistribNormalization() = default;
        DistribNormalization(const std::vector<double> mean, const std::vector<double> std)
            : defined(true), mean(mean), std(std) {
        }

        bool isDefined() const {
            return defined;
        }
    };

    struct Padding {
      private:
        bool defined = false;

      public:
        const size_t stride_x = 0;
        const size_t stride_y = 0;
        const std::vector<double> fill_value = {0, 0, 0};

        Padding() = default;
        Padding(size_t stride) : defined(stride), stride_x(stride), stride_y(stride) {
        }
        Padding(size_t stride, const std::vector<double> &fill_value)
            : defined(stride || !fill_value.empty()), stride_x(stride), stride_y(stride), fill_value(fill_value) {
        }
        Padding(size_t stride_x, size_t stride_y)
            : defined(stride_x || stride_y), stride_x(stride_x), stride_y(stride_y) {
        }
        Padding(size_t stride_x, size_t stride_y, const std::vector<double> &fill_value)
            : defined(stride_x || stride_y || !fill_value.empty()), stride_x(stride_x), stride_y(stride_y),
              fill_value(fill_value) {
        }

        bool isDefined() const {
            return defined;
        }
    };

    InputImageLayerDesc() = default;
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space)
        : resize(resize), crop(crop), color_space(color_space) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, double norm_range_min, double norm_range_max)
        : resize(resize), crop(crop), color_space(color_space), range_norm(norm_range_min, norm_range_max) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, double norm_range_min, double norm_range_max,
                        const std::vector<double> &mean, const std::vector<double> &std)
        : resize(resize), crop(crop), color_space(color_space), range_norm(norm_range_min, norm_range_max),
          distrib_norm(mean, std) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, const std::vector<double> &mean,
                        const std::vector<double> &std)
        : resize(resize), crop(crop), color_space(color_space), distrib_norm(mean, std) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, const RangeNormalization &norm)
        : resize(resize), crop(crop), color_space(color_space), range_norm(norm) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, const DistribNormalization &norm)
        : resize(resize), crop(crop), color_space(color_space), distrib_norm(norm) {
        setDefaultToBlobSizeTransformationIsItNeed();
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, const RangeNormalization &range_norm,
                        const DistribNormalization &distrib_norm)
        : resize(resize), crop(crop), color_space(color_space), range_norm(range_norm), distrib_norm(distrib_norm) {
    }
    InputImageLayerDesc(Resize resize, Crop crop, ColorSpace color_space, const RangeNormalization &range_norm,
                        const DistribNormalization &distrib_norm, const Padding &padding)
        : resize(resize), crop(crop), color_space(color_space), range_norm(range_norm), distrib_norm(distrib_norm),
          padding(padding) {
    }

    bool isTransformationToBlobSizeDefined() const {
        if (resize != Resize::NO or crop != Crop::NO)
            return true;
        return false;
    }
    bool isDefined() const {
        if (isTransformationToBlobSizeDefined() or color_space != ColorSpace::NO or range_norm.isDefined() or
            distrib_norm.isDefined())
            return true;
        return false;
    }
    bool doNeedResize() const {
        return resize != Resize::NO;
    }
    Resize getResizeType() const {
        return resize;
    }
    bool doNeedCrop() const {
        if (crop == Crop::NO or resize == Resize::NO_ASPECT_RATIO)
            return false;
        return true;
    }
    Crop getCropType() const {
        return crop;
    }
    bool doNeedColorSpaceConversion(ColorSpace src_color_space) const {
        if (color_space == src_color_space or color_space == ColorSpace::NO)
            return false;
        return true;
    }
    bool doNeedColorSpaceConversion(int src_color_space) const {
        if (color_space == ColorSpace::NO)
            return false;

        if ((src_color_space == FOURCC_BGR && color_space == ColorSpace::BGR) ||
            (src_color_space == FOURCC_RGB && color_space == ColorSpace::RGB) ||
            (src_color_space == FOURCC_YUV && color_space == ColorSpace::YUV))
            return false;

        return true;
    }
    ColorSpace getTargetColorSpace() const {
        return color_space;
    }
    bool doNeedRangeNormalization() const {
        return range_norm.isDefined();
    }
    const RangeNormalization &getRangeNormalization() const {
        return range_norm;
    }
    bool doNeedDistribNormalization() const {
        return distrib_norm.isDefined();
    }
    const DistribNormalization &getDistribNormalization() const {
        return distrib_norm;
    }
    bool doNeedPadding() const {
        return padding.isDefined();
    }
    const Padding &getPadding() const {
        return padding;
    }

  private:
    Resize resize;
    const Crop crop;
    const ColorSpace color_space;
    const RangeNormalization range_norm;
    const DistribNormalization distrib_norm;
    const Padding padding;

    void setDefaultToBlobSizeTransformationIsItNeed() {
        if (isDefined() and not isTransformationToBlobSizeDefined()) {
            this->resize = Resize::NO_ASPECT_RATIO;
        }
    }
};

class ImageTransformationParams {
  protected:
    bool was_resize = false;
    bool was_crop = false;
    bool was_aspect_ratio_resize = false;
    bool was_padding = false;

  public:
    using Ptr = std::shared_ptr<ImageTransformationParams>;

    double resize_scale_x = 1;
    double resize_scale_y = 1;

    size_t padding_size_x = 0;
    size_t padding_size_y = 0;

    size_t croped_border_size_x = 0;
    size_t croped_border_size_y = 0;

    bool WasTransformation() {
        return (was_aspect_ratio_resize || was_crop || was_padding || was_resize);
    }

    void CropHasDone(size_t _cropped_frame_size_x, size_t _cropped_frame_size_y) {
        was_crop = true;
        croped_border_size_x = safe_add(croped_border_size_x, _cropped_frame_size_x);
        croped_border_size_y = safe_add(croped_border_size_y, _cropped_frame_size_y);
    }
    bool WasCrop() const {
        return was_crop;
    }

    void AspectRatioResizeHasDone(size_t _padding_size_x, size_t _padding_size_y, double _resize_scale_x,
                                  double _resize_scale_y) {
        was_aspect_ratio_resize = true;
        PaddingHasDone(_padding_size_x, _padding_size_y);
        ResizeHasDone(_resize_scale_x, _resize_scale_y);
    }
    bool WasAspectRatioResize() const {
        return was_aspect_ratio_resize;
    }

    void ResizeHasDone(double _resize_scale_x, double _resize_scale_y) {
        was_resize = true;
        resize_scale_x *= _resize_scale_x;
        resize_scale_y *= _resize_scale_y;
    }
    bool WasResize() const {
        return was_resize;
    }

    void PaddingHasDone(size_t _padding_size_x, size_t _padding_size_y) {
        was_padding = true;
        padding_size_x = safe_add(padding_size_x, _padding_size_x);
        padding_size_y = safe_add(padding_size_y, _padding_size_y);
    }
    bool WasPadding() const {
        return was_padding;
    }
};
} // namespace InferenceBackend
