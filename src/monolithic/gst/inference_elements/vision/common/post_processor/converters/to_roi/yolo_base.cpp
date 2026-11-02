/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_base.h"
#include "yolo_v2.h"
#include "yolo_v3.h"
#include "yolo_v4.h"
#include "yolo_v5.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace post_processing;

namespace {
std::vector<float> getAnchors(GstStructure *s) {
    if (!gst_structure_has_field(s, "anchors"))
        throw std::runtime_error("model proc does not have \"anchors\" parameter.");

    GValueArray *arr = nullptr;
    gst_structure_get_array(s, "anchors", &arr);

    std::vector<float> anchors;
    if (arr) {
        anchors.reserve(arr->n_values);
        for (guint i = 0; i < arr->n_values; ++i)
            anchors.push_back(g_value_get_double(g_value_array_get_nth(arr, i)));
        g_value_array_free(arr);
    } else {
        throw std::runtime_error("\"anchors\" array is null.");
    }

    return anchors;
}

size_t getClassesNum(GstStructure *s, size_t labels_num) {
    if (!gst_structure_has_field(s, "classes") && labels_num)
        return labels_num;

    int _classes = 0;
    gst_structure_get_int(s, "classes", &_classes);

    size_t classes = safe_convert<size_t>(_classes);

    if (!labels_num)
        return classes;

    if (classes < labels_num)
        GST_WARNING("Number of classes does not match with namber of labels: classes=%lu, labels=%lu.", classes,
                    labels_num);

    if (classes > labels_num)
        throw std::runtime_error("Number of classes greater then number of labels.");

    return classes;
}

std::pair<size_t, size_t> getCellsNumber(GstStructure *s) {
    const bool has_cells_number_x = gst_structure_has_field(s, "cells_number_x");
    const bool has_cells_number_y = gst_structure_has_field(s, "cells_number_y");
    const bool has_cells_number = gst_structure_has_field(s, "cells_number");

    if ((has_cells_number_x or has_cells_number_y) && has_cells_number)
        throw std::runtime_error("Please set only \"cells_number_x\" and \"cells_number_y\" or only \"cells_number\".");

    if ((has_cells_number_x and !has_cells_number_y) or (!has_cells_number_x and has_cells_number_y))
        throw std::runtime_error("Please set both \"cells_number_x\" and \"cells_number_y\" or only \"cells_number\".");

    if (has_cells_number_x && has_cells_number_y && !has_cells_number) {
        int cells_number_x = 0;
        int cells_number_y = 0;

        gst_structure_get_int(s, "cells_number_x", &cells_number_x);
        gst_structure_get_int(s, "cells_number_y", &cells_number_y);

        return std::make_pair(safe_convert<size_t>(cells_number_x), safe_convert<size_t>(cells_number_y));
    } else if (has_cells_number) {
        int cells_number = 0;

        gst_structure_get_int(s, "cells_number", &cells_number);

        return std::make_pair(safe_convert<size_t>(cells_number), safe_convert<size_t>(cells_number));
    } else {
        GST_WARNING(
            "model-proc does not have \"cells_number\" or \"cells_number_x\" and \"cells_number_y\" parameters.");
    }

    return std::make_pair(0, 0);
}

size_t getBboxNumberOnCell(GstStructure *s) {
    int bbox_number_on_cell = 0;
    if (gst_structure_has_field(s, "bbox_number_on_cell")) {
        gst_structure_get_int(s, "bbox_number_on_cell", &bbox_number_on_cell);
    } else {
        GST_WARNING("model-proc file does not have \"bbox_number_on_cell\" parameter.");
    }
    return safe_convert<size_t>(bbox_number_on_cell);
}

double getIOUThreshold(GstStructure *s) {
    double iou_threshold = 0.5;
    if (gst_structure_has_field(s, "iou_threshold")) {
        gst_structure_get_double(s, "iou_threshold", &iou_threshold);
    }

    return iou_threshold;
}

bool getDoClsSoftmax(GstStructure *s) {
    gboolean do_cls_sftm = FALSE;
    if (gst_structure_has_field(s, "do_cls_softmax")) {
        gst_structure_get_boolean(s, "do_cls_softmax", &do_cls_sftm);
    }

    return do_cls_sftm;
}

bool getOutputSigmoidActivation(GstStructure *s) {
    gboolean do_coords_sgmd = FALSE;
    if (gst_structure_has_field(s, "output_sigmoid_activation")) {
        gst_structure_get_boolean(s, "output_sigmoid_activation", &do_coords_sgmd);
    }

    return do_coords_sgmd;
}

static const std::unordered_map<YOLOBaseConverter::OutputDimsLayout, YOLOBaseConverter::LayoutDesc> layout_to_desc = {
    {YOLOBaseConverter::OutputDimsLayout::BCxCy, YOLOBaseConverter::LayoutDesc{-1, 0, 1, 2}},
    {YOLOBaseConverter::OutputDimsLayout::BCyCx, YOLOBaseConverter::LayoutDesc{-1, 0, 2, 1}},
    {YOLOBaseConverter::OutputDimsLayout::CxCyB, YOLOBaseConverter::LayoutDesc{-1, 2, 0, 1}},
    {YOLOBaseConverter::OutputDimsLayout::CxCyB, YOLOBaseConverter::LayoutDesc{-1, 2, 1, 0}},
    {YOLOBaseConverter::OutputDimsLayout::NBCxCy, YOLOBaseConverter::LayoutDesc{0, 1, 2, 3}},
    {YOLOBaseConverter::OutputDimsLayout::NBCyCx, YOLOBaseConverter::LayoutDesc{0, 1, 3, 2}},
    {YOLOBaseConverter::OutputDimsLayout::NCxCyB, YOLOBaseConverter::LayoutDesc{0, 3, 1, 2}},
    {YOLOBaseConverter::OutputDimsLayout::NCyCxB, YOLOBaseConverter::LayoutDesc{0, 3, 2, 1}}};

bool matchDims(const YOLOBaseConverter::LayoutDesc &desc, const std::vector<size_t> &dims, size_t cell_x, size_t cell_y,
               size_t boxes_data) {
    if (dims.size() == 3 && desc.N != -1)
        return false;

    // cells number are same (including 0) *CxCy* prefered to keep backward compatibility
    if (cell_x == cell_y) {
        if (desc.Cx < desc.Cy && boxes_data == dims[desc.B])
            return true;
    } else {
        if (cell_x == dims[desc.Cx] && cell_y == dims[desc.Cy] && boxes_data == dims[desc.B])
            return true;
    }
    return false;
}

} // namespace

