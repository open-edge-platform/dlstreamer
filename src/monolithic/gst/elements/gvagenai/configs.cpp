/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "configs.hpp"

#include <gst/gst.h>
GST_DEBUG_CATEGORY_EXTERN(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

namespace genai {

std::string ConfigParser::trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

bool ConfigParser::to_bool(const std::string &value, const std::string &key) {
    // Reuse OpenVINO's string->bool conversion.
    try {
        return ov::Any(value).as<bool>();
    } catch (const std::exception &e) {
        throw std::runtime_error("Cannot convert value '" + value + "' to boolean for property '" + key +
                                 "'. Accepted forms: true/false, 1/0, yes/no, on/off (case-insensitive)");
    }
}

template <typename T>
bool ConfigParser::convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                                const std::string &ov_prop_name, ov::Property<T, ov::PropertyMutability::RW> ov_prop) {
    if (ov_prop_name != key)
        return false;
    T ov_val;
    if constexpr (std::is_same_v<T, bool>) {
        ov_val = to_bool(value, key);
    } else {
        std::stringstream ss(value);
        ss >> ov_val;
        if (ss.fail()) {
            throw std::runtime_error("Cannot convert value '" + value + "' to expected type for property '" + key +
                                     "'");
        }
    }
    properties.emplace(ov_prop(ov_val));
    GST_INFO("Set generation config: %s = %s", key.c_str(), value.c_str());
    return true;
}

template <typename T>
bool ConfigParser::convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                                const std::string &ov_prop_name,
                                ov::Property<std::set<T>, ov::PropertyMutability::RW> ov_prop) {
    if (ov_prop_name != key)
        return false;
    std::set<T> items_set;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ';')) { // Use semicolon as separator
        item = trim(item);
        if (!item.empty()) {
            if constexpr (std::is_same_v<T, std::string>) {
                items_set.insert(item);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                try {
                    size_t idx = 0;
                    int64_t token_id = std::stoll(item, &idx);
                    if (idx != item.size()) // reject trailing non-numeric characters (e.g. "1abc")
                        throw std::invalid_argument("trailing characters");
                    items_set.insert(token_id);
                } catch (const std::exception &e) {
                    throw std::runtime_error("Invalid token ID '" + item + "' in property '" + key + "': " + e.what());
                }
            } else {
                throw std::runtime_error("Unsupported type for set property '" + key +
                                         "': " + std::string(typeid(T).name()));
            }
        }
    }
    properties.emplace(ov_prop(items_set));
    GST_INFO("Set generation config: %s with %zu items", ov_prop_name.c_str(), items_set.size());
    return true;
}

bool ConfigParser::convert_prop(ov::AnyMap &properties, const std::string &key, const std::string &value,
                                const std::string &ov_prop_name,
                                ov::Property<ov::genai::StopCriteria, ov::PropertyMutability::RW> ov_prop) {
    if (ov_prop_name != key)
        return false;
    ov::genai::StopCriteria criteria;
    if (value == "EARLY") {
        criteria = ov::genai::StopCriteria::EARLY;
    } else if (value == "HEURISTIC") {
        criteria = ov::genai::StopCriteria::HEURISTIC;
    } else if (value == "NEVER") {
        criteria = ov::genai::StopCriteria::NEVER;
    } else {
        throw std::runtime_error("Invalid stop_criteria value '" + value +
                                 "'. Valid values are: EARLY, HEURISTIC, NEVER");
    }
    properties.emplace(ov_prop(criteria));
    GST_INFO("Set generation config: %s = %s", ov_prop_name.c_str(), value.c_str());
    return true;
}

