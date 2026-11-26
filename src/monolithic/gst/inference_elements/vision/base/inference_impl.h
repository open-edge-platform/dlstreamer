/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#pragma once

#include "classification_history.h"
#include "fair_mutex.h"
#include "gstgvaclassify.h"
#include "gva_base_inference.h"
#include "input_model_preproc.h"

#include "inference_backend/image_inference.h"

#include <gst/video/video.h>

#include <gst/analytics/analytics.h>
#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class InferenceImpl {
  public:
    struct Model {
        std::string name;
        std::shared_ptr<InferenceBackend::ImageInference> inference;
        std::vector<ModelInputProcessorInfo::Ptr> input_processor_info;
        std::map<std::string, GstStructure *> output_processor_info;
        std::string labels;
    };

    InferenceImpl(GvaBaseInference *gva_base_inference);

    static dlstreamer::ContextPtr GetDisplay(GvaBaseInference *gva_base_inference);
    static void SetDisplay(GvaBaseInference *gva_base_inference, const dlstreamer::ContextPtr &display);

    GstFlowReturn TransformFrameIp(GvaBaseInference *element, GstBuffer *buffer);
    void FlushOutputs();
    void FlushInference();
    // Deliver this element's completed frames that PushOutput() left behind because the output was blocked. Runs on
    // the element's streaming thread and blocks in gst_pad_push() with no instance lock held, so only this stream is
    // throttled. With wait_all (EOS/FLUSH_STOP) it also waits for in-flight frames so nothing is left behind the event.
    void DrainOutput(GvaBaseInference *element, bool wait_all = false);
    void SetElementCount(size_t count) {
        element_count = count;
    }
    bool IsShared() const {
        return element_count > 1;
    }
    const Model &GetModel() const;

    void UpdateObjectClasses(const gchar *obj_classes_str);
    bool FilterObjectClass(GstVideoRegionOfInterestMeta *roi) const;
    bool FilterObjectClass(GstAnalyticsODMtd roi) const;
    bool FilterObjectClass(const std::string &object_class) const;

    InferenceBackend::MemoryType GetInferenceMemoryType() const {
        return memory_type;
    }

    ~InferenceImpl();

    static bool IsRoiSizeValid(const GstVideoRegionOfInterestMeta *roi_meta);
    static bool IsRoiSizeValid(const GstAnalyticsODMtd roi_meta);

  private:
    InferenceBackend::MemoryType memory_type;
    struct InferenceResult : public InferenceBackend::ImageInference::IFrameBase {
        void SetImage(InferenceBackend::ImagePtr image_) override {
            image = image_;
        }
        InferenceBackend::ImagePtr GetImage() const override {
            return image;
        }
        std::shared_ptr<InferenceFrame> inference_frame;
        Model *model;
        std::shared_ptr<InferenceBackend::Image> image;
    };

    enum InferenceStatus {
        INFERENCE_EXECUTED = 1,
        INFERENCE_SKIPPED_PER_PROPERTY = 2, // frame skipped due to inference-interval set to value greater than 1
        INFERENCE_SKIPPED_NO_BLOCK = 3,     // frame skipped due to no-block policy
        INFERENCE_SKIPPED_ROI = 4           // roi skipped because is_roi_inference_needed() returned false
    };

    std::vector<std::string> object_classes;

    // Serializes frame submission of all elements sharing this instance, in arrival order: when the device is
    // saturated every stream gets its turn, instead of whichever thread wins the race for a plain mutex.
    mutable FairMutex _mutex;
    // number of elements (streams) currently attached to this instance, maintained by the instance pool
    std::atomic<size_t> element_count{1};
    Model model;
    std::shared_ptr<InferenceBackend::Allocator> allocator;

    struct OutputFrame {
        GstBuffer *buffer;
        uint64_t inference_count;
        GvaBaseInference *filter;
        std::vector<std::shared_ptr<InferenceFrame>> inference_rois;
        // being pushed downstream with output_frames_mutex released; owned by the pushing thread until erased
        bool pushing = false;
    };

    std::list<OutputFrame> output_frames;
    std::mutex output_frames_mutex;
    // signalled when a frame completes inference or leaves output_frames
    std::condition_variable output_frames_cv;

#ifndef _WIN32
    void SetAffinityMask(const cpu_set_t &mask);
#else
    void SetAffinityMask(const WinCorePinningMask &mask);
#endif
    void PushOutput();
    bool CheckSrcPadBlocked(GstObject *src);
    void RecordClassificationHistory(const OutputFrame &frame);
    std::list<OutputFrame>::iterator DeliverFrame(std::unique_lock<std::mutex> &lock,
                                                  std::list<OutputFrame>::iterator frame);
    void PushBufferToSrcPad(OutputFrame &output_frame);
    void PushFramesIfInferenceFailed(std::vector<std::shared_ptr<InferenceBackend::ImageInference::IFrameBase>> frames);
    void InferenceCompletionCallback(std::map<std::string, InferenceBackend::OutputBlob::Ptr> blobs,
                                     std::vector<std::shared_ptr<InferenceBackend::ImageInference::IFrameBase>> frames);
    void UpdateOutputFrames(std::shared_ptr<InferenceFrame> &inference_roi);
    Model CreateModel(GvaBaseInference *gva_base_inference, const std::string &model_file,
                      const std::string &model_proc_path, const std::string &labels_str,
                      const std::string &custom_preproc_lib);
    void UpdateModelReshapeInfo(GvaBaseInference *gva_base_inference);

    GstFlowReturn SubmitImages(GvaBaseInference *gva_base_inference,
                               const std::vector<GstVideoRegionOfInterestMeta> &metas, GstBuffer *buffer);
    std::shared_ptr<InferenceResult> MakeInferenceResult(GvaBaseInference *gva_base_inference, Model &model,
                                                         GstVideoRegionOfInterestMeta *meta,
                                                         std::shared_ptr<InferenceBackend::Image> &image,
                                                         GstBuffer *buffer);
};
