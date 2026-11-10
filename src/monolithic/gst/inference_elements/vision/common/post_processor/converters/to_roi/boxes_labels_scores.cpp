/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * Context - where this file sits in gvadetect:
 *
 *   frame -> pre-processing -> OpenVINO inference -> output tensors ("blobs", by output name)
 *         -> PostProcessorImpl::process() -> ConverterFacade::convert():
 *              1. BlobToMetaConverter::convert()  - decode blobs into "detection" tensors   <- this file
 *              2. CoordinatesRestorer::restore()  - map coordinates back to the original frame
 *              3. MetaAttacher::attach()          - attach them to the buffer as ROI metadata
 *
 *   - Converters are created once per element by BlobToMetaConverter::create(), which maps the declared
 *     converter name (model_info model_type, or the deprecated model-proc "converter") to a class, and
 *     BlobToROIConverter::create(), the factory for all detection converters.
 *   - Initializer carries what the converter needs from the model: name, input size and batch, output
 *     names/shapes (outputs_info), labels, and model_proc_output_info - a GstStructure with converter
 *     settings (confidence_threshold, iou_threshold, nms_execute, roi_scale), filled from model_info.
 *   - Base classes: BlobToMetaConverter holds the model info and labels; BlobToROIConverter holds the
 *     thresholds, runs NMS and drops invalid boxes in storeObjects(), and turns DetectedObjects into
 *     "detection" tensors. A converter only has to decode its tensors into DetectedObjects.
 *   - Output: one list of detections per image in the batch, with coordinates normalized to [0,1] of the
 *     model input; mapping them back to the frame (crop/letterbox) is step 2 above.
 *
 * "boxes_labels_scores" converter: turns detection models whose results are split across separate
 * output tensors into ROI (detection) metadata for gvadetect.
 *
 * Developed mostly for Intel's Geti and Open Model Zoo detection models.
 *
 * Supported outputs (matched by tensor name, extra outputs are ignored):
 *   - "boxes"  [N,4|5] or [B,N,4|5]: x1,y1,x2,y2[,confidence] in model input pixels (Geti/OTX ATSS, SSD,
 *              Open Model Zoo *-detection-02xx models), or
 *   - "bboxes" [N,4|5] or [B,N,4|5]: the same, normalized to [0,1] (end-to-end models such as D-FINE);
 *   - "labels" [N], [1,N] or [B,N] (optional): class id per box, FP32/I32/U32/I64/U64; default 0;
 *   - "scores" [N], [1,N] or [B,N] (optional): confidence per box; takes precedence over the 5th box
 *              value, which in turn defaults to 1.0.
 *
 * How it is selected:
 *   - model_info model_type "ssd" (Geti / Model API), or converter "boxes_labels_scores";
 *   - legacy model-proc names "boxes", "boxes_labels", "tensor_to_bbox_atss" (with a deprecation warning);
 *   - automatically, when no converter is declared and the outputs match (see findLayout()).
 *
 * Processing per box: drop NaN/Inf values, apply confidence_threshold, normalize pixel coordinates by the
 * model input size, clamp to the image, apply roi_scale and map the label id to its name. NMS follows
 * model_info nms_execute; if absent, it runs for "boxes" (legacy behavior) and is skipped for "bboxes"
 * (Model API default). If every box in a batch is NaN/Inf, a one-time warning is posted on the element,
 * since that usually means the inference device produced invalid results.
 *
 * Replaces the former BoxesConverter, BoxesLabelsConverter, BoxesScoresConverter and their
 * BoxesLabelsScoresConverter base class.
 */

#include "boxes_labels_scores.h"

#include "gva_base_inference.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace post_processing;

