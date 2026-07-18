/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "bbox_regression.h"
#include "gva_utils.h"
#include "mtcnn_common.h"
#include "safe_arithmetic.hpp"
#include "utils.h"
#include "video_frame.h"
#include <gst/analytics/analytics.h>
#include <inference_backend/logger.h>

namespace {

gboolean process_pnet_output(GstGvaBBoxRegression *bboxregression, GstBuffer *buffer) {
    assert(bboxregression && buffer && "Expected valid pointers GstGvaBBoxRegression and GstBuffer");

    GVA::VideoFrame video_frame(buffer, bboxregression->info);
    std::vector<GVA::Tensor> tensors = video_frame.tensors();
    std::vector<float> conv_blob;
    std::vector<float> prob_blob;

    if (tensors.size() != 2) {
        GVA_ERROR("Invalid tensor vector size");
        return FALSE;
    }

    for (const GVA::Tensor &tensor : tensors) {
        if (tensor.get_int("tensor_id") == 0)
            conv_blob = tensor.data<float>();
        else
            prob_blob = tensor.data<float>();
    }

    if (conv_blob.empty() || prob_blob.empty()) {
        GVA_ERROR("Empty tensor");
        return FALSE;
    }

    std::vector<guint> dims = tensors[0].dims();
    const size_t output_width = dims[3];
    const size_t output_height = dims[2];

    const size_t sx = 1;
    const size_t sy = sx * output_width;
    const size_t sz = safe_mul(sy, output_height);
    const float scale_factor =
        safe_convert<float>(bboxregression->info->height) / safe_convert<float>(PNET_IN_SIZE(output_height));

    for (size_t y = 0; y < output_height; ++y) {
        for (size_t x = 0; x < output_width; ++x) {
            double score = PROB_MAP(prob_blob, x, y, sx, sy, sz);
            if (score > PNET_THRESHOLD) {
                float bb1_x = (safe_convert<float>(x * PNET_WINDOW_STEP) + 1.0f) * scale_factor;
                float bb1_y = (safe_convert<float>(y * PNET_WINDOW_STEP) + 1.0f) * scale_factor;
                float bb2_x =
                    (safe_convert<float>(x * PNET_WINDOW_STEP + PNET_SCAN_WINDOW_SIZE) + 1.0f - 1.0f) * scale_factor;
                float bb2_y =
                    (safe_convert<float>(y * PNET_WINDOW_STEP + PNET_SCAN_WINDOW_SIZE) + 1.0f - 1.0f) * scale_factor;
                float bb_width = bb2_x - bb1_x;
                float bb_height = bb2_y - bb1_y;

                float bb_left = bb1_x + OUT_MAP(conv_blob, x, y, 0, sx, sy, sz) * bb_width;
                float bb_top = bb1_y + OUT_MAP(conv_blob, x, y, 1, sx, sy, sz) * bb_height;
                float bb_right = bb2_x + OUT_MAP(conv_blob, x, y, 2, sx, sy, sz) * bb_width;
                float bb_bottom = bb2_y + OUT_MAP(conv_blob, x, y, 3, sx, sy, sz) * bb_height;

                float w = bb_right - bb_left;
                float h = bb_bottom - bb_top;
                float l = MAX(w, h);

                float candi_x = bb_left + w * 0.5f - l * 0.5f;
                float candi_y = bb_top + h * 0.5f - l * 0.5f;
                float candi_w = l;
                float candi_h = l;

                GVA::RegionOfInterest roi = video_frame.add_region(candi_x, candi_y, candi_w, candi_h);
                roi.add_tensor("bboxregression").set_double("score", score);
            }
        }
    }
    gst_buffer_foreach_meta(buffer, foreach_meta_remove_one, GSIZE_TO_POINTER(gst_gva_tensor_meta_api_get_type()));

    return TRUE;
}

gboolean process_rnet_output(GstGvaBBoxRegression *bboxregression, GstBuffer *buffer) {
    assert(bboxregression && buffer && "Expected valid pointers GstGvaBBoxRegression and GstBuffer");

    GVA::VideoFrame video_frame(buffer, bboxregression->info);

    // FIXME: use std::vector
    GArray *candidates = g_array_new(FALSE, TRUE, sizeof(FaceCandidate));

    for (const GVA::RegionOfInterest &roi : video_frame.regions()) {
        auto rect = roi.rect();
        FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<float> conv_blob, prob_blob;

        for (const GVA::Tensor &tensor : roi.tensors()) {
            if ((tensor.name() == "nms") && (tensor.has_field("score"))) {
                candidate.x = rect.x;
                candidate.y = rect.y;
                candidate.width = rect.w;
                candidate.height = rect.h;
                candidate.score = tensor.get_double("score");
            } else if (tensor.layer_name() == RNET_OUT_CONV_NAME)
                conv_blob = tensor.data<float>();
            else if (tensor.layer_name() == RNET_OUT_PROB_NAME)
                prob_blob = tensor.data<float>();
        }

        if ((prob_blob.size() < SCORE_MAP_INDEX) || (conv_blob.empty())) {
            GVA_ERROR("Invalid tensor blob");
            return FALSE;
        }

        double score = prob_blob[SCORE_MAP_INDEX];
        if (score > RNET_THRESHOLD) {
            float left = safe_convert<float>(candidate.x) + conv_blob[0] * candidate.width;
            float top = safe_convert<float>(candidate.y) + conv_blob[1] * candidate.height;
            float right = safe_convert<float>(candidate.x) + candidate.width - 1.f + conv_blob[2] * candidate.width;
            float bottom = safe_convert<float>(candidate.y) + candidate.height - 1.f + conv_blob[3] * candidate.height;

            float width = right - left + 1;
            float height = bottom - top + 1;
            float length = MAX(width, height);

            candidate.x = safe_convert<uint32_t>(left + width * 0.5f - length * 0.5f);
            candidate.y = safe_convert<uint32_t>(top + height * 0.5f - length * 0.5f);
            candidate.height = candidate.width = safe_convert<uint32_t>(length);
            candidate.score = score;
            g_array_append_val(candidates, candidate);
        }
    }

    gst_buffer_foreach_meta(buffer, foreach_meta_remove_one,
                            GSIZE_TO_POINTER(GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));

    for (size_t i = 0; i < candidates->len; i++) {
        FaceCandidate *c = &g_array_index(candidates, FaceCandidate, i);
        GVA::RegionOfInterest roi = video_frame.add_region(c->x, c->y, c->width, c->height);
        roi.add_tensor("bboxregression").set_double("score", c->score);
    }

    g_array_free(candidates, TRUE);

    return TRUE;
}

gboolean process_onet_output(GstGvaBBoxRegression *bboxregression, GstBuffer *buffer) {
    assert(bboxregression && buffer && "Expected valid pointers GstGvaBBoxRegression and GstBuffer");

    GVA::VideoFrame video_frame(buffer, bboxregression->info);

    // FIXME: use std::vector
    GArray *candidates = g_array_new(FALSE, TRUE, sizeof(FaceCandidate));

    for (const GVA::RegionOfInterest &roi : video_frame.regions()) {
        FaceCandidate candidate = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<float> conv_blob, prob_blob, fld_blob;
        auto rect = roi.rect();

        for (const GVA::Tensor &tensor : roi.tensors()) {
            if (tensor.name() == "nms" and tensor.has_field("score")) {
                candidate.x = rect.x;
                candidate.y = rect.y;
                candidate.width = rect.w;
                candidate.height = rect.h;
                candidate.score = tensor.get_double("score");
            } else if (tensor.layer_name() == ONET_OUT_CONV_NAME)
                conv_blob = tensor.data<float>();
            else if (tensor.layer_name() == ONET_OUT_PROB_NAME)
                prob_blob = tensor.data<float>();
            else if (tensor.layer_name() == ONET_OUT_FLD_NAME)
                fld_blob = tensor.data<float>();
        }

        if (prob_blob.size() < SCORE_MAP_INDEX || conv_blob.empty() || fld_blob.empty()) {
            GVA_ERROR("Invalid tensor blob");
            return FALSE;
        }

        double score = prob_blob[SCORE_MAP_INDEX];
        if (score > ONET_THRESHOLD) {
            float left = candidate.x + conv_blob[0] * candidate.width;
            float top = candidate.y + conv_blob[1] * candidate.height;
            float right = candidate.x + candidate.width - 1.f + conv_blob[2] * candidate.width;
            float bottom = candidate.y + candidate.height - 1.f + conv_blob[3] * candidate.height;

            FaceCandidate candi = {TRUE, 0, 0, 0, 0, 0, 0., 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            candi.x = safe_convert<uint32_t>(left);
            candi.y = safe_convert<uint32_t>(top);
            candi.width = safe_convert<uint32_t>(right - left + 1);
            candi.height = safe_convert<uint32_t>(bottom - top + 1);
            candi.score = score;
            candi.left_eye_x = safe_convert<int32_t>(fld_blob[0] * candidate.width + candidate.x + 1);
            candi.right_eye_x = safe_convert<int32_t>(fld_blob[1] * candidate.width + candidate.x + 1);
            candi.nose_x = safe_convert<int32_t>(fld_blob[2] * candidate.width + candidate.x + 1);
            candi.mouth_left_x = safe_convert<int32_t>(fld_blob[3] * candidate.width + candidate.x + 1);
            candi.mouth_right_x = safe_convert<int32_t>(fld_blob[4] * candidate.width + candidate.x + 1);
            candi.left_eye_y = safe_convert<int32_t>(fld_blob[5] * candidate.height + candidate.y + 1);
            candi.right_eye_y = safe_convert<int32_t>(fld_blob[6] * candidate.height + candidate.y + 1);
            candi.nose_y = safe_convert<int32_t>(fld_blob[7] * candidate.height + candidate.y + 1);
            candi.mouth_left_y = safe_convert<int32_t>(fld_blob[8] * candidate.height + candidate.y + 1);
            candi.mouth_right_y = safe_convert<int32_t>(fld_blob[9] * candidate.height + candidate.y + 1);

            g_array_append_val(candidates, candi);
        }
    }
    gst_buffer_foreach_meta(buffer, foreach_meta_remove_one,
                            GSIZE_TO_POINTER(GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE));

    for (size_t i = 0; i < candidates->len; i++) {
        FaceCandidate *c = &g_array_index(candidates, FaceCandidate, i);

        GstBuffer **writable_buffer = &buffer;
        gva_buffer_check_and_make_writable(writable_buffer, PRETTY_FUNCTION_NAME);

        GstStructure *structure = gst_structure_new(
            "bboxregression", "score", G_TYPE_DOUBLE, c->score, "left_eye_x", G_TYPE_INT, c->left_eye_x, "right_eye_x",
            G_TYPE_INT, c->right_eye_x, "nose_x", G_TYPE_INT, c->nose_x, "mouth_left_x", G_TYPE_INT, c->mouth_left_x,
            "mouth_right_x", G_TYPE_INT, c->mouth_right_x, "left_eye_y", G_TYPE_INT, c->left_eye_y, "right_eye_y",
            G_TYPE_INT, c->right_eye_y, "nose_y", G_TYPE_INT, c->nose_y, "mouth_left_y", G_TYPE_INT, c->mouth_left_y,
            "mouth_right_y", G_TYPE_INT, c->mouth_right_y, NULL);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(*writable_buffer);

        if (not relation_meta)
            throw std::runtime_error("Failed to add GstAnalyticsRelationMeta to buffer");

        GstAnalyticsODMtd od_mtd;
        if (!gst_analytics_relation_meta_add_od_mtd(relation_meta, 0, c->x, c->y, c->width, c->height, 0, &od_mtd)) {
            throw std::runtime_error("Failed to add roi data to meta");
        }

        GstAnalyticsMtd tensor_mtd;
        GVA::Tensor gva_tensor(structure);
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
            gst_buffer_add_video_region_of_interest_meta(*writable_buffer, 0, c->x, c->y, c->width, c->height);
        meta->id = od_mtd.id;
        gst_video_region_of_interest_meta_add_param(meta, structure);
    }

    g_array_free(candidates, TRUE);

    return TRUE;
}

} // namespace

gboolean bbox_regression(GstGvaBBoxRegression *bboxregression, GstBuffer *buffer) {
    if (!bboxregression || !buffer) {
        GVA_ERROR("Invalid arguments: GvaBBoxRegression or GstBuffer are null");
        return FALSE;
    }

    try {
        switch (bboxregression->mode) {
        case MODE_PNET:
            return process_pnet_output(bboxregression, buffer);
        case MODE_RNET:
            return process_rnet_output(bboxregression, buffer);
        case MODE_ONET:
            return process_onet_output(bboxregression, buffer);
        default:
            throw std::invalid_argument("Unknown bboxregression mode");
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(bboxregression, STREAM, FAILED, ("bbox_regression failed"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return FALSE;
    }
}
