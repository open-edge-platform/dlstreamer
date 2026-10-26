/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* Requesting and releasing pads, and the events and metadata the source pad
 * carries. None of these run a pipeline for longer than it takes to make the
 * point; the pad ones do not run one at all.
 */

#include "streammux_harness.h"

#include <gtest/gtest.h>

namespace {

struct PadRequest : public ::testing::TestWithParam<const char *> {
    const char *elementName() const {
        return GetParam();
    }
    const char *padTemplate() const {
        return g_str_equal(GetParam(), "gvastreammux") ? "sink_%u" : "src_%u";
    }
    std::string padName(guint i) const {
        return (g_str_equal(GetParam(), "gvastreammux") ? "sink_" : "src_") + std::to_string(i);
    }
};

/* Asking for a pad without naming it must give a free index. Taking the pad
 * count instead collided with a live pad once one in the middle had been
 * released -- on the demuxer that crashed the process. */
TEST_P(PadRequest, AnUnnamedRequestTakesTheLowestFreeIndex) {
    GstElement *element = gst_element_factory_make(elementName(), nullptr);
    ASSERT_NE(element, nullptr);

    std::vector<GstPad *> pads;
    for (guint i = 0; i < 3; i++)
        pads.push_back(gst_element_request_pad_simple(element, padName(i).c_str()));
    ASSERT_NE(pads[1], nullptr);

    gst_element_release_request_pad(element, pads[1]);
    gst_object_unref(pads[1]);

    /* An unnamed request, which is how an application asks the element to
     * choose: request_pad_simple needs a name, so go through the template. */
    GstPadTemplate *templ = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(element), padTemplate());
    ASSERT_NE(templ, nullptr);
    GstPad *reused = gst_element_request_pad(element, templ, nullptr, nullptr);
    ASSERT_NE(reused, nullptr) << "an unnamed request was refused while an index was free";
    EXPECT_STREQ(GST_PAD_NAME(reused), padName(1).c_str());

    gst_element_release_request_pad(element, reused);
    gst_object_unref(reused);
    gst_object_unref(pads[0]);
    gst_object_unref(pads[2]);
    gst_object_unref(element);
}

/* An index that is in use must be refused rather than quietly replacing the
 * pad already there, which leaked it along with everything queued on it. */
TEST_P(PadRequest, AnIndexAlreadyInUseIsRefused) {
    GstElement *element = gst_element_factory_make(elementName(), nullptr);
    ASSERT_NE(element, nullptr);

    GstPad *first = gst_element_request_pad_simple(element, padName(0).c_str());
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(gst_element_request_pad_simple(element, padName(0).c_str()), nullptr);

    gst_element_release_request_pad(element, first);
    gst_object_unref(first);
    gst_object_unref(element);
}

/* The ceiling is a documented limit, so it is worth pinning. */
TEST_P(PadRequest, AnIndexBeyondTheCeilingIsRefused) {
    GstElement *element = gst_element_factory_make(elementName(), nullptr);
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(gst_element_request_pad_simple(element, padName(256).c_str()), nullptr);
    gst_object_unref(element);
}

INSTANTIATE_TEST_SUITE_P(BothElements, PadRequest, ::testing::Values("gvastreammux", "gvastreamdemux"),
                         [](const ::testing::TestParamInfo<const char *> &info) { return std::string(info.param); });

/* The muxer presents one stream assembled from several, so downstream must see
 * one stream-start -- not one per source, which left the stream id being
 * whichever arrived last. */
TEST(StreammuxPads, OneStreamStartRegardlessOfSourceCount) {
    StreammuxHarness h;
    for (guint i = 0; i < 4; i++)
        h.addSource(i);
    h.start();

    for (int i = 0; i < 4; i++)
        for (guint s = 0; s < 4; s++)
            h.push(s, i * StreammuxHarness::kFrameDuration);

    ASSERT_TRUE(h.waitFor([&] { return h.outputCount() > 0; }));
    EXPECT_EQ(h.srcEventCount(GST_EVENT_STREAM_START), 1u);
}

} // namespace