namespace {

const std::string pixel_boxes_name = "boxes";
const std::string normalized_boxes_name = "bboxes";
const std::string labels_name = "labels";
const std::string scores_name = "scores";

struct BoxesShape {
    size_t images;
    size_t proposals;
    size_t box_size;
};

/// @brief Splits a box tensor shape into images x proposals x values per box.
/// @param dims Box tensor shape: [N,4|5] or [B,N,4|5].
/// @return The split shape (images = 1 for [N,4|5]), or nullopt for any other shape.
std::optional<BoxesShape> getBoxesShape(const std::vector<size_t> &dims) {
    if (dims.size() != 2 && dims.size() != 3)
        return std::nullopt;
    const size_t box_size = dims.back();
    if (box_size != 4 && box_size != 5)
        return std::nullopt;
    return BoxesShape{dims.size() == 3 ? dims[0] : 1, dims[dims.size() - 2], box_size};
}

/// @brief Checks that a labels/scores tensor holds exactly one value per box, in box order.
/// @param dims Tensor shape; [N], [1,N] and [B,N] are accepted.
/// @param boxes Shape of the box tensor it must match.
/// @return true if the shape matches.
bool hasValuePerBox(const std::vector<size_t> &dims, const BoxesShape &boxes) {
    const size_t total = std::accumulate(dims.begin(), dims.end(), size_t{1}, std::multiplies<>());
    return !dims.empty() && dims.back() == boxes.proposals && total == boxes.images * boxes.proposals;
}

/// @brief Looks up a model output by name.
/// @param blobs All model outputs of the current inference request.
/// @param name Output tensor name.
/// @return The output blob; throws std::invalid_argument if it is missing or has no data.
const InferenceBackend::OutputBlob::Ptr &getBlob(const OutputBlobs &blobs, const std::string &name) {
    const auto it = blobs.find(name);
    if (it == blobs.end() || !it->second || !it->second->GetData())
        throw std::invalid_argument("Output '" + name + "' is missing or empty.");
    return it->second;
}

/// @brief Reads one class id from a labels tensor.
/// @param blob Labels tensor (FP32, I32, U32, I64 or U64).
/// @param index Flat box index: image * proposals + proposal.
/// @return The class id, or -1 for a non-finite FP32 value; throws for unsupported precisions.
int64_t readLabel(const InferenceBackend::OutputBlob &blob, size_t index) {
    const void *data = blob.GetData();
    switch (blob.GetPrecision()) {
    case InferenceBackend::Blob::Precision::FP32: {
        const float label = static_cast<const float *>(data)[index];
        return std::isfinite(label) ? static_cast<int64_t>(label) : -1;
    }
    case InferenceBackend::Blob::Precision::I32:
        return static_cast<const int32_t *>(data)[index];
    case InferenceBackend::Blob::Precision::U32:
        return static_cast<const uint32_t *>(data)[index];
    case InferenceBackend::Blob::Precision::I64:
        return static_cast<const int64_t *>(data)[index];
    case InferenceBackend::Blob::Precision::U64:
        return static_cast<int64_t>(static_cast<const uint64_t *>(data)[index]);
    default:
        throw std::invalid_argument("Unsupported '" + labels_name + "' precision.");
    }
}

/// @brief Like findLayout(), but fails with a descriptive error instead of returning nullopt.
/// @param outputs_info Model output names and shapes.
/// @return The detected layout; throws std::invalid_argument describing the expected format.
BoxesLabelsScoresConverter::Layout requireLayout(const ModelOutputsInfo &outputs_info) {
    const auto layout = BoxesLabelsScoresConverter::findLayout(outputs_info);
    if (!layout)
        throw std::invalid_argument("Model outputs do not match the '" + BoxesLabelsScoresConverter::getName() +
                                    "' format: expected '" + pixel_boxes_name + "' or '" + normalized_boxes_name +
                                    "' [N,4|5] or [B,N,4|5], with optional per-box '" + labels_name + "' and '" +
                                    scores_name + "'.");
    return *layout;
}

/// @brief Reads the nms_execute flag (copied from model_info metadata) from the converter settings.
/// @param model_proc_output_info Converter settings; may be null.
/// @return The flag, or nullopt if the model doesn't declare it.
std::optional<bool> getNmsExecute(const GstStructure *model_proc_output_info) {
    gboolean nms_execute = FALSE;
    if (model_proc_output_info == nullptr ||
        !gst_structure_get_boolean(model_proc_output_info, "nms_execute", &nms_execute))
        return std::nullopt;
    return nms_execute == TRUE;
}

} // namespace

/// @brief Reads the layout and NMS setting from the initializer before it is moved into the base class.
/// @param initializer Model name, input/output info, labels and converter settings.
/// @param confidence_threshold Boxes with a lower confidence are dropped.
/// @param iou_threshold IoU threshold used when NMS runs.
/// Throws std::invalid_argument if the model outputs don't match a supported layout.
BoxesLabelsScoresConverter::BoxesLabelsScoresConverter(Initializer initializer, double confidence_threshold,
                                                       double iou_threshold)
    : BoxesLabelsScoresConverter(requireLayout(initializer.outputs_info),
                                 getNmsExecute(initializer.model_proc_output_info.get()), std::move(initializer),
                                 confidence_threshold, iou_threshold) {
}

