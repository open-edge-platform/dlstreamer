/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "config.h"

#include "mapped_mat.h"
#include "tracker.h"

#include "gva_utils.h"
#include "utils.h"

#include <functional>
#include <inference_backend/logger.h>

namespace dls = dlstreamer;

namespace {

constexpr int DEFAULT_MAX_NUM_OBJECTS = -1;
constexpr bool DEFAULT_TRACKING_PER_CLASS = true;
constexpr int NO_ASSOCIATION = -1;

bool CaseInsCompare(const std::string &s1, const std::string &s2) {
    return (s1.size() == s2.size()) &&
           std::equal(s1.begin(), s1.end(), s2.begin(), [](char a, char b) { return toupper(a) == toupper(b); });
}

vas::BackendType backendType(const std::string &backend_type) {
    if (CaseInsCompare(backend_type, "CPU")) {
        return vas::BackendType::CPU;
    } else if (CaseInsCompare(backend_type, "VPU")) {
        return vas::BackendType::VPU;
    } else if (CaseInsCompare(backend_type, "GPU")) {
        return vas::BackendType::GPU;
    } else if (CaseInsCompare(backend_type, "FPGA")) {
        return vas::BackendType::FPGA;
    } else if (CaseInsCompare(backend_type, "HDDL")) {
        return vas::BackendType::HDDL;
    } else {
        throw std::invalid_argument("Unknown tracking device " + backend_type);
    }
}

std::vector<vas::ot::DetectedObject> convertRoisToDetectedObjects(std::vector<GVA::RegionOfInterest> &regions,
                                                                  std::unordered_map<int, std::string> &labels) {
    std::vector<vas::ot::DetectedObject> detected_objects;
    for (GVA::RegionOfInterest &roi : regions) {
        int label_id = roi.detection().get_int("label_id", std::numeric_limits<int>::max());
        if (labels.find(label_id) == labels.end())
            labels[label_id] = roi.label();
        auto rect = roi.rect();
        cv::Rect obj_rect(rect.x, rect.y, rect.w, rect.h);
        detected_objects.emplace_back(obj_rect, label_id);
    }
    return detected_objects;
}

void append(GVA::VideoFrame &video_frame, const vas::ot::Object &tracked_object, const std::string &label,
            double confidence) {
    auto roi = video_frame.add_region(tracked_object.rect.x, tracked_object.rect.y, tracked_object.rect.width,
                                      tracked_object.rect.height, label, confidence);
    roi.detection().set_int("label_id", tracked_object.class_label);
    roi.set_object_id(tracked_object.tracking_id);
}

} // namespace

namespace VasWrapper {

class TrackerBackend {
  public:
    TrackerBackend(dls::MemoryMapperPtr buffer_mapper) : _buf_mapper(std::move(buffer_mapper)) {
    }

    virtual std::unique_ptr<vas::ot::ObjectTracker::Builder> getBuilder() {
        return std::make_unique<vas::ot::ObjectTracker::Builder>();
    }

    virtual void init(std::unique_ptr<vas::ot::ObjectTracker::Builder> builder, vas::ot::TrackingType type) {
        if (builder->backend_type == vas::BackendType::GPU)
            throw std::invalid_argument("Invalid backend type provided");
        _object_tracker = builder->Build(type);
        _imageless_algo =
            type == vas::ot::TrackingType::ZERO_TERM_IMAGELESS || type == vas::ot::TrackingType::SHORT_TERM_IMAGELESS;
    }

    virtual std::vector<vas::ot::Object> track(dls::FramePtr buffer,
                                               const std::vector<vas::ot::DetectedObject> &detected_objects) {
        if (_imageless_algo) {
            // For imageless algorithms image data is not important
            // So in this case dummy (empty) cv::Mat is passed to avoid redundant buffer map/unmap operations
            if (_dummy_mat.empty())
                prepareDummyCvMat(*buffer);
            return _object_tracker->Track(_dummy_mat, detected_objects);
        }

        dls::FramePtr sys_buf = _buf_mapper->map(buffer, dls::AccessMode::Read);
        MappedMat cv_mat(sys_buf);
        return _object_tracker->Track(cv_mat.mat(), detected_objects);
    }

    void prepareDummyCvMat(dls::Frame &buffer) {
        dlstreamer::ImageInfo image_info(buffer.tensor(0)->info());
        cv::Size cv_size(image_info.width(), image_info.height());
        dls::ImageFormat format = static_cast<dls::ImageFormat>(buffer.format());
        if (format == dls::ImageFormat::NV12 || format == dls::ImageFormat::I420)
            cv_size.height = cv_size.height * 3 / 2;
        _dummy_mat = cv::Mat(cv_size, CV_8UC3);
    }