bool YOLOBaseConverter::LayoutDesc::operator==(const LayoutDesc &desc) const {
    return (desc.Cx == Cx && desc.Cy == Cy && desc.B == B && desc.N == N);
};

YOLOBaseConverter::LayoutDesc::operator bool() const {
    return (Cx != -1 && Cy != -1 && B != -1);
}

YOLOBaseConverter::LayoutDesc YOLOBaseConverter::LayoutDesc::fromLayout(YOLOBaseConverter::OutputDimsLayout layout) {
    YOLOBaseConverter::LayoutDesc desc{};
    auto it = layout_to_desc.find(layout);
    if (it != layout_to_desc.end())
        desc = it->second;
    return desc;
}

size_t YOLOBaseConverter::tryAutomaticConfigWithDims(const std::vector<size_t> &dims, OutputDimsLayout layout,
                                                     size_t boxes, size_t classes, std::pair<size_t, size_t> &cells) {

    size_t &cells_x = cells.first;
    size_t &cells_y = cells.second;

    auto desc = LayoutDesc::fromLayout(layout);
    if (!desc)
        throw std::runtime_error("Unsupported output layout.");

    cells_x = dims[desc.Cx];
    cells_y = dims[desc.Cy];
    return cells.first * cells.second * boxes * (classes + 5);
}

