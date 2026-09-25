/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Layer 3 contract test for the gvawatermark segmentation mask dims regression
// (commit c0bf460a / PR #1047). Layers 1 and 2 each independently assert that a
// producer and the consumer agree with a *documented* [W,H] convention - but never
// verify that the real producer's actual output tensor, unmodified, renders correctly
// through the real consumer. This test closes that gap: it runs the genuine
// MaskRCNNConverter::convert() on a synthetic, non-square inference blob, takes its
// unmodified GstStructure output, attaches it to an ROI, and pushes it through the
// real gvawatermarkimpl element - so a convention drift between producer and consumer
// is caught even if each side still "documents" a convention correctly in isolation.

#include "test_common.h"
#include "test_utils.h"

#include "common/post_processor/converters/to_roi/mask_rcnn.h"

#include <cstring>
#include <dlstreamer/gst/videoanalytics/tensor.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsobjectdetectionmtd.h>
#include <gst/video/gstvideometa.h>

using namespace post_processing;
using namespace InferenceBackend;

constexpr char impl_name[] = "gvawatermarkimpl";

#define WATERMARK_BGR_CAPS GST_VIDEO_CAPS_MAKE("BGR")

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));
static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));

namespace {

class FakeOutputBlob : public OutputBlob {
    std::vector<uint8_t> _data;
    std::vector<size_t> _dims;
    Blob::Precision _precision;

  public:
    template <typename T>
    FakeOutputBlob(std::vector<size_t> dims, const std::vector<T> &values, Blob::Precision precision)
        : _dims(std::move(dims)), _precision(precision) {
        _data.resize(values.size() * sizeof(T));
        std::memcpy(_data.data(), values.data(), _data.size());
    }

    const std::vector<size_t> &GetDims() const override {
        return _dims;
    }
    Layout GetLayout() const override {
        return Layout::ANY;
    }
    Precision GetPrecision() const override {
        return _precision;
    }
    const void *GetData() const override {
        return _data.data();
    }
};

// ROI placed on the output frame - independent of the mask_rcnn model's own input/box space.
constexpr guint ROI_X = 10, ROI_Y = 10, ROI_W = 30, ROI_H = 60;
constexpr guint FRAME_W = 100, FRAME_H = 100; // BGR packed 3B/px; FRAME_W*3 needs no row padding
static Resolution frame_resolution = {(gint)FRAME_W, (gint)FRAME_H};

// Deliberately oblong so a width/height swap is not visually coincidental (see Layer 2 notes).
constexpr size_t MASK_W = 5, MASK_H = 50;
constexpr size_t MASK_ON_ROWS = 3; // bottom-most rows are "on"

static bool pixel_is_colored(GstBuffer *buffer, guint x, guint y) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    gsize stride = FRAME_W * 3;
    gsize offset = y * stride + x * 3;
    ck_assert(offset + 2 < info.size);
    bool colored = info.data[offset] != 0 || info.data[offset + 1] != 0 || info.data[offset + 2] != 0;
    gst_buffer_unmap(buffer, &info);
    return colored;
}

struct PixelStats {
    bool bottom_left_colored;
    bool top_center_colored;
};

static void record_pixel_stats(GstBuffer *buffer, gpointer user_data) {
    PixelStats *stats = (PixelStats *)user_data;
    stats->bottom_left_colored = pixel_is_colored(buffer, ROI_X + 3, ROI_Y + ROI_H - 3);
    stats->top_center_colored = pixel_is_colored(buffer, ROI_X + ROI_W / 2, ROI_Y + 3);
}

