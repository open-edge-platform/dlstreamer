/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "config.h"
#include <inference_backend/buffer_mapper.h>
#include <mapped_mat.h>

extern "C" {
#include <pipeline_test_common.h>
#include <test_utils.h>
}

#include <gst/check/gstcheck.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <stdio.h>
#include <unistd.h>

struct TestData {
    TestData(const std::string &caps) : MCaps(gst_caps_from_string(caps.c_str())), MVideoInfo(gst_video_info_new()) {
        gst_video_info_from_caps(MVideoInfo, MCaps);
        MMapper = BufferMapperFactory::createMapper(InferenceBackend::MemoryType::SYSTEM);
    }

    ~TestData() {
        gst_video_info_free(MVideoInfo);
        gst_caps_unref(MCaps);
    }

    GstCaps *MCaps;
    GstVideoInfo *MVideoInfo;
    std::shared_ptr<dlstreamer::MemoryMapper> MMapper;
    bool MPerformedCopying = false;
};

// FIXME. This check works fine on system memory. Probably it won't work for other memory types.
void check_data_copying_callback(GstBuffer *app_buffer, gpointer test_data) {
    TestData *data = static_cast<TestData *>(test_data);

    auto gst_buf = std::make_shared<dlstreamer::GSTFrame>(app_buffer, data->MVideoInfo);

    auto sys_buf = data->MMapper->map(gst_buf, dlstreamer::AccessMode::Read);
    unsigned char *buffer_ptr = static_cast<unsigned char *>(sys_buf->tensor(0)->data());

    {
        MappedMat mat(sys_buf);
        // If pointers are not the same, copying were performed
        data->MPerformedCopying = buffer_ptr != mat.mat().data;
    }
}

std::string generate_file_name() {
    char cwd[MAX_STR_PATH_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        ck_assert(!"Unable to generate file name");
    }
    return std::string(cwd) + "/bad_resolution.mp4";
}

void generate_video_with_caps(const std::string &caps, const std::string &file_name) {
    char pipeline_gen[MAX_STR_PATH_SIZE];
    sprintf(pipeline_gen,
            "videotestsrc num-buffers=3 pattern=colors ! %s ! vapostproc ! vah264enc ! h264parse ! qtmux ! "
            "filesink location=%s",
            caps.c_str(), file_name.c_str());
    check_run_pipeline(pipeline_gen, GST_CLOCK_TIME_NONE);
}

void delete_generated_video(const std::string &file_name) {
    if (remove(file_name.c_str())) {
        ck_assert(!"Unable to delete generated file");
    }
}

bool check_data_copying(const std::string &decoder_string, const std::string &caps, const std::string &file_name) {
    char model_path[MAX_STR_PATH_SIZE];
    get_model_path(model_path, MAX_STR_PATH_SIZE, "face-detection-adas-0001", "FP32");

    const char *appsink_name = "appsink1";
    char pipeline_str[8 * MAX_STR_PATH_SIZE];
    sprintf(pipeline_str, "filesrc location=%s ! %s ! gvadetect model=%s device=CPU ! appsink sync=false name=%s",
            file_name.c_str(), decoder_string.c_str(), model_path, appsink_name);

    TestData test_data(caps);
    test_data.MPerformedCopying = true;

    AppsinkTestData appsink_test_data = {
        .check_buf_cb = check_data_copying_callback, .frame_count_limit = 500, .user_data = &test_data};

    check_run_pipeline_with_appsink_default(pipeline_str, GST_CLOCK_TIME_NONE, &appsink_name, 1, &appsink_test_data);

    return test_data.MPerformedCopying;
}

GST_START_TEST(test_avdec_decoder_perform_copy) {
    const auto caps = "video/x-raw,format=I420,width=768,height=432";
    auto file_name = generate_file_name();
    generate_video_with_caps(caps, file_name);
    bool copied_data = check_data_copying("qtdemux ! h264parse ! avdec_h264 ! videoconvert", caps, file_name);
    ck_assert(copied_data && "DID expected copying of data!");
    delete_generated_video(file_name);
}
GST_END_TEST;

GST_START_TEST(test_avdec_decoder_perform_no_copy) {
    const auto caps = "video/x-raw,format=I420,width=768,height=432";
    auto file_name = generate_file_name();
    generate_video_with_caps(caps, file_name);
    bool copied_data = check_data_copying("qtdemux ! h264parse ! avdec_h264 ! videoconvert ! video/x-raw,format=BGRx",
                                          "video/x-raw,format=BGRx,width=768,height=432", file_name);
    ck_assert(!copied_data && "DID NOT expect copying of data!");
    delete_generated_video(file_name);
}
GST_END_TEST;

GST_START_TEST(test_avdec_decoder_perform_copy_nv12) {
    const auto caps = "video/x-raw,format=I420,width=768,height=432";
    auto file_name = generate_file_name();
    generate_video_with_caps(caps, file_name);
    bool copied_data = check_data_copying("qtdemux ! h264parse ! vah264dec ! videoconvert ! video/x-raw",
                                          "video/x-raw,format=NV12,width=768,height=432", file_name);
    ck_assert(copied_data && "DID expected copying of data!");
    delete_generated_video(file_name);
}
GST_END_TEST;

static Suite *gvatrack_test_suite(void) {
    Suite *s = suite_create("pipeline_gvatrack_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_avdec_decoder_perform_copy);
    tcase_add_test(tc_chain, test_avdec_decoder_perform_no_copy);
    tcase_add_test(tc_chain, test_avdec_decoder_perform_copy_nv12);

    return s;
}

GST_CHECK_MAIN(gvatrack_test);