std::pair<std::vector<size_t>, size_t> YOLOBaseConverter::getMinBlobDims(const ModelOutputsInfo &outputs_info) {
    auto min_size_dims = outputs_info.cbegin()->second;
    size_t min_blob_size = std::numeric_limits<size_t>::max();

    for (const auto &output_info_pair : outputs_info) {
        const auto &blob_dims = output_info_pair.second;

        size_t blob_size = std::accumulate(blob_dims.cbegin(), blob_dims.cend(), 1lu, std::multiplies<size_t>());
        min_blob_size = std::min(min_blob_size, blob_size);

        if (blob_size == min_blob_size)
            min_size_dims = blob_dims;
    }

    return std::pair<std::vector<size_t>, size_t>(min_size_dims, min_blob_size);
}

YOLOBaseConverter::OutputDimsLayout YOLOBaseConverter::getLayoutFromDims(const ModelOutputsInfo &outputs_info,
                                                                         const std::vector<float> &anchors,
                                                                         size_t classes,
                                                                         std::pair<size_t, size_t> cells_number) {
    auto min_blob_dims_pair = getMinBlobDims(outputs_info);
    const auto &min_blob_dims = min_blob_dims_pair.first;

    if (min_blob_dims.size() == 1)
        return YOLOBaseConverter::OutputDimsLayout::NO;

    size_t boxes = anchors.size() / (outputs_info.size() * 2);
    size_t boxes_data = boxes * (classes + 5);
    size_t cells_x = cells_number.first;
    size_t cells_y = cells_number.second;
    auto it = std::find_if(layout_to_desc.begin(), layout_to_desc.end(),
                           [=](auto &e) { return matchDims(e.second, min_blob_dims, cells_x, cells_y, boxes_data); });

    auto layout = YOLOBaseConverter::OutputDimsLayout::NO;
    if (it != layout_to_desc.end())
        layout = it->first;
    return layout;
}

bool YOLOBaseConverter::tryAutomaticConfig(const ModelImageInputInfo &input_info, const ModelOutputsInfo &outputs_info,
                                           OutputDimsLayout dims_layout, size_t classes,
                                           const std::vector<float> &anchors, std::pair<size_t, size_t> &cells,
                                           size_t &boxes) {

    boxes = anchors.size() / (outputs_info.size() * 2);
    cells.first = 0;
    cells.second = 0;

    auto min_blob_dims = getMinBlobDims(outputs_info);

    size_t batch_size = input_info.batch_size;

    if (dims_layout != OutputDimsLayout::NO) {
        size_t result_blob_size = tryAutomaticConfigWithDims(min_blob_dims.first, dims_layout, boxes, classes, cells);

        if (result_blob_size * batch_size == min_blob_dims.second)
            return true;
    }

    cells.first = input_info.width / default_downsample_degree;
    cells.second = input_info.height / default_downsample_degree;

    return min_blob_dims.second == batch_size * cells.first * cells.second * boxes * (classes + 5);
}

