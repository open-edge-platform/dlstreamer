/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <openvino/genai/generation_config.hpp>
#include <openvino/genai/scheduler_config.hpp>

namespace genai {

/**
 * @brief Configuration parser for OpenVINO™ GenAI parameters
 *
 * The ConfigParser class provides static methods to parse configuration strings
 * for OpenVINO™ GenAI models. It supports parsing both generation configuration
 * and scheduler configuration from human-readable string formats.
 *
 * The parser accepts configuration in KEY=VALUE,KEY=VALUE format and converts
 * them to appropriate data structures. It handles type conversion and
 * validation.
 */
class ConfigParser {
  public:
    /**
     * @brief Parse generation config string in KEY=VALUE,KEY=VALUE format
     *
     * This method parses a comma-separated configuration string where each item
     * follows the KEY=VALUE format. It converts the parsed parameters into an
     * AnyMap containing generation properties.
     *
     * @param config_str Configuration string in "key1=value1,key2=value2" format
     * @return AnyMap with generation properties ready for model configuration
     * @throws std::runtime_error if parsing fails or invalid values are provided
     */
    static ov::AnyMap parse_generation_config_string(const std::string &config_str);

    /**
     * @brief Parse scheduler config string in KEY=VALUE,KEY=VALUE format
     *
     * This method parses a comma-separated configuration string for scheduler
     * configuration used in continuous batching scenarios.
     * It handles parameters related to batch processing, memory management,
     * and cache optimization.
     *
     * @param config_str Configuration string in "key1=value1,key2=value2" format
     * @return Optional SchedulerConfig object, nullopt if config_str is empty
     * @throws std::runtime_error if parsing fails or invalid values are provided
     */
    static std::optional<ov::genai::SchedulerConfig> parse_scheduler_config_string(const std::string &config_str);

    /**
     * @brief Parse pipeline (plugin/device) config string in KEY=VALUE,KEY=VALUE format
     *
     * This method parses a comma-separated configuration string into an AnyMap of string
     * values that is passed to the pipeline at construction as OpenVINO device/plugin
     * properties. Values are kept as strings and coerced by the plugin. Useful for device
     * tuning.
     *
     * @param config_str Configuration string in "key1=value1,key2=value2" format
     * @return AnyMap with plugin properties, empty if config_str is empty
     * @throws std::runtime_error if parsing fails
     */
    static ov::AnyMap parse_pipeline_config_string(const std::string &config_str);

  private:
    /**
     * @brief Trim whitespace and tabs from the beginning and end of a string
     * @param str Input string to trim
     * @return Trimmed string with leading and trailing whitespace removed
     */
    static std::string trim(const std::string &str);

    /**
     * @brief Convert a string property value to property and add to properties map
     * @tparam T Type of the property value
     * @param properties Properties map to add the converted property to
     * @param key Configuration key from input
     * @param value Configuration value from input as string
     * @param ov_prop_name Property name to match against
     * @param ov_prop Property object for type conversion
     */
    template <typename T>
    static void convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                             const std::string &ov_prop_name, ov::Property<T, ov::PropertyMutability::RW> ov_prop);

    /**
     * @brief Convert a string property value to set property and add to properties map
     * @tparam T Type of the set elements
     * @param properties Properties map to add the converted property to
     * @param key Configuration key from input
     * @param value Configuration value from input as semicolon-separated string
     * @param ov_prop_name Property name to match against
     * @param ov_prop Property object for set type conversion
     */
    template <typename T>
    static void convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                             const std::string &ov_prop_name,
                             ov::Property<std::set<T>, ov::PropertyMutability::RW> ov_prop);

    /**
     * @brief Convert a string property value to StopCriteria property and add to properties map
     * @param properties Properties map to add the converted property to
     * @param key Configuration key from input
     * @param value Configuration value from input as string (EARLY, HEURISTIC, NEVER)
     * @param ov_prop_name Property name to match against
     * @param ov_prop StopCriteria property object
     */
    static void convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                             const std::string &ov_prop_name,
                             ov::Property<ov::genai::StopCriteria, ov::PropertyMutability::RW> ov_prop);

    /**
     * @brief Convert a configuration map to properties map
     * @param config_map Map of configuration key-value pairs
     * @return AnyMap with converted properties
     */
    static ov::AnyMap convert_to_properties(const std::map<std::string, std::string> &config_map);
};

} // namespace genai