/// @brief Delegated constructor that initializes the base class with the resolved NMS setting.
/// @param layout Layout detected from the model outputs.
/// @param nms_execute nms_execute from model_info; when absent, legacy "boxes" models keep NMS and "bboxes"
///        follow Model API's default (off).
/// @param initializer,confidence_threshold,iou_threshold As in the public constructor.
BoxesLabelsScoresConverter::BoxesLabelsScoresConverter(Layout layout, std::optional<bool> nms_execute,
                                                       Initializer &&initializer, double confidence_threshold,
                                                       double iou_threshold)
    : BlobToROIConverter(std::move(initializer), confidence_threshold, nms_execute.value_or(!layout.normalized),
                         iou_threshold),
      layout(std::move(layout)) {
}

/// @brief Detects the supported layout from output names and shapes.
/// @param outputs_info Model output names and shapes.
/// @return The layout, or nullopt if neither or both of "boxes"/"bboxes" exist, the box shape is unsupported,
///         or "labels"/"scores" don't hold one value per box.
std::optional<BoxesLabelsScoresConverter::Layout>
BoxesLabelsScoresConverter::findLayout(const ModelOutputsInfo &outputs_info) {
    const bool has_pixel_boxes = outputs_info.count(pixel_boxes_name) != 0;
    const bool has_normalized_boxes = outputs_info.count(normalized_boxes_name) != 0;
    if (has_pixel_boxes == has_normalized_boxes)
        return std::nullopt;

    Layout layout;
    layout.normalized = has_normalized_boxes;
    layout.boxes_name = layout.normalized ? normalized_boxes_name : pixel_boxes_name;

    const auto boxes = getBoxesShape(outputs_info.at(layout.boxes_name));
    if (!boxes)
        return std::nullopt;
    layout.box_size = boxes->box_size;

    const auto isAbsentOrPerBox = [&](const std::string &name, bool &present) {
        const auto it = outputs_info.find(name);
        present = it != outputs_info.end();
        return !present || hasValuePerBox(it->second, *boxes);
    };
    if (!isAbsentOrPerBox(labels_name, layout.has_labels) || !isAbsentOrPerBox(scores_name, layout.has_scores))
        return std::nullopt;

    return layout;
}

/// @brief Converts outputs without frame context (e.g. unit tests); warnings go to the debug log.
/// @param output_blobs Model outputs of one inference request.
/// @return Detection tensors for each image in the batch.
TensorsTable BoxesLabelsScoresConverter::convert(const OutputBlobs &output_blobs) {
    bool only_non_finite = false;
    TensorsTable result = convertBlobs(output_blobs, only_non_finite);
    if (only_non_finite)
        warnNonFinite(nullptr);
    return result;
}

/// @brief Pipeline entry point; warnings are posted on the inference element's bus.
/// @param output_blobs Model outputs of one inference request.
/// @param frames Frames of the batch; frames[0] provides the element to post warnings on.
/// @return Detection tensors for each image in the batch.
TensorsTable BoxesLabelsScoresConverter::convert(const OutputBlobs &output_blobs, FramesWrapper &frames) {
    bool only_non_finite = false;
    TensorsTable result = convertBlobs(output_blobs, only_non_finite);
    if (only_non_finite)
        warnNonFinite(!frames.empty() && frames[0].gva_base_inference ? GST_ELEMENT(frames[0].gva_base_inference)
                                                                      : nullptr);
    return result;
}

/// @brief Reports an all-NaN/Inf result, once per converter instance.
/// @param element Element to post a GStreamer warning on; if null, the warning goes to the debug log.
void BoxesLabelsScoresConverter::warnNonFinite(GstElement *element) {
    std::call_once(non_finite_warning, [&] {
        const std::string message = "Model '" + getModelName() +
                                    "' returned only NaN/Inf detections - the inference device likely produced "
                                    "invalid results (e.g. FP16 overflow). Try another device.";
        if (element)
            GST_ELEMENT_WARNING(element, STREAM, FAILED, ("%s", message.c_str()), (NULL));
        else
            GVA_WARNING("%s", message.c_str());
    });
}