ov::AnyMap ConfigParser::convert_to_properties(const std::map<std::string, std::string> &config_map) {
    ov::AnyMap properties;

    for (const auto &pair : config_map) {
        const std::string &k = pair.first;
        const std::string &v = pair.second;

        // Each convert_prop returns true only when the key matches its property name. OR the
        // results so an unknown key can be rejected below.
        bool matched = false;

        // Generic parameters
        matched |= convert_prop(properties, k, v, "max_new_tokens", ov::genai::max_new_tokens);
        matched |= convert_prop(properties, k, v, "max_length", ov::genai::max_length);
        matched |= convert_prop(properties, k, v, "ignore_eos", ov::genai::ignore_eos);
        matched |= convert_prop(properties, k, v, "min_new_tokens", ov::genai::min_new_tokens);

        // EOS and stop parameters
        matched |= convert_prop(properties, k, v, "eos_token_id", ov::genai::eos_token_id);
        matched |= convert_prop(properties, k, v, "stop_strings", ov::genai::stop_strings);
        matched |= convert_prop(properties, k, v, "include_stop_str_in_output", ov::genai::include_stop_str_in_output);
        matched |= convert_prop(properties, k, v, "stop_token_ids", ov::genai::stop_token_ids);

        // Penalties
        matched |= convert_prop(properties, k, v, "repetition_penalty", ov::genai::repetition_penalty);
        matched |= convert_prop(properties, k, v, "presence_penalty", ov::genai::presence_penalty);
        matched |= convert_prop(properties, k, v, "frequency_penalty", ov::genai::frequency_penalty);

        // Beam search specific parameters
        matched |= convert_prop(properties, k, v, "num_beams", ov::genai::num_beams);
        matched |= convert_prop(properties, k, v, "num_beam_groups", ov::genai::num_beam_groups);
        matched |= convert_prop(properties, k, v, "diversity_penalty", ov::genai::diversity_penalty);
        matched |= convert_prop(properties, k, v, "length_penalty", ov::genai::length_penalty);
        matched |= convert_prop(properties, k, v, "num_return_sequences", ov::genai::num_return_sequences);
        matched |= convert_prop(properties, k, v, "no_repeat_ngram_size", ov::genai::no_repeat_ngram_size);
        matched |= convert_prop(properties, k, v, "stop_criteria", ov::genai::stop_criteria);

        // Random sampling parameters
        matched |= convert_prop(properties, k, v, "do_sample", ov::genai::do_sample);
        matched |= convert_prop(properties, k, v, "temperature", ov::genai::temperature);
        matched |= convert_prop(properties, k, v, "top_p", ov::genai::top_p);
        matched |= convert_prop(properties, k, v, "top_k", ov::genai::top_k);
        matched |= convert_prop(properties, k, v, "min_p", ov::genai::min_p);
        matched |= convert_prop(properties, k, v, "rng_seed", ov::genai::rng_seed);

        // CDPruner parameters: visual token pruning for VLMs
        matched |= convert_prop(properties, k, v, "pruning_ratio", ov::genai::pruning_ratio);
        matched |= convert_prop(properties, k, v, "relevance_weight", ov::genai::relevance_weight);

        // Assisting generation parameters
        matched |=
            convert_prop(properties, k, v, "assistant_confidence_threshold", ov::genai::assistant_confidence_threshold);
        matched |= convert_prop(properties, k, v, "num_assistant_tokens", ov::genai::num_assistant_tokens);
        matched |= convert_prop(properties, k, v, "max_ngram_size", ov::genai::max_ngram_size);

        // Other parameters
        matched |= convert_prop(properties, k, v, "apply_chat_template", ov::genai::apply_chat_template);

        if (!matched) {
            throw std::runtime_error("Unknown generation config key: '" + k + "'");
        }
    }

    return properties;
}

ov::AnyMap ConfigParser::parse_generation_config_string(const std::string &config_str) {
    // Return empty map if no configuration provided
    if (config_str.empty()) {
        return ov::AnyMap();
    }

    // Parse KEY=VALUE,KEY=VALUE format
    std::map<std::string, std::string> config_map;

    std::istringstream ss(config_str);
    std::string pair;
    while (std::getline(ss, pair, ',')) {
        pair = trim(pair);

        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(pair.substr(0, pos));
            std::string value = trim(pair.substr(pos + 1));

            if (!key.empty()) {
                config_map[key] = value;
            } else {
                throw std::runtime_error("Empty key in generation config: '" + pair + "'");
            }
        } else if (!pair.empty()) {
            throw std::runtime_error("Invalid generation config format, expected KEY=VALUE: '" + pair + "'");
        }
    }
    return ConfigParser::convert_to_properties(config_map);
}

