/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gva_dwelltime_meta.h"
#include "glib.h"
#include "gst/analytics/analytics.h"

#include <glib/gslice.h>
#include <gmock/gmock.h>
#include <gst/gstbuffer.h>
#include <gst/gstinfo.h>
#include <gst/gstmeta.h>
#include <gtest/gtest.h>

#include <gst/check/gstcheck.h>

#include <gst/video/gstvideometa.h>

using ::testing::_;
using ::testing::Mock;
using ::testing::Return;

#include "dlstreamer/gst/videoanalytics/region_of_interest.h"

struct RegionOfInterestTest : public ::testing::Test {

    // TODO: rename to SetUpTestSuite when migrate to googletest version higher than 1.8
    // https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#sharing-resources-between-tests-in-the-same-test-suite
    // static void SetUpTestCase() {
    // }

    GstBuffer *buffer;
    GVA::RegionOfInterest *region_of_interest;

    void SetUp() {
        buffer = gst_buffer_new_and_alloc(0);
        GstVideoInfo info;
        gst_video_info_set_format(&info, GST_VIDEO_FORMAT_NV12, 1920, 1080); // FullHD

        const gchar *label = "detection";

        GstVideoRegionOfInterestMeta *meta =
            gst_buffer_add_video_region_of_interest_meta(buffer, label, 0.0, 0.0, 0.3, 0.6);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(relation_meta, nullptr);

        GQuark label_quark = g_quark_from_string(label);

        GstAnalyticsODMtd od_mtd;
        gboolean ret = gst_analytics_relation_meta_add_oriented_od_mtd(relation_meta, label_quark, 0.0, 0.0, 0.3, 0.6,
                                                                       0.0, 0.77f, &od_mtd);
        ASSERT_TRUE(ret);

        meta->id = od_mtd.id;

        region_of_interest = new GVA::RegionOfInterest(od_mtd, meta);
    }

    void TearDown() {
        if (buffer)
            gst_buffer_unref(buffer);
        if (region_of_interest)
            delete region_of_interest;
    }
};

TEST_F(RegionOfInterestTest, RegionOfInterestTestTensors) {
    ASSERT_EQ(region_of_interest->tensors().size(), 0);

    std::vector<GVA::Tensor> test_tensors;
    const size_t tensors_num = 10;
    for (size_t i = 0; i < tensors_num; ++i) {
        GstStructure *gst_structure = gst_structure_new_empty(("tensor_" + std::to_string(i)).c_str());
        GVA::Tensor tensor(gst_structure);
        tensor.set_double("confidence", (double)i / (double)tensors_num);
        region_of_interest->add_tensor(tensor);
        test_tensors.push_back(tensor);
    }
    GstStructure *detection_structure = gst_structure_new_empty("detection");
    GVA::Tensor detection(detection_structure);
    detection.set_double("confidence", 0.77);
    region_of_interest->add_tensor(detection);

    ASSERT_EQ(region_of_interest->tensors().size(), tensors_num + 1);
    ASSERT_DOUBLE_EQ(region_of_interest->confidence(), 0.77f);
    ASSERT_DOUBLE_EQ((*region_of_interest).tensors()[5].confidence(), 0.5);

    double confidence = 0.0;
    for (GVA::Tensor tensor : region_of_interest->tensors()) {
        if (!tensor.is_detection()) {
            ASSERT_DOUBLE_EQ(tensor.confidence(), confidence);
            confidence += 0.1;
        } else
            ASSERT_DOUBLE_EQ(tensor.confidence(), 0.77);
    }

    ASSERT_EQ(region_of_interest->tensors().size(), test_tensors.size() + 1);
    auto tensors_roi = region_of_interest->tensors();
    for (size_t i = 0; i < tensors_num; ++i) {
        ASSERT_EQ(tensors_roi[i].confidence(), test_tensors[i].confidence());
    }
}

TEST_F(RegionOfInterestTest, RegionOfInterestTestDwellTimes) {
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    ASSERT_NE(relation_meta, nullptr);

    GstAnalyticsDwellTimeMtd dwell_mtd;
    gboolean ret = gst_analytics_relation_meta_add_dwelltime_mtd(relation_meta, "zone-a", 2.5, 10.0, &dwell_mtd);
    ASSERT_TRUE(ret);

    ret = gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                   region_of_interest->region_id(), dwell_mtd.id);
    ASSERT_TRUE(ret);

    auto dwell_times = region_of_interest->dwell_times();
    ASSERT_EQ(dwell_times.size(), 1);
    ASSERT_EQ(dwell_times[0].zone_id, "zone-a");
    ASSERT_DOUBLE_EQ(dwell_times[0].dwell_time_sec, 2.5);
    ASSERT_DOUBLE_EQ(dwell_times[0].first_seen_timestamp_sec, 10.0);
}
