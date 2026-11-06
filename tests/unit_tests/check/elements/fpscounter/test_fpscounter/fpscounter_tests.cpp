/*******************************************************************************
 * Copyright (C) 2020-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "fpscounter.h"
#include "fpscounter_c.h"
#include "gva_utils.h"
#include <fstream>
#include <gmock/gmock.h>
#include <gst/gstmeta.h>
#include <gtest/gtest.h>
#include <test_common.h>
#include <test_utils.h>

struct FpsCounterTest : public ::testing::Test {
    FILE *getTempFile() {
        return fopen("fpscounter_test.txt", "w+");
    }
};

TEST_F(FpsCounterTest, IterativeFpsCounter_positive) {
    IterativeFpsCounter counter(0, 1, false, false, false);
    // add 3 frames per second
    FILE *tmpFile = getTempFile();
    ASSERT_TRUE(tmpFile != nullptr);
    for (size_t i = 0; i < 3; i++) {
        counter.NewFrame("test1", tmpFile, nullptr);
        counter.NewFrame("test1", tmpFile, nullptr);
        usleep(1000000);
        counter.NewFrame("test2", tmpFile, nullptr);
    }

    counter.EOS("test1", tmpFile);

    fseek(tmpFile, 0, SEEK_SET);
    for (size_t i = 0; i < 3; i++) {
        // read data from tempFile
        float fps = -1.0f;
        int numStream = 0;
        float fpsPerStream = -1.0f;
        float fps1 = -1.0f;
        float fps2 = -1.0f;
        float nsec = -1;
        EXPECT_EQ(fscanf(tmpFile,
                         "FpsCounter(last %fsec): total=%f fps, number-streams=%d, per-stream=%f fps (%f, %f)\n", &nsec,
                         &fps, &numStream, &fpsPerStream, &fps1, &fps2),
                  6);
        // validate
        EXPECT_NEAR(fps, 3.0f, 0.02f); // (2 + 1 frames) / 1000ms
        EXPECT_EQ(numStream, 2);
        EXPECT_NEAR(fpsPerStream, 1.5f, 0.02f); // (2 + 1 frames) / (2 streams * 1000ms)
    }
    fclose(tmpFile);
}

TEST_F(FpsCounterTest, IterativeFpsCounter_NewFrameNegative) {
    IterativeFpsCounter counter(0, 1, false, false, false);
    EXPECT_FALSE(counter.NewFrame("test1", nullptr, nullptr)); // false - file is null
}

TEST_F(FpsCounterTest, FpsCounters_C_interface_positive) {
    FILE *tmpFile = getTempFile();
    ASSERT_TRUE(tmpFile != nullptr);
    fps_counter_set_output(tmpFile);
    fps_counter_create_iterative("1,2", false, false);

    for (size_t i = 0; i < 2; i++) {
        fps_counter_new_frame(nullptr, "test1", nullptr);
        usleep(1000000);
        fps_counter_new_frame(nullptr, "test2", nullptr);
    }
    fps_counter_eos("test1");

    fseek(tmpFile, 0, SEEK_SET);
    for (size_t i = 0; i < 3; i++) {
        // read data from tempFile
        float fps = -1.0f;
        int numStream = 0;
        float fpsPerStream = -1.0f;
        float nsec = -1;
        float dummyF = -1.0f;
        // 1th & 2th rows - iterative 1 sec
        // 3th row - iterative 2 sec
        EXPECT_EQ(fscanf(tmpFile,
                         "FpsCounter(last %fsec): total=%f fps, number-streams=%d, per-stream=%f fps (%f, %f)\n", &nsec,
                         &fps, &numStream, &fpsPerStream, &dummyF, &dummyF),
                  6);
        // validate
        EXPECT_NEAR(fps, 2.0f, 0.02f); // total fps = (2 frames / 1000ms) or (4 frames / 2000ms)
        EXPECT_EQ(numStream, 2);
        EXPECT_NEAR(fpsPerStream, 1.0f, 0.02f); // per stream = total fps / 2 streams
    }
    fclose(tmpFile);

    fps_counter_set_output(stdout);
}

TEST_F(FpsCounterTest, FpsCounters_C_interface_silent) {
    FILE *tmpFile = getTempFile();
    ASSERT_TRUE(tmpFile != nullptr);
    fps_counter_set_output(tmpFile);
    fps_counter_create_iterative("1", false, false);

    // nothing is expected to be written while the counter is silenced
    fps_counter_set_silent(true);
    fps_counter_new_frame(nullptr, "test1", nullptr);
    usleep(1000000);
    fps_counter_new_frame(nullptr, "test1", nullptr);
    fps_counter_eos("test1");

    fseek(tmpFile, 0, SEEK_END);
    EXPECT_EQ(ftell(tmpFile), 0);

    // changing the output while silenced must not break the silence
    fps_counter_set_output(tmpFile);
    fps_counter_new_frame(nullptr, "test1", nullptr);
    usleep(1000000);
    fps_counter_new_frame(nullptr, "test1", nullptr);

    fseek(tmpFile, 0, SEEK_END);
    EXPECT_EQ(ftell(tmpFile), 0);

    // once unsilenced, the configured output is used again
    fps_counter_set_silent(false);
    fps_counter_new_frame(nullptr, "test1", nullptr);
    usleep(1000000);
    fps_counter_new_frame(nullptr, "test1", nullptr);

    fseek(tmpFile, 0, SEEK_END);
    EXPECT_GT(ftell(tmpFile), 0);

    fclose(tmpFile);

    fps_counter_set_output(stdout);
}

GTEST_API_ int main(int argc, char **argv) {
    std::cout << "Running Components::FpsCounterTest from " << __FILE__ << std::endl;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
