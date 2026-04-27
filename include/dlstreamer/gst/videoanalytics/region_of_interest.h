/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file region_of_interest.h
 * @brief This file contains GVA::RegionOfInterest class to control region of interest for particular GVA::VideoFrame
 * with GVA::Tensor instances added
 */

#pragma once

#include "../metadata/gstanalyticskeypointmtd.h"
#include "tensor.h"

#include <cstdint>
#include <gst/analytics/analytics.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace GVA {

/**
 * @brief Template structure for rectangle containing x, y, w, h fields
 */
template <typename T>
struct Rect {
    T x, y, w, h;
};

/**
 * @brief This class represents region of interest - object describing detection result (bounding box) and containing
 * multiple Tensor objects (inference results) attached by multiple models. For example, it can be region of interest
 * with detected face and recognized age and sex of a person. It can be produced by a pipeline with gvadetect with
 * detection model and two gvaclassify elements with two classification models. Such RegionOfInterest will have bounding
 * box coordinates filled and will have 3 Tensor objects attached - 1 Tensor object with detection result and 2 Tensor
 * objects with classification results coming from 2 classifications
 */
class RegionOfInterest {
  public:
    /**
     * @brief Get bounding box of the RegionOfInterest as pixel coordinates in original image
     * @return Bounding box coordinates of the RegionOfInterest
     */
    Rect<uint32_t> rect() const {
        gint x;
        gint y;
        gint w;
        gint h;
        gfloat r;

        if (!gst_analytics_od_mtd_get_oriented_location(const_cast<GstAnalyticsODMtd *>(&_od_meta), &x, &y, &w, &h, &r,
                                                        nullptr)) {
            throw std::runtime_error("Error when trying to read the location of the RegionOfInterest");
        }
        return {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }

    /**
     * @brief Get bounding box of the RegionOfInterest as normalized coordinates in the range [0, 1]
     * @return Bounding box coordinates of the RegionOfInterest
     */
    Rect<double> normalized_rect() {
        Tensor det = detection();
        return {det.get_double("x_min"), det.get_double("y_min"), det.get_double("x_max") - det.get_double("x_min"),
                det.get_double("y_max") - det.get_double("y_min")};
    }

    /**
     * @brief Get RegionOfInterest bounding box rotation
     * @return Bounding box rotation of the RegionOfInterest
     */
    float rotation() const {
        gint x;
        gint y;
        gint w;
        gint h;
        gfloat rotation;

        if (!gst_analytics_od_mtd_get_oriented_location(&_od_meta, &x, &y, &w, &h, &rotation, nullptr)) {
            throw std::runtime_error("Error when trying to read the rotation of the RegionOfInterest");
        }
        return rotation;
    }

    /**
     * @brief Get RegionOfInterest label
     * @return RegionOfInterest label
     */
    std::string label() const {
        GQuark label = gst_analytics_od_mtd_get_obj_type(const_cast<GstAnalyticsODMtd *>(&_od_meta));
        return label ? g_quark_to_string(label) : "";
    }

    /**
     * @brief Get RegionOfInterest detection confidence (set by gvadetect)
     * @return detection confidence from analytics metadata
     * @throws std::runtime_error if confidence cannot be read from metadata
     */
    double confidence() const {
        gfloat conf;
        if (!gst_analytics_od_mtd_get_confidence_lvl(const_cast<GstAnalyticsODMtd *>(&_od_meta), &conf)) {
            throw std::runtime_error("Error when trying to read the confidence of the RegionOfInterest");
        }
        return conf;
    }

    /**
     * @brief Get unique id of the RegionOfInterest. The id typically created by gvatrack element.
     * @return Unique id, or zero value if not found
     */
    int32_t object_id() const {
        GstAnalyticsTrackingMtd trk_mtd;
        if (gst_analytics_relation_meta_get_direct_related(_od_meta.meta, _od_meta.id, GST_ANALYTICS_REL_TYPE_ANY,
                                                           gst_analytics_tracking_mtd_get_mtd_type(), nullptr,
                                                           &trk_mtd)) {
            guint64 id;
            GstClockTime tracking_first_seen, tracking_last_seen;
            gboolean tracking_lost;
            if (!gst_analytics_tracking_mtd_get_info(&trk_mtd, &id, &tracking_first_seen, &tracking_last_seen,
                                                     &tracking_lost)) {
                throw std::runtime_error("Failed to get tracking mtd info");
            }

            return id;
        }
        return 0;
    }

    /**
     * @brief Get all Tensor instances added to this RegionOfInterest
     * @return vector of Tensor instances added to this RegionOfInterest
     */
    std::vector<Tensor> tensors() const {
        return this->_tensors;
    }

    /**
     * @brief Add new tensor (inference result) to this RegionOfInterest
     * @param tensor Tensor object to add to this RegionOfInterest
     */
    void add_tensor(const Tensor &tensor) {
        GstStructure *s = tensor.gst_structure();
        if (!s) {
            throw std::invalid_argument("GVA::RegionOfInterest::add_tensor: tensor structure is nullptr");
        }
        gst_video_region_of_interest_meta_add_param(_gst_meta, s);

        GstAnalyticsMtd tensor_mtd;
        if (tensor.convert_to_meta(&tensor_mtd, &_od_meta, _od_meta.meta)) {
            if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_CONTAIN, _od_meta.id,
                                                          tensor_mtd.id)) {
                throw std::runtime_error(
                    "Failed to set relation between object detection metadata and tensor metadata");
            }
            if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                          tensor_mtd.id, _od_meta.id)) {
                throw std::runtime_error(
                    "Failed to set relation between tensor metadata and object detection metadata");
            }
        }

        _tensors.emplace_back(tensor);
        if (_tensors.back().is_detection())
            _detection = &_tensors.back();
    }

    /**
     * @brief Returns detection Tensor, last added to this RegionOfInterest. As any other Tensor, returned detection
     * Tensor can contain arbitrary information. If you use RegionOfInterest based on GstVideoRegionOfInterestMeta
     * attached by gvadetect by default, then this Tensor will contain "label_id", "confidence", "x_min", "x_max",
     * "y_min", "y_max" fields.
     * If RegionOfInterest doesn't have detection Tensor, it will be created in-place.
     * @return detection Tensor, empty if there were no detection Tensor objects added to this RegionOfInterest when
     * this method was called
     */
    Tensor detection() {
        if (!_detection) {
            GstStructure *gst_structure = gst_structure_new_empty("detection");
            Tensor detection_tensor(gst_structure);
            add_tensor(detection_tensor);
        }
        return _detection ? *_detection : nullptr;
    }

    /**
     * @brief Get label_id from detection Tensor, last added to this RegionOfInterest
     * @return last added detection Tensor label_id if exists, otherwise 0
     */
    int label_id() const {
        GQuark label = gst_analytics_od_mtd_get_obj_type(const_cast<GstAnalyticsODMtd *>(&_od_meta));
        if (label) {
            GstAnalyticsClsMtd cls_descriptor_mtd;
            if (!gst_analytics_relation_meta_get_direct_related(
                    _od_meta.meta, _od_meta.id, GST_ANALYTICS_REL_TYPE_RELATE_TO, gst_analytics_cls_mtd_get_mtd_type(),
                    nullptr, &cls_descriptor_mtd)) {
                return 0;
            }

            gint label_id = gst_analytics_cls_mtd_get_index_by_quark(&cls_descriptor_mtd, label);
            if (label_id < 0) {
                throw std::runtime_error("Error when trying to read the label id of the RegionOfInterest");
            }
            return label_id;
        } else {
            return 0;
        }
    }

    /**
     * @brief Construct RegionOfInterest from analytics metadata and video metadata
     * @param od_meta Object detection analytics metadata
     * @param meta Video region of interest metadata containing additional parameters
     */
    RegionOfInterest(GstAnalyticsODMtd od_meta, GstVideoRegionOfInterestMeta *meta)
        : _gst_meta(meta), _detection(nullptr), _od_meta(od_meta) {

        if (not _gst_meta)
            throw std::invalid_argument("GVA::RegionOfInterest: meta is nullptr");

        _tensors.reserve(g_list_length(meta->params));

        for (GList *l = meta->params; l; l = g_list_next(l)) {
            GstStructure *s = GST_STRUCTURE(l->data);
            const char *type = gst_structure_get_string(s, "type");
            if (not gst_structure_has_name(s, "object_id") &&
                (type == nullptr || strcmp(type, TENSOR_TYPE_KEYPOINTS) != 0) &&
                (type == nullptr || strcmp(type, TENSOR_TYPE_CLASSIFICATION) != 0)) {
                _tensors.emplace_back(s);
                if (_tensors.back().is_detection())
                    _detection = &_tensors.back();
            }
        }

        // append tensors converted from metadata
        gpointer state = NULL;
        GstAnalyticsMtd handle;
        while (gst_analytics_relation_meta_get_direct_related(od_meta.meta, od_meta.id, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                              GST_ANALYTICS_MTD_TYPE_ANY, &state, &handle)) {
            GstStructure *s = GVA::Tensor::convert_to_tensor(handle);
            if (s != nullptr) {
                auto shared_s = std::shared_ptr<GstStructure>(s, gst_structure_free);
                _tensors.emplace_back(s);
                _converted_structures.push_back(shared_s);
            }
        }
    }

    /**
     * @brief Access RegionOfInterest ID
     * Use this method to get RegionOfInterest ID. ID is generated with "GVA::VideoFrame::add_region()" call.
     * Region ID can be a positive or negative integer, but never zero.
     * @return ID field value
     */
    int region_id() {
        return _od_meta.id;
    }

    /**
     * @brief Retrieves the parent object detection ID for this region of interest.
     * @return The ID of the parent object detection metadata if found, -1 otherwise.
     */
    int parent_id() {
        GstAnalyticsODMtd rlt_mtd;
        if (gst_analytics_relation_meta_get_direct_related(_od_meta.meta, _od_meta.id,
                                                           GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                           gst_analytics_od_mtd_get_mtd_type(), nullptr, &rlt_mtd)) {
            return rlt_mtd.id;
        } else {
            return -1;
        }
    }

    /**
     * @brief Set object ID
     * @param id ID to set
     */
    void set_object_id(int32_t id) {
        if (_gst_meta) {
            GstStructure *object_id = gst_video_region_of_interest_meta_get_param(_gst_meta, "object_id");
            if (object_id) {
                gst_structure_set(object_id, "id", G_TYPE_INT, id, NULL);
            } else {
                object_id = gst_structure_new("object_id", "id", G_TYPE_INT, id, NULL);
                gst_video_region_of_interest_meta_add_param(_gst_meta, object_id);
            }
        }

        gpointer state = nullptr;
        GstAnalyticsTrackingMtd trk_mtd;
        while (gst_analytics_relation_meta_get_direct_related(_od_meta.meta, _od_meta.id, GST_ANALYTICS_REL_TYPE_ANY,
                                                              gst_analytics_tracking_mtd_get_mtd_type(), &state,
                                                              &trk_mtd)) {
            if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_NONE, _od_meta.id,
                                                          trk_mtd.id)) {
                throw std::runtime_error("Failed to remove relation between od meta and tracking meta");
            }
        }

        if (!gst_analytics_relation_meta_add_tracking_mtd(_od_meta.meta, id, 0, &trk_mtd)) {
            throw std::runtime_error("Failed to add tracking metadata");
        }

        if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, _od_meta.id,
                                                      trk_mtd.id)) {
            throw std::runtime_error("Failed to set relation between od meta and tracking meta");
        }
    }

    /**
     * @brief Get list of parameters attached to this RegionOfInterest
     * @return GList pointer to parameters
     */
    GList *get_params() const {
        return _gst_meta->params;
    }

    /**
     * @brief Get parameter by name from this RegionOfInterest
     * @param name Name of the parameter to retrieve
     * @return GstStructure pointer to the parameter, or nullptr if not found
     */
    GstStructure *get_param(const char *name) const {
        return gst_video_region_of_interest_meta_get_param(_gst_meta, name);
    }

    /**
     * @brief Add parameter structure to this RegionOfInterest
     * @param s GstStructure to add as parameter
     */
    void add_param(GstStructure *s) {
        gst_video_region_of_interest_meta_add_param(_gst_meta, s);

        GVA::Tensor tensor(s);
        GstAnalyticsMtd tensor_mtd;
        if (tensor.convert_to_meta(&tensor_mtd, &_od_meta, _od_meta.meta)) {
            if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_CONTAIN, _od_meta.id,
                                                          tensor_mtd.id)) {
                throw std::runtime_error(
                    "Failed to set relation between object detection metadata and tensor metadata");
            }
            if (!gst_analytics_relation_meta_set_relation(_od_meta.meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                          tensor_mtd.id, _od_meta.id)) {
                throw std::runtime_error(
                    "Failed to set relation between tensor metadata and object detection metadata");
            }
        }
    }

    /**
     * @brief Internal function, don't use or use with caution.
     * @return pointer to underlying GstVideoRegionOfInterestMeta
     */
    GstVideoRegionOfInterestMeta *_meta() const {
        assert(_gst_meta != nullptr);
        return _gst_meta;
    }

  protected:
    /**
     * @brief GstVideoRegionOfInterestMeta containing fields filled with detection result (produced by gvadetect
     * element in Gstreamer pipeline) and all the additional tensors, describing detection and other inference
     * results (produced by gvainference, gvadetect, gvaclassify in Gstreamer pipeline)
     */
    GstVideoRegionOfInterestMeta *_gst_meta;
    /**
     * @brief vector of Tensor objects added to this RegionOfInterest (describing detection & inference results),
     * obtained from GstVideoRegionOfInterestMeta
     */
    std::vector<Tensor> _tensors;

    /**
     * @brief vector of GstStructure shared pointers that were allocated by convert_to_tensor.
     */
    std::vector<std::shared_ptr<GstStructure>> _converted_structures;

    /**
     * @brief last added detection Tensor instance, defined as Tensor with name set to "detection"
     */
    Tensor *_detection;

    /**
     * @brief handle containing data required to use gst_analytics_od_mtd APIs, to retrieve analytics data related
     * to that region of interest.
     */
    GstAnalyticsODMtd _od_meta;
};

} // namespace GVA