ov::AnyMap ConfigParser::parse_pipeline_config_string(const std::string &config_str) {
    // Return empty map if no configuration provided
    if (config_str.empty()) {
        return ov::AnyMap();
    }

    // Parse KEY=VALUE,KEY=VALUE format into an AnyMap. Most values are kept as strings; the
    // OpenVINO runtime coerces them to the expected type via ov::Any. A few NPU KV-cache
    // properties are the exception: GenAI extracts them with pop_int_and_cast(), which requires
    // an int64_t/int.
    static const std::set<std::string> integer_keys = {"MAX_PROMPT_LEN", "MIN_RESPONSE_LEN"};
    auto make_any = [](const std::string &prop, const std::string &val) -> ov::Any {
        if (integer_keys.count(prop)) {
            try {
                size_t idx = 0;
                int64_t parsed = std::stoll(val, &idx);
                if (idx != val.size()) // reject trailing non-numeric characters (e.g. "2048abc")
                    throw std::invalid_argument("trailing characters");
                return ov::Any(parsed);
            } catch (const std::exception &) {
                throw std::runtime_error("Pipeline config property '" + prop + "' requires an integer value, got '" +
                                         val + "'");
            }
        }
        return ov::Any(val);
    };

    // A key of the form DEVICE.PROP nests PROP under that device's property block. GenAI's
    // VLM pipeline reads the "DEVICE_PROPERTIES" key and then indexes it by device name.
    // Un-dotted keys remain top-level.
    ov::AnyMap properties;
    // Per-device nested property blocks, keyed by device name (e.g. "NPU").
    std::map<std::string, ov::AnyMap> device_properties;

    std::istringstream ss(config_str);
    std::string pair;
    while (std::getline(ss, pair, ',')) {
        pair = trim(pair);
        if (pair.empty())
            continue;

        size_t pos = pair.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid pipeline config format, expected KEY=VALUE: '" + pair + "'");
        }

        std::string key = trim(pair.substr(0, pos));
        std::string value = trim(pair.substr(pos + 1));
        if (key.empty()) {
            throw std::runtime_error("Empty key in pipeline config: '" + pair + "'");
        }

        // DEVICE.PROP -> nested device property block
        size_t dot = key.find('.');
        if (dot != std::string::npos) {
            std::string device = trim(key.substr(0, dot));
            std::string prop = trim(key.substr(dot + 1));
            if (device.empty() || prop.empty()) {
                throw std::runtime_error("Invalid nested pipeline config key, expected DEVICE.PROP: '" + key + "'");
            }
            device_properties[device].emplace(prop, make_any(prop, value));
            GST_INFO("Set pipeline config: %s.%s = %s", device.c_str(), prop.c_str(), value.c_str());
            continue;
        }

        properties.emplace(key, make_any(key, value));
        GST_INFO("Set pipeline config: %s = %s", key.c_str(), value.c_str());
    }

    // Fold all per-device blocks into a single nested DEVICE_PROPERTIES map:
    // {"DEVICE_PROPERTIES": {"NPU": {...}, "CPU": {...}}}.
    if (!device_properties.empty()) {
        ov::AnyMap device_map;
        for (const auto &dev : device_properties) {
            device_map.emplace(dev.first, dev.second);
            GST_INFO("Set pipeline config: DEVICE_PROPERTIES for %s with %zu nested keys", dev.first.c_str(),
                     dev.second.size());
        }
        properties.emplace(ov::device::properties(device_map));
    }

    return properties;
}

