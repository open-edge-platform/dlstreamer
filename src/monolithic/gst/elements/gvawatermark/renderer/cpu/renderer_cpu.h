/*******************************************************************************
 * Copyright (C) 2020-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/memory_mapper.h"
#include "iostream"
#include "renderer.h"

class RendererCPU : public Renderer {
  public:
    RendererCPU(std::shared_ptr<ColorConverter> color_converter, dlstreamer::MemoryMapperPtr buffer_mapper)
        : Renderer(color_converter), _buffer_mapper(std::move(buffer_mapper)) {
    }
    ~RendererCPU();

  protected:
    dlstreamer::FramePtr buffer_map(dlstreamer::FramePtr buffer) override;
    void DrawRotatedRectangle(cv::InputOutputArray img, cv::Point pt1, cv::Point pt2, double rotation,
                              const cv::Scalar &color, int thickness = 1, int lineType = cv::LINE_8, int shift = 0);

    dlstreamer::MemoryMapperPtr _buffer_mapper;
};

class RendererYUV : public RendererCPU {
  public:
    RendererYUV(std::shared_ptr<ColorConverter> color_converter, dlstreamer::MemoryMapperPtr buffer_mapper)
        : RendererCPU(color_converter, std::move(buffer_mapper)) {
    }

  protected:
    void draw_backend(std::vector<cv::Mat> &image_planes, std::vector<render::Prim> &prims) override;

    virtual void draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) = 0;
    virtual void draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) = 0;
    virtual void draw_text(std::vector<cv::Mat> &mats, render::Text text) = 0;
    virtual void draw_text_bg(std::vector<cv::Mat> &mats, render::Text text) = 0;
    virtual void draw_line(std::vector<cv::Mat> &mats, render::Line line) = 0;
    virtual void draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) = 0;
    virtual void draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) = 0;

    void draw_rect_y_plane(cv::Mat &y, cv::Point2i pt1, cv::Point2i pt2, double rotation, double color, int thick);
};

class RendererI420 : public RendererYUV {
  public:
    RendererI420(std::shared_ptr<ColorConverter> color_converter, dlstreamer::MemoryMapperPtr buffer_mapper)
        : RendererYUV(color_converter, std::move(buffer_mapper)) {
    }

  protected:
    void draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) override;
    void draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) override;
    void draw_text(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_text_bg(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_line(std::vector<cv::Mat> &mats, render::Line line) override;
    void draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) override;
    void draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) override;
};

class RendererNV12 : public RendererYUV {
  public:
    RendererNV12(std::shared_ptr<ColorConverter> color_converter, dlstreamer::MemoryMapperPtr buffer_mapper)
        : RendererYUV(color_converter, std::move(buffer_mapper)) {
    }

  protected:
    void draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) override;
    void draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) override;
    void draw_text(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_text_bg(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_line(std::vector<cv::Mat> &mats, render::Line line) override;
    void draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) override;
    void draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) override;
};

class RendererBGR : public RendererYUV {
  public:
    RendererBGR(std::shared_ptr<ColorConverter> color_converter, dlstreamer::MemoryMapperPtr buffer_mapper)
        : RendererYUV(color_converter, std::move(buffer_mapper)) {
    }

  protected:
    void draw_rectangle(std::vector<cv::Mat> &mats, render::Rect rect) override;
    void draw_circle(std::vector<cv::Mat> &mats, render::Circle circle) override;
    void draw_text(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_text_bg(std::vector<cv::Mat> &mats, render::Text text) override;
    void draw_line(std::vector<cv::Mat> &mats, render::Line line) override;
    void draw_instance_mask(std::vector<cv::Mat> &mats, render::InstanceSegmantationMask mask) override;
    void draw_semantic_mask(std::vector<cv::Mat> &mats, render::SemanticSegmantationMask mask) override;
};
