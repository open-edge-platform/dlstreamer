/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "nms.h"
#include "mtcnn_common.h"
#include "safe_arithmetic.hpp"
#include "utils.h"
#include "video_frame.h"
#include <gst/analytics/analytics.h>
#include <inference_backend/logger.h>

namespace {

gint compare(gconstpointer _c1, gconstpointer _c2) {
    assert(_c1 && _c2 && "Expected valid pointers to compare");
    const FaceCandidate *c1 = static_cast<const FaceCandidate *>(_c1);
    const FaceCandidate *c2 = static_cast<const FaceCandidate *>(_c2);

    return (c1->score > c2->score) ? -1 : ((c1->score == c2->score) ? 0 : 1);
}

gfloat iou(FaceCandidate *a, FaceCandidate *b, NMSMode mode) {
    assert(a && b && "Expected valid pointers FaceCandidate");
    auto x1 = std::max(a->x, b->x);
    auto y1 = std::max(a->y, b->y);
    auto x2 = std::min(safe_add(a->x, a->width), safe_add(b->x, b->width));
    auto y2 = std::min(safe_add(a->y, a->height), safe_add(b->y, b->height));
    auto w = std::max(0u, (x2 - x1 + 1));
    auto h = std::max(0u, (y2 - y1 + 1));
    // To avoid integer overflow, we cast to float and then do the multiplication
    float inter = safe_convert<float>(w) * safe_convert<float>(h);
    float areaA = safe_convert<float>(a->width) * safe_convert<float>(a->height);
    float areaB = safe_convert<float>(b->width) * safe_convert<float>(b->height);
    float overlap;

    if (mode == NMS_MIN)
        overlap = inter / MIN(areaA, areaB);
    else
        overlap = inter / (areaA + areaB - inter);

    return (overlap >= 0) ? overlap : 0;
}

void _nms(GArray *results, NMSMode mode, float threshold) {
    assert(results && "Expected valid pointer GArray");

    guint num;
    guint i, j;

    g_array_sort(results, compare);
    num = results->len;

    for (i = 0; i < num; i++) {
        FaceCandidate *first = &g_array_index(results, FaceCandidate, i);
        if (first->valid == TRUE) {
            for (j = i + 1; j < num; j++) {
                FaceCandidate *candi = &g_array_index(results, FaceCandidate, j);
                if (candi->valid == TRUE) {
                    if (iou(first, candi, mode) > threshold) {
                        candi->valid = FALSE;
                    }
                }
            }
        }
    }

    i = 0;
    do {
        FaceCandidate *c = &g_array_index(results, FaceCandidate, i);
        if (c->valid == FALSE)
            g_array_remove_index(results, i);
        else
            i++;
    } while (i < results->len);
}

gboolean process_pnet_nms(GstGvaNms *nms, GstBuffer *buffer) {
    assert(nms && buffer && "Expected valid pointers GstGvaNms and GstBuffer");

    GVA::VideoFrame video_frame(buffer, nms->info);
    std::vector<GVA::RegionOfInterest> regions = video_frame.regions();

    GArray *candidates = g_array_new(FALSE, TRUE, sizeof(FaceCandidate));

    if (!nms->merge) {
        for (const GVA::RegionOfInterest &roi : regions) {
            auto rect = roi.rect();
            for (const GVA::Tensor &tensor : roi.tensors()) {
                if ((tensor.name() == "bboxregression") && (tensor.has_field("score"))) {
                    FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                    candidate.x = rect.x;
                    candidate.y = rect.y;
                    candidate.width = rect.w;
                    candidate.height = rect.h;
                    candidate.score = tensor.get_double("score");
                    g_array_append_val(candidates, candidate);
                    video_frame.remove_region(roi);
                }
            }
        }

        if (candidates->len > 0)
            _nms(candidates, NMS_UNION, .5f);
    } else {
        for (const GVA::RegionOfInterest &roi : regions) {
            auto rect = roi.rect();
            for (const GVA::Tensor &tensor : roi.tensors()) {
                if (tensor.name() == "nms" and tensor.has_field("score")) {
                    FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                    candidate.x = rect.x;
                    candidate.y = rect.y;
                    candidate.width = rect.w;
                    candidate.height = rect.h;
                    candidate.score = tensor.get_double("score");
                    g_array_append_val(candidates, candidate);
                }
            }
        }

        gst_buffer_foreach_meta(buffer, foreach_meta_remove_one,
                                GSIZE_TO_POINTER(GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));
        if (candidates->len > 0)
            _nms(candidates, NMS_UNION, .7f);
    }

    for (size_t i = 0; i < candidates->len; i++) {
        FaceCandidate *face_candidate = &g_array_index(candidates, FaceCandidate, i);
        GVA::RegionOfInterest roi =
            video_frame.add_region(face_candidate->x, face_candidate->y, face_candidate->width, face_candidate->height);
        roi.add_tensor("nms").set_double("score", face_candidate->score);
    }

    g_array_free(candidates, TRUE);

    return TRUE;
}

gboolean process_rnet_nms(GstGvaNms *nms, GstBuffer *buffer) {
    assert(nms && buffer && "Expected valid pointers GstGvaNms and GstBuffer");

    GVA::VideoFrame video_frame(buffer, nms->info);
    std::vector<GVA::RegionOfInterest> regions = video_frame.regions();

    GArray *candidates = g_array_new(FALSE, TRUE, sizeof(FaceCandidate));

    for (const GVA::RegionOfInterest &roi : regions) {
        auto rect = roi.rect();
        for (const GVA::Tensor &tensor : roi.tensors()) {
            if ((tensor.name() == "bboxregression") && (tensor.has_field("score"))) {
                FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                candidate.x = rect.x;
                candidate.y = rect.y;
                candidate.width = rect.w;
                candidate.height = rect.h;
                candidate.score = tensor.get_double("score");
                g_array_append_val(candidates, candidate);
            }
        }
    }

    gst_buffer_foreach_meta(buffer, foreach_meta_remove_one,
                            GSIZE_TO_POINTER(GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));

    if (candidates->len > 0)
        _nms(candidates, NMS_UNION, .7f);

    for (size_t i = 0; i < candidates->len; i++) {
        FaceCandidate *face_candidate = &g_array_index(candidates, FaceCandidate, i);
        GVA::RegionOfInterest roi =
            video_frame.add_region(face_candidate->x, face_candidate->y, face_candidate->width, face_candidate->height);
        roi.add_tensor("nms").set_double("score", face_candidate->score);
    }

    g_array_free(candidates, TRUE);

    return TRUE;
}

gboolean process_onet_nms(GstGvaNms *nms, GstBuffer *buffer) {
    assert(nms && buffer && "Expected valid pointers GstGvaNms and GstBuffer");

    GVA::VideoFrame video_frame(buffer, nms->info);
    std::vector<GVA::RegionOfInterest> regions = video_frame.regions();

    GArray *candidates = g_array_new(FALSE, TRUE, sizeof(FaceCandidate));

    for (const GVA::RegionOfInterest &roi : regions) {
        auto rect = roi.rect();
        for (const GVA::Tensor &tensor : roi.tensors()) {
            if (tensor.name() == "bboxregression") {
                double score = 0.0;
                int32_t left_eye_x, right_eye_x, nose_x, mouth_left_x, mouth_right_x = 0;
                int32_t left_eye_y, right_eye_y, nose_y, mouth_left_y, mouth_right_y = 0;
                if (gst_structure_get(tensor.gst_structure(), "score", G_TYPE_DOUBLE, &score, "left_eye_x", G_TYPE_INT,
                                      &left_eye_x, "right_eye_x", G_TYPE_INT, &right_eye_x, "nose_x", G_TYPE_INT,
                                      &nose_x, "mouth_left_x", G_TYPE_INT, &mouth_left_x, "mouth_right_x", G_TYPE_INT,
                                      &mouth_right_x, "left_eye_y", G_TYPE_INT, &left_eye_y, "right_eye_y", G_TYPE_INT,
                                      &right_eye_y, "nose_y", G_TYPE_INT, &nose_y, "mouth_left_y", G_TYPE_INT,
                                      &mouth_left_y, "mouth_right_y", G_TYPE_INT, &mouth_right_y, NULL)) {
                    FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                    candidate.x = rect.x;
                    candidate.y = rect.y;
                    candidate.width = rect.w;
                    candidate.height = rect.h;
                    candidate.score = score;
                    candidate.left_eye_x = left_eye_x;
                    candidate.right_eye_x = right_eye_x;
                    candidate.nose_x = nose_x;
                    candidate.mouth_left_x = mouth_left_x;
                    candidate.mouth_right_x = mouth_right_x;
                    candidate.left_eye_y = left_eye_y;
                    candidate.right_eye_y = right_eye_y;
                    candidate.nose_y = nose_y;
                    candidate.mouth_left_y = mouth_left_y;
                    candidate.mouth_right_y = mouth_right_y;
                    g_array_append_val(candidates, candidate);
                }
            }
        }
    }

    gst_buffer_foreach_meta(buffer, foreach_meta_remove_one,
                            GSIZE_TO_POINTER(GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));

    if (candidates->len > 0)
        _nms(candidates, NMS_MIN, .7f);

    for (size_t i = 0; i < candidates->len; i++) {
        FaceCandidate *c = &g_array_index(candidates, FaceCandidate, i);
        float landmarks[] = {safe_convert<float>(c->left_eye_x - c->x) / c->width,
                             safe_convert<float>(c->left_eye_y - c->y) / c->height,
                             safe_convert<float>(c->right_eye_x - c->x) / c->width,
                             safe_convert<float>(c->right_eye_y - c->y) / c->height,
                             safe_convert<float>(c->nose_x - c->x) / c->width,
                             safe_convert<float>(c->nose_y - c->y) / c->height,
                             safe_convert<float>(c->mouth_left_x - c->x) / c->width,
                             safe_convert<float>(c->mouth_left_y - c->y) / c->height,
                             safe_convert<float>(c->mouth_right_x - c->x) / c->width,
                             safe_convert<float>(c->mouth_right_y - c->y) / c->height};
        GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, landmarks, sizeof(landmarks), 1);
        gsize n_elem = 0;

        GstStructure *params =
            gst_structure_new("landmarks", "format", G_TYPE_STRING, "landmark_points", "data_buffer", G_TYPE_VARIANT, v,
                              "data", G_TYPE_POINTER, g_variant_get_fixed_array(v, &n_elem, 1), NULL);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);