  protected:
    std::unique_ptr<vas::ot::ObjectTracker> _object_tracker;
    dls::MemoryMapperPtr _buf_mapper;
    bool _imageless_algo = false;
    cv::Mat _dummy_mat;
};

Tracker::Tracker(const std::string &device, vas::ot::TrackingType tracking_type, vas::ColorFormat in_color,
                 const std::string &config_kv, dls::MemoryMapperPtr mapper, dls::ContextPtr /*context*/) {

    // Parse device string. Examples: VPU.1, CPU, GPU, etc.
    std::vector<std::string> full_device = Utils::splitString(device, '.');
    auto backend_type = backendType(full_device[0]);

    _impl = std::make_unique<TrackerBackend>(mapper);

    std::unique_ptr<vas::ot::ObjectTracker::Builder> builder = _impl->getBuilder();
    builder->input_image_format = in_color;

    // Initialize with defaults
    builder->max_num_objects = DEFAULT_MAX_NUM_OBJECTS;
    builder->tracking_per_class = DEFAULT_TRACKING_PER_CLASS;

    // Parse tracking configuration
    auto cfg = Utils::stringToMap(config_kv);
    auto iter = cfg.end();
    try {
        iter = cfg.find("max_num_objects");
        if (iter != cfg.end()) {
            builder->max_num_objects = std::stoi(iter->second);
            cfg.erase(iter);
        }

        iter = cfg.find("tracking_per_class");
        if (iter != cfg.end()) {
            builder->tracking_per_class = Utils::strToBool(iter->second);
            cfg.erase(iter);
        }
    } catch (...) {
        if (iter == cfg.end())
            std::throw_with_nested(std::runtime_error("Error occured while parsing key/value parameters"));
        std::throw_with_nested(std::runtime_error("Invalid value provided for parameter: " + iter->first));
    }

    GVA_INFO("Tracker configuration:");
    GVA_INFO("-- tracking_type: %d", static_cast<int>(tracking_type));
    GVA_INFO("-- input_image_format: %d", static_cast<int>(builder->input_image_format));
    GVA_INFO("-- max_num_objects: %#x", builder->max_num_objects);
    GVA_INFO("-- tracking_per_class: %u", builder->tracking_per_class);

    builder->backend_type = backend_type;
    GVA_INFO("-- backend_type: %d", static_cast<int>(builder->backend_type));

    if ((builder->backend_type == vas::BackendType::VPU || builder->backend_type == vas::BackendType::GPU) &&
        full_device.size() > 1) {
        cfg["device_id"] = full_device.at(1);
        GVA_INFO("-- device_id: %s", cfg["device_id"].c_str());
    }

    builder->platform_config = std::move(cfg);
    _impl->init(std::move(builder), tracking_type);
}

void Tracker::track(dls::FramePtr buffer, GVA::VideoFrame &frame_meta) {
    if (!buffer)
        throw std::invalid_argument("buffer is nullptr");

    std::vector<vas::ot::Object> tracked_objects;
    auto regions = frame_meta.regions();
    try {
        std::vector<vas::ot::DetectedObject> detected_objects = convertRoisToDetectedObjects(regions, labels);
        tracked_objects = _impl->track(std::move(buffer), detected_objects);
    } catch (const std::exception &e) {
        GST_ERROR("Exception within tracker occurred: %s", Utils::createNestedErrorMsg(e).c_str());
        throw std::runtime_error("Track: error while tracking objects");
    }

    // Tracker itself does not produce confidence, so for objects without associated detection on this frame
    // the confidence of the last detection associated with the same track is carried forward.
    std::unordered_map<uint64_t, double> alive_confidences;
    for (const auto &tracked_object : tracked_objects) {
        const bool associated = tracked_object.association_idx != NO_ASSOCIATION;
        double confidence = 0.0;
        if (associated) {
            confidence = regions[tracked_object.association_idx].confidence();
        } else {
            auto it = confidences.find(tracked_object.tracking_id);
            if (it != confidences.end())
                confidence = it->second;
        }
        alive_confidences[tracked_object.tracking_id] = confidence;

        if (tracked_object.status == vas::ot::TrackingStatus::LOST)
            continue;
        if (associated) {
            regions[tracked_object.association_idx].set_object_id(tracked_object.tracking_id);
        } else {
            auto it = labels.find(tracked_object.class_label);
            std::string label = it != labels.end() ? it->second : std::string();
            append(frame_meta, tracked_object, label, confidence);
        }
    }
    // Drop entries of tracks that are no longer reported by the tracker
    confidences = std::move(alive_confidences);
}

} // namespace VasWrapper
