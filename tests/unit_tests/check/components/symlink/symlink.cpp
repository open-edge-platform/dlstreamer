/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <regex>

#define private public
#define protected public

#include "base/gst_allocator_wrapper.h"
#include "base/inference_impl.h"
#include "test_utils.h"

TEST(symlink_test, model_file) {
    gchar allocator_name[] = "default";
    gchar device[] = "CPU";
    gchar model_name[] = "yolov8_license_plate_detector";

    char model_path[MAX_STR_PATH_SIZE] = {0};
    get_model_path(model_path, MAX_STR_PATH_SIZE, model_name, "FP32");

    std::string link = model_path;
    link = link.substr(0, link.find("xml"));
    std::string link_path = link + "symlink.xml";
    std::filesystem::remove(link_path);
    std::filesystem::create_directory_symlink(model_path, link_path);

    GvaBaseInference bi;
    bi.allocator_name = allocator_name;
    bi.device = device;
    bi.model = (char *)link_path.c_str();
    bi.model_proc = nullptr;
    bi.labels = nullptr;
    bi.custom_preproc_lib = nullptr;
    bi.custom_postproc_lib = nullptr;
    bi.ov_extension_lib = nullptr;

    try {
        InferenceImpl ii(&bi);
        std::filesystem::remove(link_path);
        throw std::runtime_error("Symbolic link to model file opened");
    } catch (const std::invalid_argument &e) {
        std::filesystem::remove(link_path);
        // propagate error if reason other than symbolic link detected
        if (std::string(e.what()).find("symbolic link") == std::string::npos)
            throw e;
    }
}

TEST(symlink_test, model_proc_file) {
    gchar allocator_name[] = "default";
    gchar device[] = "CPU";
    gchar model_name[] = "yolov8_license_plate_detector";

    char model_path[MAX_STR_PATH_SIZE] = {0};
    get_model_path(model_path, MAX_STR_PATH_SIZE, model_name, "FP32");

    std::string link = model_path;
    link = link.substr(0, link.find("xml"));
    std::string link_path = link + "symlink.xml";
    std::filesystem::remove(link_path);
    std::filesystem::create_directory_symlink(model_path, link_path);

    GvaBaseInference bi;
    bi.allocator_name = allocator_name;
    bi.device = device;
    bi.model = model_path;
    bi.model_proc = (char *)link_path.c_str();
    bi.labels = nullptr;
    bi.custom_preproc_lib = nullptr;
    bi.custom_postproc_lib = nullptr;
    bi.ov_extension_lib = nullptr;

    try {
        InferenceImpl ii(&bi);
        std::filesystem::remove(link_path);
        throw std::runtime_error("Symbolic link to model proc file opened");
    } catch (const std::invalid_argument &e) {
        std::filesystem::remove(link_path);
        // propagate error if reason other than symbolic link detected
        if (std::string(e.what()).find("symbolic link") == std::string::npos)
            throw e;
    }
}

TEST(symlink_test, labels_file) {
    gchar allocator_name[] = "default";
    gchar device[] = "CPU";
    gchar model_name[] = "yolov8_license_plate_detector";

    char model_path[MAX_STR_PATH_SIZE] = {0};
    get_model_path(model_path, MAX_STR_PATH_SIZE, model_name, "FP32");

    std::string link = model_path;
    link = link.substr(0, link.find("xml"));
    std::string link_path = link + "symlink.xml";
    std::filesystem::remove(link_path);
    std::filesystem::create_directory_symlink(model_path, link_path);

    GvaBaseInference bi;
    bi.allocator_name = allocator_name;
    bi.device = device;
    bi.model = model_path;
    bi.model_proc = nullptr;
    bi.labels = (char *)link_path.c_str();
    bi.custom_preproc_lib = nullptr;
    bi.custom_postproc_lib = nullptr;
    bi.ov_extension_lib = nullptr;

    try {
        InferenceImpl ii(&bi);
        std::filesystem::remove(link_path);
        throw std::runtime_error("Symbolic link to labels file opened");
    } catch (const std::invalid_argument &e) {
        std::filesystem::remove(link_path);
        // propagate error if reason other than symbolic link detected
        if (std::string(e.what()).find("symbolic link") == std::string::npos)
            throw e;
    }
}

int main(int argc, char *argv[]) {
    std::cout << "Running Components::symlink_test from " << __FILE__ << std::endl;
    try {
        testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    } catch (const std::exception &e) {
        std::cerr << "Caught std::exception in " << __FILE__ << " at line " << __LINE__ << " in function "
                  << __FUNCTION__ << std::endl;
        std::cerr << "Context: Failed during GoogleTest initialization: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Caught unknown exception in " << __FILE__ << " at line " << __LINE__ << " in function "
                  << __FUNCTION__ << std::endl;
        std::cerr << "Context: Failed during GoogleTest initialization with unknown exception type" << std::endl;
        return 1;
    }
}
