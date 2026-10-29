/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Prints the OpenVINO devices available to DL Streamer as JSON.
//
// Python tools (e.g. the pipeline optimizer) use this instead of the OpenVINO
// Python bindings, so the DL Streamer Python runtime does not depend on the
// Python ABI of the OpenVINO bindings, and the very same OpenVINO runtime that
// DL Streamer elements are linked against is queried.

#include <openvino/openvino.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace {

void print_usage(const char *program) {
    std::cout << "Usage: " << program << " [--help]\n"
              << "\n"
              << "Prints the OpenVINO devices available to DL Streamer as JSON:\n"
              << "  {\"openvino_version\": \"...\", \"devices\": [\n"
              << "    {\"name\": \"GPU.0\", \"full_name\": \"...\", \"type\": \"integrated|discrete\"}, ...]}\n";
}

std::string device_type(const ov::Core &core, const std::string &device) {
    try {
        return core.get_property(device, ov::device::type) == ov::device::Type::DISCRETE ? "discrete" : "integrated";
    } catch (const std::exception &) {
        // CPU and some plugins do not report DEVICE_TYPE
        return "integrated";
    }
}

std::string full_device_name(const ov::Core &core, const std::string &device) {
    try {
        return core.get_property(device, ov::device::full_name);
    } catch (const std::exception &) {
        return "";
    }
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        std::cerr << "Unknown argument: " << argv[i] << "\n";
        print_usage(argv[0]);
        return 2;
    }

    try {
        ov::Core core;
        nlohmann::json devices = nlohmann::json::array();
        for (const auto &device : core.get_available_devices()) {
            devices.push_back({
                {"name", device},
                {"full_name", full_device_name(core, device)},
                {"type", device_type(core, device)},
            });
        }

        const nlohmann::json result = {
            {"openvino_version", ov::get_openvino_version().buildNumber},
            {"devices", devices},
        };
        std::cout << result.dump(2) << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to query OpenVINO devices: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