std::optional<ov::genai::SchedulerConfig> ConfigParser::parse_scheduler_config_string(const std::string &config_str) {
    // Return nullopt if no configuration provided
    if (config_str.empty()) {
        return std::nullopt;
    }

    // Parse KEY=VALUE,KEY=VALUE format and create scheduler config
    ov::genai::SchedulerConfig scheduler_config;

    // Collect cache eviction and sparse attention parameters (applied after the main loop)
    std::map<std::string, std::string> cache_eviction_params;
    std::map<std::string, std::string> sparse_attention_params;

    std::istringstream ss(config_str);
    std::string pair;
    while (std::getline(ss, pair, ',')) {
        pair = trim(pair);

        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(pair.substr(0, pos));
            std::string value = trim(pair.substr(pos + 1));

            if (key.empty()) {
                throw std::runtime_error("Empty key in scheduler config: '" + pair + "'");
            }

            try {
                if (key == "max_num_batched_tokens") {
                    scheduler_config.max_num_batched_tokens = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(), scheduler_config.max_num_batched_tokens);
                } else if (key == "num_kv_blocks") {
                    scheduler_config.num_kv_blocks = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(), scheduler_config.num_kv_blocks);
                } else if (key == "cache_size") {
                    scheduler_config.cache_size = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(), scheduler_config.cache_size);
                } else if (key == "dynamic_split_fuse") {
                    scheduler_config.dynamic_split_fuse = to_bool(value, key);
                    GST_INFO("Set scheduler config: %s = %s", key.c_str(),
                             scheduler_config.dynamic_split_fuse ? "true" : "false");
                } else if (key == "use_cache_eviction") {
                    scheduler_config.use_cache_eviction = to_bool(value, key);
                    GST_INFO("Set scheduler config: %s = %s", key.c_str(),
                             scheduler_config.use_cache_eviction ? "true" : "false");
                } else if (key == "max_num_seqs") {
                    scheduler_config.max_num_seqs = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(), scheduler_config.max_num_seqs);
                } else if (key == "enable_prefix_caching") {
                    scheduler_config.enable_prefix_caching = to_bool(value, key);
                    GST_INFO("Set scheduler config: %s = %s", key.c_str(),
                             scheduler_config.enable_prefix_caching ? "true" : "false");
                } else if (key == "num_linear_attention_blocks") {
                    scheduler_config.num_linear_attention_blocks = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(),
                             scheduler_config.num_linear_attention_blocks);
                } else if (key == "cache_interval_multiplier") {
                    scheduler_config.cache_interval_multiplier = std::stoull(value);
                    GST_INFO("Set scheduler config: %s = %zu", key.c_str(),
                             scheduler_config.cache_interval_multiplier.value());
                } else if (key == "use_sparse_attention") {
                    scheduler_config.use_sparse_attention = to_bool(value, key);
                    GST_INFO("Set scheduler config: %s = %s", key.c_str(),
                             scheduler_config.use_sparse_attention ? "true" : "false");
                } else if (key.starts_with("cache_eviction_")) {
                    // Collect cache eviction config parameters
                    cache_eviction_params[key] = value;
                } else if (key.starts_with("sparse_attention_")) {
                    // Collect sparse attention config parameters
                    sparse_attention_params[key] = value;
                } else {
                    throw std::runtime_error("Unknown scheduler config key: '" + key + "'");
                }
            } catch (const std::exception &e) {
                throw std::runtime_error("Failed to parse scheduler config value for key '" + key + "' with value '" +
                                         value + "': " + e.what());
            }
        } else if (!pair.empty()) {
            throw std::runtime_error("Invalid scheduler config format, expected KEY=VALUE: '" + pair + "'");
        }
    }

    // Apply cache eviction config if any parameters were provided
    if (!cache_eviction_params.empty()) {
        try {
            // Get current values or use defaults
            size_t start_size = scheduler_config.cache_eviction_config.get_start_size();
            size_t recent_size = scheduler_config.cache_eviction_config.get_recent_size();
            size_t max_cache_size = scheduler_config.cache_eviction_config.get_max_cache_size();
            ov::genai::AggregationMode aggregation_mode = scheduler_config.cache_eviction_config.aggregation_mode;
            bool apply_rotation = scheduler_config.cache_eviction_config.apply_rotation;
            size_t snapkv_window_size = scheduler_config.cache_eviction_config.snapkv_window_size;

            // KVCrush sub-config; budget=0 means KVCrush disabled
            ov::genai::KVCrushConfig kvcrush = scheduler_config.cache_eviction_config.kvcrush_config;
            // Adaptive R-KV sub-config, used when aggregation_mode=ADAPTIVE_RKV
            ov::genai::AdaptiveRKVConfig adaptive_rkv = scheduler_config.cache_eviction_config.adaptive_rkv_config;

            // Apply collected parameters
            for (const auto &param : cache_eviction_params) {
                const std::string &key = param.first;
                const std::string &value = param.second;

                if (key == "cache_eviction_start_size") {
                    start_size = std::stoull(value);
                } else if (key == "cache_eviction_recent_size") {
                    recent_size = std::stoull(value);
                } else if (key == "cache_eviction_max_cache_size") {
                    max_cache_size = std::stoull(value);
                } else if (key == "cache_eviction_aggregation_mode") {
                    if (value == "SUM") {
                        aggregation_mode = ov::genai::AggregationMode::SUM;
                    } else if (value == "NORM_SUM") {
                        aggregation_mode = ov::genai::AggregationMode::NORM_SUM;
                    } else if (value == "ADAPTIVE_RKV") {
                        // cannot be combined with KVCrush
                        aggregation_mode = ov::genai::AggregationMode::ADAPTIVE_RKV;
                    } else {
                        throw std::runtime_error("Invalid cache_eviction_aggregation_mode value '" + value +
                                                 "'. Valid values are: SUM, NORM_SUM, ADAPTIVE_RKV");
                    }
                } else if (key == "cache_eviction_apply_rotation") {
                    apply_rotation = to_bool(value, key);
                } else if (key == "cache_eviction_snapkv_window_size") {
                    snapkv_window_size = std::stoull(value);
                } else if (key == "cache_eviction_kvcrush_budget") {
                    kvcrush.budget = std::stoull(value);
                } else if (key == "cache_eviction_kvcrush_rng_seed") {
                    kvcrush.rng_seed = std::stoull(value);
                } else if (key == "cache_eviction_kvcrush_anchor_point_mode") {
                    if (value == "RANDOM") {
                        kvcrush.anchor_point_mode = ov::genai::KVCrushAnchorPointMode::RANDOM;
                    } else if (value == "ZEROS") {
                        kvcrush.anchor_point_mode = ov::genai::KVCrushAnchorPointMode::ZEROS;
                    } else if (value == "ONES") {
                        kvcrush.anchor_point_mode = ov::genai::KVCrushAnchorPointMode::ONES;
                    } else if (value == "MEAN") {
                        kvcrush.anchor_point_mode = ov::genai::KVCrushAnchorPointMode::MEAN;
                    } else if (value == "ALTERNATING") {
                        kvcrush.anchor_point_mode = ov::genai::KVCrushAnchorPointMode::ALTERNATING;
                    } else {
                        throw std::runtime_error("Invalid cache_eviction_kvcrush_anchor_point_mode value '" + value +
                                                 "'. Valid values are: RANDOM, ZEROS, ONES, MEAN, ALTERNATING");
                    }
                } else if (key == "cache_eviction_adaptive_rkv_attention_mass") {
                    adaptive_rkv.attention_mass = std::stod(value);
                } else if (key == "cache_eviction_adaptive_rkv_window_size") {
                    adaptive_rkv.window_size = std::stoull(value);
                } else {
                    throw std::runtime_error("Unknown cache eviction config key: '" + key + "'");
                }
            }

            // Create new cache eviction config
            scheduler_config.cache_eviction_config =
                ov::genai::CacheEvictionConfig(start_size, recent_size, max_cache_size, aggregation_mode,
                                               apply_rotation, snapkv_window_size, kvcrush, adaptive_rkv);

            GST_INFO("Applied cache eviction config: start_size=%zu, "
                     "recent_size=%zu, max_cache_size=%zu, aggregation_mode=%d, "
                     "apply_rotation=%s, snapkv_window_size=%zu, kvcrush_budget=%zu, "
                     "adaptive_rkv_attention_mass=%f, adaptive_rkv_window_size=%zu",
                     start_size, recent_size, max_cache_size, static_cast<int>(aggregation_mode),
                     apply_rotation ? "true" : "false", snapkv_window_size, kvcrush.budget, adaptive_rkv.attention_mass,
                     adaptive_rkv.window_size);
        } catch (const std::exception &e) {
            throw std::runtime_error("Failed to apply cache eviction config: " + std::string(e.what()));
        }
    }

    // Apply sparse attention config if any parameters were provided
    if (!sparse_attention_params.empty()) {
        try {
            // Get current values or use defaults
            ov::genai::SparseAttentionMode mode = scheduler_config.sparse_attention_config.mode;
            size_t num_last_dense_tokens_in_prefill =
                scheduler_config.sparse_attention_config.num_last_dense_tokens_in_prefill;
            size_t num_retained_start_tokens_in_cache =
                scheduler_config.sparse_attention_config.num_retained_start_tokens_in_cache;
            size_t num_retained_recent_tokens_in_cache =
                scheduler_config.sparse_attention_config.num_retained_recent_tokens_in_cache;
            float xattention_threshold = scheduler_config.sparse_attention_config.xattention_threshold;
            size_t xattention_block_size = scheduler_config.sparse_attention_config.xattention_block_size;
            size_t xattention_stride = scheduler_config.sparse_attention_config.xattention_stride;

            // Apply collected parameters
            for (const auto &param : sparse_attention_params) {
                const std::string &key = param.first;
                const std::string &value = param.second;

                if (key == "sparse_attention_mode") {
                    if (value == "TRISHAPE") {
                        mode = ov::genai::SparseAttentionMode::TRISHAPE;
                    } else if (value == "XATTENTION") {
                        mode = ov::genai::SparseAttentionMode::XATTENTION;
                    } else {
                        throw std::runtime_error("Invalid sparse_attention_mode value '" + value +
                                                 "'. Valid values are: TRISHAPE, XATTENTION");
                    }
                } else if (key == "sparse_attention_num_last_dense_tokens_in_prefill") {
                    num_last_dense_tokens_in_prefill = std::stoull(value);
                } else if (key == "sparse_attention_num_retained_start_tokens_in_cache") {
                    num_retained_start_tokens_in_cache = std::stoull(value);
                } else if (key == "sparse_attention_num_retained_recent_tokens_in_cache") {
                    num_retained_recent_tokens_in_cache = std::stoull(value);
                } else if (key == "sparse_attention_xattention_threshold") {
                    xattention_threshold = std::stof(value);
                } else if (key == "sparse_attention_xattention_block_size") {
                    xattention_block_size = std::stoull(value);
                } else if (key == "sparse_attention_xattention_stride") {
                    xattention_stride = std::stoull(value);
                } else {
                    throw std::runtime_error("Unknown sparse attention config key: '" + key + "'");
                }
            }

            scheduler_config.sparse_attention_config = ov::genai::SparseAttentionConfig(
                mode, num_last_dense_tokens_in_prefill, num_retained_start_tokens_in_cache,
                num_retained_recent_tokens_in_cache, xattention_threshold, xattention_block_size, xattention_stride);

            GST_INFO("Applied sparse attention config: mode=%d, num_last_dense_tokens_in_prefill=%zu, "
                     "num_retained_start_tokens_in_cache=%zu, num_retained_recent_tokens_in_cache=%zu, "
                     "xattention_threshold=%f, xattention_block_size=%zu, xattention_stride=%zu",
                     static_cast<int>(mode), num_last_dense_tokens_in_prefill, num_retained_start_tokens_in_cache,
                     num_retained_recent_tokens_in_cache, xattention_threshold, xattention_block_size,
                     xattention_stride);
        } catch (const std::exception &e) {
            throw std::runtime_error("Failed to apply sparse attention config: " + std::string(e.what()));
        }
    }

    return scheduler_config;
}

} // namespace genai
