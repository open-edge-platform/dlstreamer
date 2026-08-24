/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/audio_infer_impl.h"
#include "base/gst_allocator_wrapper.h"
#include "base/inference_coordinator.h"
#include "common/post_processor.h"
#include "glib.h"
#include "gvaclassify/classification_history.h"
#include "openvino/safe_queue.h"
#include "post_processor/frame_wrapper.h"
#include "tracker_factory.h"
#include "vas/common.h"
#include "vas/components/ot/mtt/hungarian_wrap.h"
#include "vas/components/ot/zero_term_chist_tracker.h"

#define GENERATE(FIELD)                                                                                                \
    template <typename T>                                                                                              \
    constexpr auto is_visible##FIELD(T const &t)->decltype(t->FIELD, true) {                                           \
        return true;                                                                                                   \
    }                                                                                                                  \
                                                                                                                       \
    constexpr auto is_visible##FIELD(...) {                                                                            \
        return false;                                                                                                  \
    }

GENERATE(priv);
GENERATE(pub);

GENERATE(_label);
GENERATE(_impl);
GENERATE(labels);
GENERATE(Tracker);
GENERATE(ZeroTermChistTracker);
GENERATE(TrackObjects);

GENERATE(major_);
GENERATE(minor_);
GENERATE(patch_);

GENERATE(registered_all);
GENERATE(registred_trackers);

GENERATE(model);
GENERATE(model_proc);
GENERATE(device);

GENERATE(audioData);
GENERATE(inferenceStartTime);
GENERATE(startTimeSet);
GENERATE(audio_base_inference);
GENERATE(sliding_samples);

GENERATE(queue_);
GENERATE(mutex_);
GENERATE(condition_);

GENERATE(gva_classify);
GENERATE(current_num_frame);
GENERATE(history);
GENERATE(history_mutex);

GENERATE(buffer);
GENERATE(model_instance_id);
GENERATE(roi);
GENERATE(image_transform_info);
GENERATE(width);
GENERATE(height);

GENERATE(size_width_);
GENERATE(size_height_);
GENERATE(kHungarianNotAssigned);
GENERATE(kHungarianAssigned);
GENERATE(kIntMax);
GENERATE(int_cost_map_rows_);
GENERATE(int_cost_map_);
GENERATE(problem_);

TEST(oo_permissions_test, self_test) {
    class C {
      public:
        char pub;

      private:
        char priv;
    } c;

    ASSERT_EQ(is_visiblepub(&c), true);
    ASSERT_EQ(is_visiblepriv(&c), false);
}

TEST(oo_permissions_test, tracker_test) {
    using vas::ot::ZeroTermChistTracker;
    using InitParameters = vas::ot::Tracker::InitParameters;
    InitParameters init_parameters;
    ZeroTermChistTracker tracker(init_parameters);
    ASSERT_EQ(is_visible_label(&tracker), false);
    ASSERT_EQ(is_visible_impl(&tracker), false);
    ASSERT_EQ(is_visiblelabels(&tracker), false);
    ASSERT_EQ(is_visibleTracker(&tracker), false);
    ASSERT_EQ(is_visibleZeroTermChistTracker(&tracker), false);
    ASSERT_EQ(is_visibleTrackObjects(&tracker), false);
}

TEST(oo_permissions_test, version_test) {
    using vas::Version;
    Version version(0, 0, 0);
    ASSERT_EQ(is_visiblemajor_(&version), false);
    ASSERT_EQ(is_visibleminor_(&version), false);
    ASSERT_EQ(is_visiblepatch_(&version), false);
}

TEST(oo_permissions_test, gva_base_inference_test) {
    GvaBaseInference gbi;
    ASSERT_EQ(is_visiblemodel(&gbi), true);
    ASSERT_EQ(is_visiblemodel_proc(&gbi), true);
    ASSERT_EQ(is_visibledevice(&gbi), true);
}

TEST(oo_permissions_test, audio_infer_impl_test) {
    GvaAudioBaseInference gabi;
    AudioInferImpl aii(&gabi);
    ASSERT_EQ(is_visibleaudioData(&aii), false);
    ASSERT_EQ(is_visibleinferenceStartTime(&aii), false);
    ASSERT_EQ(is_visiblestartTimeSet(&aii), false);
    ASSERT_EQ(is_visibleaudio_base_inference(&aii), false);
    ASSERT_EQ(is_visiblesliding_samples(&aii), false);
}

TEST(oo_permissions_test, safe_queue_test) {
    SafeQueue<std::shared_ptr<int>> sq;
    ASSERT_EQ(is_visiblequeue_(&sq), false);
    ASSERT_EQ(is_visiblemutex_(&sq), false);
    ASSERT_EQ(is_visiblecondition_(&sq), false);
}

TEST(oo_permissions_test, classification_history_test) {
    GstGvaClassify gva_classify;
    ClassificationHistory ch(&gva_classify);
    ASSERT_EQ(is_visiblegva_classify(&ch), false);
    ASSERT_EQ(is_visiblecurrent_num_frame(&ch), false);
    ASSERT_EQ(is_visiblehistory(&ch), false);
    ASSERT_EQ(is_visiblehistory_mutex(&ch), false);
}

TEST(oo_permissions_test, frame_wrapper_test) {
    using post_processing::FrameWrapper;
    GstBuffer b;
    GMutex *meta_mutex = new GMutex;
    g_mutex_init(meta_mutex);
    FrameWrapper fw(&b, "test", meta_mutex);
    ASSERT_EQ(is_visiblebuffer(&fw), true);
    ASSERT_EQ(is_visiblemodel_instance_id(&fw), true);
    ASSERT_EQ(is_visibleroi(&fw), true);
    ASSERT_EQ(is_visibleimage_transform_info(&fw), true);
    ASSERT_EQ(is_visiblewidth(&fw), true);
    ASSERT_EQ(is_visibleheight(&fw), true);
}

TEST(oo_permissions_test, hungarian_algo_test) {
    using vas::ot::HungarianAlgo;
    cv::Mat_<float> cost_map;
    HungarianAlgo ha(cost_map);
    ASSERT_EQ(is_visiblesize_width_(&ha), false);
    ASSERT_EQ(is_visiblesize_height_(&ha), false);
    ASSERT_EQ(is_visiblekHungarianNotAssigned(&ha), false);
    ASSERT_EQ(is_visiblekHungarianAssigned(&ha), false);
    ASSERT_EQ(is_visiblekIntMax(&ha), false);
    ASSERT_EQ(is_visibleint_cost_map_rows_(&ha), false);
    ASSERT_EQ(is_visibleint_cost_map_(&ha), false);
    ASSERT_EQ(is_visibleproblem_(&ha), false);
}

int main(int argc, char *argv[]) {
    std::cout << "Running Components::oo_permissions_test from " << __FILE__ << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
