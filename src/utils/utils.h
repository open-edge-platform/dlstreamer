/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#define UNUSED(x) ((void)(x))

#ifdef __cplusplus

#include <cstdint>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _MSC_VER
#define PRETTY_FUNCTION_NAME __FUNCSIG__
#else
#define PRETTY_FUNCTION_NAME __PRETTY_FUNCTION__
#endif

namespace Utils {

const std::string dpcppInstructionMsg = "Seems DPC++ dependency is not installed. Please follow installation guide: "
                                        "https://dlstreamer.github.io/get_started/install/"
                                        "install_guide_ubuntu.html#step-7-install-intel-oneapi-dpc-c-compiler-optional";

std::string createNestedErrorMsg(const std::exception &e, std::string &&msg = "", int level = 0);

template <typename OutputIter>
void splitString(const std::string &input, OutputIter out_iter, char delimiter = ',') {
    std::string token;
    std::istringstream tokenStream(input);
    while (std::getline(tokenStream, token, delimiter)) {
        *out_iter++ = token;
    }
}

std::vector<std::string> splitString(const std::string &input, char delimiter = ',');

template <typename Iter>
std::string join(Iter begin, Iter end, char delimiter = ',') {
    std::ostringstream result;
    for (auto iter = begin; iter != end; iter++) {
        if (iter == begin)
            result << *iter;
        else
            result << delimiter << *iter;
    }
    return result.str();
}

/**
 * Parses a label filter configuration string in the format `label1:label2:label3:...`.
 * Labels values are added to the output set for filtering.
 *
 * @param config_str The configuration string to parse (e.g., "person:car:bike").
 * @param filtered_set Output set that will contain only the relevant labels.
 * @param delimiter The delimiter used to separate label entries (default is ':').
 */
void parseFilterConfig(const std::string &config_str, std::unordered_set<std::string> &filtered_set,
                       char delimiter = ':');

/**
 * Converts string in format `key1=val1,key2=val2,...` to key/value pairs.
 *
 * @param[in] s string to convert.
 * @param[in] rec_delim delimiter to use between records (by default - `,`).
 * @param[in] kv_delim delimiter to use between key and value (by default - `=`).
 *
 * @return map container with key/value pairs of strings.
 */
std::map<std::string, std::string> stringToMap(const std::string &s, char rec_delim = ',', char kv_delim = '=');
bool fileExists(const std::string &path);
bool symLink(const std::string &path);
constexpr bool IsLinux() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}
size_t GetFileSize(const std::string &file_path);
bool CheckFileSize(const std::string &path, size_t size_threshold);

/**
 * Parses GPU device name and returns relative GPU index from it.
 *
 * @param[in] device GPU device name in forms: GPU, GPU.0, etc.
 *
 * @return relative GPU index.
 */
uint32_t getRelativeGpuDeviceIndex(const std::string &device);

/**
 * Converts string to boolean value.
 * Valid values are: 1, 0, true, false
 *
 * @param s string to convert.
 * @return boolean value.
 * @exception std::invalid_argument if no conversion can be performed.
 */
bool strToBool(const std::string &s);

/**
 * Returns number of planes for specified format.
 *
 * @param fourcc data format as FourCC value.
 * @return number of planes.
 */
uint32_t GetPlanesCount(int fourcc) noexcept;

/**
 * @brief Returns number of channels for specified format
 *
 * @param fourcc data format as FourCC value
 * @return number of channels
 */
uint32_t GetChannelsCount(int fourcc) noexcept;

/**
 * Checks if all keys in config are known.
 *
 * @param known_keys set of known keys.
 * @param config configuration map whose keys should be checked.
 * @return true if all config keys are from the set of known keys.
 */
bool checkAllKeysAreKnown(const std::set<std::string> &known_keys, const std::map<std::string, std::string> &config);

/*
 * @brief Trims path and replaces leading ~ symbol with the HOME path
 *
 * @param path
 * @return correct absolute path
 */
std::string fixPath(std::string path);

} // namespace Utils

#endif
