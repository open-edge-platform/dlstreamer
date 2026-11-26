/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "calibration.h"

namespace fs = std::filesystem;

namespace {

fs::path makeTempCalibFile(const std::string &name) {
    fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path);
    out << R"({"homography": [1,0,0, 0,1,0, 0,0,1]})";
    return path;
}

} // namespace

TEST(calibration_symlink, rejects_symlink) {
    fs::path real_file = makeTempCalibFile("calibration_symlink_test_real.json");
    fs::path link_path = fs::temp_directory_path() / "calibration_symlink_test_link.json";
    fs::remove(link_path);
    fs::create_symlink(real_file, link_path);

    dlstreamer::CalibrationStore store;
    const bool ok = store.load(link_path.string());
    const std::string error = store.last_error();

    fs::remove(link_path);
    fs::remove(real_file);

    EXPECT_FALSE(ok);
    EXPECT_NE(error.find("symbolic link"), std::string::npos) << "last_error() was: " << error;
}

TEST(calibration_symlink, accepts_regular_file) {
    fs::path real_file = makeTempCalibFile("calibration_symlink_test_regular.json");

    dlstreamer::CalibrationStore store;
    const bool ok = store.load(real_file.string());

    fs::remove(real_file);

    EXPECT_TRUE(ok) << "last_error() was: " << store.last_error();
}