BlobToMetaConverter::Ptr YOLOBaseConverter::create(BlobToMetaConverter::Initializer initializer,
                                                   const std::string &converter_name, double confidence_threshold) {
    try {
        GstStructure *model_proc_output_info = initializer.model_proc_output_info.get();

        const auto classes_number = getClassesNum(model_proc_output_info, initializer.labels.size());
        if (!classes_number)
            throw std::runtime_error("Number of classes if null.");

        const auto anchors = getAnchors(model_proc_output_info);
        if (anchors.empty())
            throw std::runtime_error("Anchors is empty.");

        const auto iou_threshold = getIOUThreshold(model_proc_output_info);
        const auto do_cls_softmax = getDoClsSoftmax(model_proc_output_info);
        const auto output_sigmoid_activation = getOutputSigmoidActivation(model_proc_output_info);

        std::pair<size_t, size_t> cells_number = getCellsNumber(model_proc_output_info);
        size_t bbox_number_on_cell = getBboxNumberOnCell(model_proc_output_info);

        OutputDimsLayout dims_layout =
            getLayoutFromDims(initializer.outputs_info, anchors, classes_number, cells_number);

        if (!(cells_number.first && cells_number.second && bbox_number_on_cell)) {
            GST_WARNING("\"cells_number\" and \"bbox_number_on_cell\" weren't found in model-proc file. Trying to set "
                        "them automatically.");
            bool is_configurated =
                tryAutomaticConfig(initializer.input_image_info, initializer.outputs_info, dims_layout, classes_number,
                                   anchors, cells_number, bbox_number_on_cell);
            if (!is_configurated)
                throw std::runtime_error("Failed to match parameters. Please define them yourself in model-proc file.");

            GST_WARNING("Result of automatic configuration: cells_number_x=%lu, cells_number_y=%lu, "
                        "bbox_number_on_cell=%lu.",
                        cells_number.first, cells_number.second, bbox_number_on_cell);
        }

        const auto &outputs_info = initializer.outputs_info;

        if (anchors.size() != bbox_number_on_cell * 2 * outputs_info.size())
            throw std::runtime_error("Anchors size must be equal (bbox_number_on_cell * layers_number * 2).");

        OutputLayerShapeConfig output_shape_info(classes_number, cells_number.first, cells_number.second,
                                                 bbox_number_on_cell);
        YOLOBaseConverter::Initializer yolo_initializer = {anchors, output_shape_info, do_cls_softmax,
                                                           output_sigmoid_activation, dims_layout};

        if (converter_name == YOLOv2Converter::getName()) {
            YOLOv2Converter::checkModelProcOutputs(cells_number, bbox_number_on_cell, classes_number, outputs_info,
                                                   dims_layout, initializer.input_image_info);

            return BlobToMetaConverter::Ptr(
                new YOLOv2Converter(std::move(initializer), confidence_threshold, iou_threshold, yolo_initializer));
        }
        if (converter_name == YOLOv3Converter::getName()) {
            const auto masks =
                YOLOv3Converter::getMask(model_proc_output_info, bbox_number_on_cell,
                                         std::min(cells_number.first, cells_number.second), outputs_info.size());

            YOLOv3Converter::checkModelProcOutputs(cells_number, bbox_number_on_cell, classes_number, masks,
                                                   outputs_info, dims_layout, initializer.input_image_info);

            return BlobToMetaConverter::Ptr(new YOLOv3Converter(std::move(initializer), confidence_threshold,
                                                                iou_threshold, yolo_initializer, masks));
        }
        if (converter_name == YOLOv4Converter::getName()) {
            const auto masks =
                YOLOv4Converter::getMask(model_proc_output_info, bbox_number_on_cell,
                                         std::min(cells_number.first, cells_number.second), outputs_info.size());

            YOLOv4Converter::checkModelProcOutputs(cells_number, bbox_number_on_cell, classes_number, masks,
                                                   outputs_info, dims_layout, initializer.input_image_info);

            return BlobToMetaConverter::Ptr(new YOLOv4Converter(std::move(initializer), confidence_threshold,
                                                                iou_threshold, yolo_initializer, masks));
        }
        if (converter_name == YOLOv5Converter::getName()) {
            const auto masks =
                YOLOv5Converter::getMask(model_proc_output_info, bbox_number_on_cell,
                                         std::min(cells_number.first, cells_number.second), outputs_info.size());

            YOLOv5Converter::checkModelProcOutputs(cells_number, bbox_number_on_cell, classes_number, masks,
                                                   outputs_info, dims_layout, initializer.input_image_info);

            return BlobToMetaConverter::Ptr(new YOLOv5Converter(std::move(initializer), confidence_threshold,
                                                                iou_threshold, yolo_initializer, masks));
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to create \"" + converter_name + "\" converter."));
    }
    return nullptr;
}

TensorsTable YOLOBaseConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            for (const auto &blob_iter : output_blobs) {
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (!blob)
                    throw std::invalid_argument("Output blob is nullptr.");

                if (!blob->GetData())
                    throw std::runtime_error("Output blob data is nullptr.");

                if (blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32)
                    throw std::runtime_error("Unsupported label precision.");

                size_t unbatched_size = blob->GetSize() / batch_size;
                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), unbatched_size, objects);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do YoloV3 post-processing."));
    }
    return TensorsTable{};
}
