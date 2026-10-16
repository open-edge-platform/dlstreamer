/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iostream>

using namespace Utils;

struct UtilsTest : public testing::Test {
    const off_t FILE_SIZE = 10 * 1024 * 1024; // 10Mb

    void create_file(int offset = 0) {
        std::ofstream stream;
        stream.open("test_file");
        stream.seekp(FILE_SIZE - 1 + offset, std::ios_base::beg);
        stream << '\0';
        stream.close();
    }
};

// Tests for IsLinux() function
TEST_F(UtilsTest, UtilsTestIsLinuxWorking) {
    EXPECT_NO_THROW(IsLinux());
}

TEST_F(UtilsTest, UtilsTestIsLinuxWorkingCorrectly) {
#ifdef __linux__
    EXPECT_TRUE(IsLinux());
#else
    EXPECT_FALSE(IsLinux());
#endif
}

// Tests for GetFileSize() function
TEST_F(UtilsTest, UtilsTestGetFileSizeNoThrowBlankString) {
    EXPECT_THROW(GetFileSize(""), std::invalid_argument);
}

TEST_F(UtilsTest, UtilsTestFileSizeIsEqualToThresholdNoThrow) {
    create_file();
    EXPECT_NO_THROW(GetFileSize("./test_file"));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFileSizeIsEqualToThreshold) {
    create_file();
    off_t actual_size = GetFileSize("./test_file");
    EXPECT_TRUE(actual_size == FILE_SIZE);
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFileSizeIsLowerThenThresholdNoThrow) {
    create_file(-1024);
    EXPECT_NO_THROW(GetFileSize("./test_file"));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFileSizeIsLowerThenThreshold) {
    create_file(-1024);
    off_t actual_size = GetFileSize("./test_file");
    EXPECT_LT(actual_size, FILE_SIZE);
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFileSizeIsHigherThenThresholdThrow) {
    create_file(1024);
    EXPECT_NO_THROW(GetFileSize("./test_file"));
    remove("./test_file");
}

// Tests for CheckFileSize() function
TEST_F(UtilsTest, UtilsTestFCheckFileSizeBlankString) {
    EXPECT_THROW(CheckFileSize("", FILE_SIZE), std::invalid_argument);
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeEqualNoThrow) {
    create_file();
    EXPECT_NO_THROW(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeEqual) {
    create_file();
    EXPECT_TRUE(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeLessNoThrow) {
    create_file(-1024);
    EXPECT_NO_THROW(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeLess) {
    create_file(-1024);
    EXPECT_TRUE(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeHigherNoThrow) {
    create_file(1024);
    EXPECT_NO_THROW(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}

TEST_F(UtilsTest, UtilsTestFCheckFileSizeHigher) {
    create_file(1024);
    EXPECT_FALSE(CheckFileSize("test_file", FILE_SIZE));
    remove("./test_file");
}
