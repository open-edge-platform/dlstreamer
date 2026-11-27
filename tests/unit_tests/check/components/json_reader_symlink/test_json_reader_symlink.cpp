/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "json_reader.h"

namespace fs = std::filesystem;

TEST(json_reader_symlink, rejects_symlink) {
    fs::path real_file = fs::temp_directory_path() / "json_reader_symlink_test_real.json";
    std::ofstream(real_file) << "{}";
    fs::path link_path = fs::temp_directory_path() / "json_reader_symlink_test_link.json";
    fs::remove(link_path);
    fs::create_symlink(real_file, link_path);

    JsonReader reader;
    bool threw = false;
    try {
        reader.read(link_path.string());
    } catch (const std::invalid_argument &e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("symbolic link"), std::string::npos) << e.what();
    }

    fs::remove(link_path);
    fs::remove(real_file);

    EXPECT_TRUE(threw);
}

TEST(json_reader_symlink, accepts_regular_file) {
    fs::path real_file = fs::temp_directory_path() / "json_reader_symlink_test_regular.json";
    std::ofstream(real_file) << R"({"key": "value"})";

    JsonReader reader;
    EXPECT_NO_THROW(reader.read(real_file.string()));

    fs::remove(real_file);
}

GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
