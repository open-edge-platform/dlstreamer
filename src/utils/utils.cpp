/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "utils.h"

#include <safe_arithmetic.hpp>

#ifdef __linux__
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cassert>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string.h>
#include <tuple>

#include "inference_backend/image.h" // FOURCC enum

namespace Utils {

std::string createNestedErrorMsg(const std::exception &e, std::string &&msg, int level) {
    if (not msg.empty())
        ++level;

    msg += "\n" + std::string(level, '\t') + e.what();

    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &e) {
        msg = createNestedErrorMsg(e, std::move(msg), level);
    }
    return msg;
}

std::vector<std::string> splitString(const std::string &input, char delimiter) {
    std::vector<std::string> tokens;
    splitString(input, std::back_inserter(tokens), delimiter);
    return tokens;
}

void parseFilterConfig(const std::string &config_str, std::unordered_set<std::string> &filtered_set, char delimiter) {
    auto labels = splitString(config_str, delimiter);
    for (const auto &label : labels) {
        if (filtered_set.count(label) == 0) {
            filtered_set.insert(label);
        }
    }
}

std::map<std::string, std::string> stringToMap(const std::string &s, char rec_delim /*= ','*/,
                                               char kv_delim /*= '='*/) {
    std::string key, val;
    std::istringstream iss(s);
    std::map<std::string, std::string> m;

    while (std::getline(std::getline(iss, key, kv_delim) >> std::ws, val, rec_delim)) {
        m.emplace(std::move(key), std::move(val));
    }

    return m;
}

bool fileExists(const std::string &path) {
    return std::ifstream(path).good();
}

bool symLink(const std::string &path) {
    return std::filesystem::is_symlink(path);
}

size_t GetFileSize(const std::string &file_path) {
#ifdef __linux__
    struct stat stat_buffer;
    int got_stat = stat(file_path.c_str(), &stat_buffer);
    if (got_stat != 0) // error while reading file information
        throw std::invalid_argument("Error while reading file '" + file_path + "' information.");

    return safe_convert<size_t>(stat_buffer.st_size);
#else
    return std::filesystem::file_size(file_path);
#endif
}

bool CheckFileSize(const std::string &path, size_t size_threshold) {
    auto file_size = GetFileSize(path);
    return file_size <= size_threshold;
}

uint32_t getRelativeGpuDeviceIndex(const std::string &device) {
    if (device.find("GPU") == std::string::npos)
        throw std::invalid_argument("Invalid GPU device name: " + device);

    const std::vector<std::string> tokens = Utils::splitString(device, '.');
    if (tokens.size() <= 1 || tokens[1] == "x")
        return 0;

    for (auto ch : tokens[1]) {
        if (!std::isdigit(ch)) {
            throw std::invalid_argument("Invalid GPU device name: " + device);
        }
    }

    return std::stoul(tokens[1]);
}

bool strToBool(const std::string &s) {
    std::istringstream iss(s);
    bool ans;
    iss >> ans;
    if (iss.fail()) {
        // Try bool
        iss.clear();
        iss >> std::boolalpha >> ans;
    }

    if (iss.fail())
        throw std::invalid_argument(s + " cannot be converted to boolean");

    return ans;
}

uint32_t GetPlanesCount(int fourcc) noexcept {
    using namespace InferenceBackend;
    switch (fourcc) {
    case FOURCC_BGRA:
    case FOURCC_BGRX:
    case FOURCC_BGR:
    case FOURCC_RGBA:
    case FOURCC_RGBX:
        return 1;
    case FOURCC_NV12:
        return 2;
    case FOURCC_BGRP:
    case FOURCC_RGBP:
    case FOURCC_I420:
        return 3;
    }

    return 0;
}

uint32_t GetChannelsCount(int fourcc) noexcept {
    using namespace InferenceBackend;
    switch (fourcc) {
    case FOURCC_BGRA:
    case FOURCC_BGRX:
    case FOURCC_RGBA:
    case FOURCC_RGBX:
        return 4;
    case FOURCC_BGR:
        return 3;
    case FOURCC_BGRP:
    case FOURCC_RGBP:
    case FOURCC_I420:
    case FOURCC_NV12:
        return 1;
    }

    return 0;
}

bool checkAllKeysAreKnown(const std::set<std::string> &known_keys, const std::map<std::string, std::string> &config) {
    auto is_valid_key = [&](const std::pair<std::string, std::string> &p) { return known_keys.count(p.first); };
    return std::all_of(std::begin(config), std::end(config), is_valid_key);
}

std::string fixPath(std::string path) {
    if (path.empty())
        return path;

    // trim left
    path.erase(path.begin(), std::find_if(path.begin(), path.end(), [](char ch) { return !std::isspace(ch); }));
    // trim right
    path.erase(std::find_if(path.rbegin(), path.rend(), [](char ch) { return !std::isspace(ch); }).base(), path.end());

    // replace leading tilde
    if (path.front() == '~')
        return std::getenv("HOME") + path.substr(1);

    return path;
}

} // namespace Utils