// Runs the real MaskRCNNConverter on a synthetic blob and returns its unmodified
// instance-segmentation tensor structure (ownership transferred to the caller).
static GstStructure *run_real_mask_rcnn_converter() {
    GstStructure *model_proc_output_info = gst_structure_new_empty("ANY");

    BlobToMetaConverter::Initializer init;
    init.model_name = "contract_test_model";
    init.input_image_info.width = 64;
    init.input_image_info.height = 32;
    init.input_image_info.batch_size = 1;
    init.model_proc_output_info = GstStructureUniquePtr(model_proc_output_info, [](GstStructure *) {});
    init.labels = {"background", "object"};

    std::vector<float> boxes_data = {0.f, 0.f, 64.f, 32.f, 0.9f};
    std::vector<int64_t> labels_data = {0}; // -> main_class = 0 + 1 = 1 ("object")
    std::vector<float> masks_data(MASK_W * MASK_H);
    for (size_t r = 0; r < MASK_H; r++)
        for (size_t c = 0; c < MASK_W; c++)
            masks_data[r * MASK_W + c] = (r >= MASK_H - MASK_ON_ROWS) ? 1.0f : 0.0f;

    OutputBlobs blobs{
        {"boxes", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, 5}, boxes_data, Blob::Precision::FP32)},
        {"labels", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1}, labels_data, Blob::Precision::I64)},
        {"masks", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, MASK_H, MASK_W}, masks_data,
                                                   Blob::Precision::FP32)},
    };

    MaskRCNNConverter converter(std::move(init), 0.5, 0.4);
    TensorsTable result = converter.convert(blobs);
    gst_structure_free(model_proc_output_info);

    ck_assert(!result.empty() && !result[0].empty());
    for (GstStructure *s : result[0][0]) {
        if (gst_structure_get_name_id(s) != g_quark_from_string("detection"))
            return gst_structure_copy(s); // detach a copy; original TensorsTable structures are left as-is
    }
    ck_assert_msg(false, "MaskRCNNConverter did not produce a segmentation tensor");
    return nullptr;
}

static void setup_buffer_with_real_converter_output(GstBuffer *buffer, gpointer /*user_data*/) {
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd od_mtd;
    gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string("object"), (gint)ROI_X, (gint)ROI_Y,
                                           (gint)ROI_W, (gint)ROI_H, 0.95f, &od_mtd);
    GstVideoRegionOfInterestMeta *roi_meta =
        gst_buffer_add_video_region_of_interest_meta(buffer, "object", ROI_X, ROI_Y, ROI_W, ROI_H);
    roi_meta->id = od_mtd.id;

    // Mirror the real attachment path (meta_attacher.cpp TensorToROIAttacher::attach): convert the
    // producer's tensor to a proper GstAnalyticsMtd and relate it to the ROI via CONTAIN, instead of
    // the legacy gst_video_region_of_interest_meta_add_param - segmentation-typed tensors are filtered
    // out of that legacy path by GVA::RegionOfInterest's constructor.
    GstStructure *seg_structure = run_real_mask_rcnn_converter();
    GVA::Tensor gva_tensor(seg_structure);
    GstAnalyticsMtd tensor_mtd;
    ck_assert(
        gva_tensor.convert_to_meta(&tensor_mtd, relation_meta, (gint)ROI_X, (gint)ROI_Y, (gint)ROI_W, (gint)ROI_H));
    ck_assert(gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id,
                                                       tensor_mtd.id));
    gst_structure_free(seg_structure);
}

} // namespace

GST_START_TEST(test_mask_rcnn_producer_matches_watermark_consumer) {
    g_print("Starting test: test_mask_rcnn_producer_matches_watermark_consumer\n");

    PixelStats stats = {false, false};
    run_test(impl_name, WATERMARK_BGR_CAPS, frame_resolution, &srctemplate, &sinktemplate,
             setup_buffer_with_real_converter_output, record_pixel_stats, &stats, NULL);

    ck_assert_msg(stats.bottom_left_colored,
                  "mask_rcnn's real output dims and gvawatermarkimpl's mask_size construction disagree - "
                  "bottom-left of ROI should be colored by the full-width bottom band");
    ck_assert_msg(!stats.top_center_colored, "Expected top-center of ROI to remain unmodified");
}
GST_END_TEST;

static Suite *watermark_segmentation_contract_suite(void) {
    Suite *s = suite_create("watermark_segmentation_contract");

    TCase *tc = tcase_create("mask_rcnn_producer_consumer_agreement");
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_mask_rcnn_producer_matches_watermark_consumer);

    return s;
}

GST_CHECK_MAIN(watermark_segmentation_contract);