/// @brief Decodes all images in the batch into detections.
/// @param output_blobs Model outputs of one inference request.
/// @param only_non_finite Set to true when every box in the batch was NaN/Inf.
/// @return Detection tensors for each image after thresholding, NMS and validation; throws std::runtime_error
///         (with the cause nested) if the outputs don't match the layout.
TensorsTable BoxesLabelsScoresConverter::convertBlobs(const OutputBlobs &output_blobs, bool &only_non_finite) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &input_info = getModelInputImageInfo();
        const size_t batch_size = input_info.batch_size;

        const auto &boxes_blob = getBlob(output_blobs, layout.boxes_name);
        const auto boxes = getBoxesShape(boxes_blob->GetDims());
        if (!boxes || boxes->box_size != layout.box_size || boxes->images != batch_size)
            throw std::invalid_argument("Unexpected '" + layout.boxes_name + "' dimensions.");
        if (boxes_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32)
            throw std::invalid_argument("'" + layout.boxes_name + "' must be FP32.");
        const auto *boxes_data = static_cast<const float *>(boxes_blob->GetData());

        const auto getPerBoxBlob = [&](const std::string &name) {
            const auto &blob = getBlob(output_blobs, name);
            if (!hasValuePerBox(blob->GetDims(), *boxes))
                throw std::invalid_argument("'" + name + "' dimensions do not match '" + layout.boxes_name + "'.");
            return blob;
        };

        const InferenceBackend::OutputBlob::Ptr labels_blob = layout.has_labels ? getPerBoxBlob(labels_name) : nullptr;
        const float *scores_data = nullptr;
        if (layout.has_scores) {
            const auto scores_blob = getPerBoxBlob(scores_name);
            if (scores_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32)
                throw std::invalid_argument("'" + scores_name + "' must be FP32.");
            scores_data = static_cast<const float *>(scores_blob->GetData());
        }

        const double x_scale = layout.normalized ? 1.0 : 1.0 / input_info.width;
        const double y_scale = layout.normalized ? 1.0 : 1.0 / input_info.height;
        double roi_scale = 1.0;
        gst_structure_get_double(getModelProcOutputInfo().get(), "roi_scale", &roi_scale);

        DetectedObjectsTable objects_table(batch_size);
        size_t non_finite_count = 0;
        for (size_t image = 0; image < batch_size; ++image) {
            for (size_t proposal = 0; proposal < boxes->proposals; ++proposal) {
                const size_t index = image * boxes->proposals + proposal;
                const float *box = boxes_data + index * boxes->box_size;

                const float confidence = scores_data ? scores_data[index] : (boxes->box_size == 5 ? box[4] : 1.0f);
                // Checked before the threshold so an all-NaN device result can be reported, not just filtered out.
                if (!std::isfinite(confidence) ||
                    !std::all_of(box, box + 4, [](float value) { return std::isfinite(value); })) {
                    ++non_finite_count;
                    continue;
                }
                if (confidence < confidence_threshold)
                    continue;

                const int64_t label_id = labels_blob ? readLabel(*labels_blob, index) : 0;
                if (label_id < 0)
                    continue;

                double x = std::clamp(box[0] * x_scale, 0.0, 1.0);
                double y = std::clamp(box[1] * y_scale, 0.0, 1.0);
                double width = std::clamp(box[2] * x_scale, 0.0, 1.0) - x;
                double height = std::clamp(box[3] * y_scale, 0.0, 1.0) - y;

                if (roi_scale > 0 && roi_scale != 1) {
                    x += width / 2 * (1 - roi_scale);
                    y += height / 2 * (1 - roi_scale);
                    width *= roi_scale;
                    height *= roi_scale;
                }

                const size_t label = static_cast<size_t>(label_id);
                objects_table[image].emplace_back(x, y, width, height, 0.0, confidence, label,
                                                  getLabelByLabelId(label));
            }
        }

        only_non_finite = non_finite_count > 0 && non_finite_count == batch_size * boxes->proposals;

        return storeObjects(objects_table);
    } catch (const std::exception &) {
        std::throw_with_nested(std::runtime_error("Failed to do " + getName() + " post-processing."));
    }
}
