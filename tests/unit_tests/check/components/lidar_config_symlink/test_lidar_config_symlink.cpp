/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "lidar_config.hpp"

namespace fs = std::filesystem;

namespace {

fs::path makeTempLidarConfig(const std::string &name) {
    fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path);
    out << R"({
        "vendor": "robosense",
        "model": "RSE1",
        "transport": {"type": "udp", "bind_address": "0.0.0.0"}
    })";
    return path;
}

} // namespace

TEST(lidar_config_symlink, rejects_symlink) {
    fs::path real_file = makeTempLidarConfig("lidar_config_symlink_test_real.json");
    fs::path link_path = fs::temp_directory_path() / "lidar_config_symlink_test_link.json";
    fs::remove(link_path);
    fs::create_symlink(real_file, link_path);

    bool threw = false;
    try {
        g3dlidar::LidarConfig::from_file(link_path.string());
    } catch (const std::invalid_argument &e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("symbolic link"), std::string::npos) << e.what();
    }

    fs::remove(link_path);
    fs::remove(real_file);

    EXPECT_TRUE(threw);
}

TEST(lidar_config_symlink, accepts_regular_file) {
    fs::path real_file = makeTempLidarConfig("lidar_config_symlink_test_regular.json");

    const auto cfg = g3dlidar::LidarConfig::from_file(real_file.string());

    fs::remove(real_file);

    EXPECT_EQ(cfg.vendor, "robosense");
}

GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