        if (not relation_meta)
            throw std::runtime_error("Failed to add GstAnalyticsRelationMeta to buffer");

        GstAnalyticsODMtd od_mtd;
        if (!gst_analytics_relation_meta_add_od_mtd(relation_meta, 0, c->x, c->y, c->width, c->height, 0, &od_mtd)) {
            throw std::runtime_error("Failed to add roi data to meta");
        }

        GstAnalyticsMtd tensor_mtd;
        GVA::Tensor gva_tensor(params);
        if (gva_tensor.convert_to_meta(&tensor_mtd, relation_meta, c->x, c->y, c->width, c->height)) {
            if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id,
                                                          tensor_mtd.id)) {
                throw std::runtime_error(
                    "Failed to set relation between object detection metadata and tensor metadata");
            }
            if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                          tensor_mtd.id, od_mtd.id)) {
                throw std::runtime_error(
                    "Failed to set relation between tensor metadata and object detection metadata");
            }
        }

        GstVideoRegionOfInterestMeta *meta =
            gst_buffer_add_video_region_of_interest_meta(buffer, 0, c->x, c->y, c->width, c->height);
        meta->id = od_mtd.id;
        gst_video_region_of_interest_meta_add_param(meta, params);
    }

    g_array_free(candidates, TRUE);

    return TRUE;
}

} // namespace

gboolean non_max_suppression(GstGvaNms *nms, GstBuffer *buffer) {
    if (!nms) {
        GST_ERROR("nms failed: GvaNms is null");
        return FALSE;
    }

    if (!buffer) {
        GST_ELEMENT_ERROR(nms, STREAM, FAILED, ("nms failed"), ("%s", "GstBuffer is null"));
        return FALSE;
    }

    try {
        switch (nms->mode) {
        case MODE_PNET:
            return process_pnet_nms(nms, buffer);
        case MODE_RNET:
            return process_rnet_nms(nms, buffer);
        case MODE_ONET:
            return process_onet_nms(nms, buffer);
        default:
            throw std::invalid_argument("Unknown nms mode");
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(nms, STREAM, FAILED, ("nms failed"), ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return FALSE;
    }
}
