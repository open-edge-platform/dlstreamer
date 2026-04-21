/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "meta_extractor.h"
#include "dlstreamer/gst/metadata/watermark_circle_meta.h"
#include "dlstreamer/gst/metadata/watermark_draw_meta.h"
#include "dlstreamer/gst/metadata/watermark_text_meta.h"

std::vector<render::Prim> MetaExtractor::extractWatermarkPrimitives(GstBuffer *buffer) {
    std::vector<render::Prim> prims;

    auto text_prims = extractTextPrimitives(buffer);
    prims.insert(prims.end(), text_prims.begin(), text_prims.end());

    auto shape_prims = extractShapePrimitives(buffer);
    prims.insert(prims.end(), shape_prims.begin(), shape_prims.end());

    auto circle_prims = extractCirclePrimitives(buffer);
    prims.insert(prims.end(), circle_prims.begin(), circle_prims.end());

    return prims;
}

std::vector<render::Prim> MetaExtractor::extractTextPrimitives(GstBuffer *buffer) {
    std::vector<render::Prim> prims;

    gpointer state = nullptr;
    GstMeta *meta = nullptr;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, watermark_text_meta_api_get_type()))) {
        auto text_meta = (WatermarkTextMeta *)meta;
        render::Text text_prim;
        text_prim.text = text_meta->text;
        text_prim.org = cv::Point((int)text_meta->pos.x, (int)text_meta->pos.y);
        text_prim.color = cv::Scalar((double)text_meta->r, (double)text_meta->g, (double)text_meta->b);
        text_prim.thick = text_meta->thickness;
        prims.push_back(text_prim);
    }

    return prims;
}

std::vector<render::Prim> MetaExtractor::extractShapePrimitives(GstBuffer *buffer) {
    std::vector<render::Prim> prims;

    gpointer state = nullptr;
    GstMeta *meta = nullptr;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, watermark_draw_meta_api_get_type()))) {
        auto draw_meta = (WatermarkDrawMeta *)meta;
        if (draw_meta->point_count == 2) {
            render::Line line_prim;
            line_prim.pt1 = cv::Point((int)draw_meta->points[0].x, (int)draw_meta->points[0].y);
            line_prim.pt2 = cv::Point((int)draw_meta->points[1].x, (int)draw_meta->points[1].y);
            line_prim.color = cv::Scalar((double)draw_meta->r, (double)draw_meta->g, (double)draw_meta->b);
            line_prim.thick = draw_meta->thickness;
            prims.push_back(line_prim);
        } else if (draw_meta->point_count > 2) {
            render::Polygon polygon_prim;
            for (guint i = 0; i < draw_meta->point_count; i++) {
                polygon_prim.points.push_back(cv::Point((int)draw_meta->points[i].x, (int)draw_meta->points[i].y));
            }
            polygon_prim.color = cv::Scalar((double)draw_meta->r, (double)draw_meta->g, (double)draw_meta->b);
            polygon_prim.thick = draw_meta->thickness;
            prims.push_back(polygon_prim);
        }
    }

    return prims;
}

std::vector<render::Prim> MetaExtractor::extractCirclePrimitives(GstBuffer *buffer) {
    std::vector<render::Prim> prims;

    gpointer state = nullptr;
    GstMeta *meta = nullptr;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, watermark_circle_meta_api_get_type()))) {
        auto circle_meta = (WatermarkCircleMeta *)meta;
        render::Circle circle_prim;
        circle_prim.center = cv::Point((int)circle_meta->center.x, (int)circle_meta->center.y);
        circle_prim.radius = circle_meta->radius;
        circle_prim.color = cv::Scalar((double)circle_meta->r, (double)circle_meta->g, (double)circle_meta->b);
        circle_prim.thick = circle_meta->thickness;
        prims.push_back(circle_prim);
    }

    return prims;
}
