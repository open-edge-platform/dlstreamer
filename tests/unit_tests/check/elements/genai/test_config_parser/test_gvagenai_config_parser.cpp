/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file test_gvagenai_config_parser.cpp
 * @brief Unit tests for gvagenai's ConfigParser.
 *
 * Pure unit tests calling ConfigParser::parse_* directly and assert on the returned
 * ov::AnyMap / ov::genai::SchedulerConfig.
 *
 * Coverage:
 *   - generation config: scalars, lenient booleans, semicolon-separated sets, stop_criteria
 *     enum, and error paths.
 *   - scheduler config: scalars, cache eviction, sparse attention, and error paths.
 *   - pipeline config: flat string values, dotted per-device nesting, and error paths.
 */

#include "configs.hpp"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <openvino/genai/generation_config.hpp>
#include <openvino/genai/scheduler_config.hpp>

#include <set>
#include <string>

using genai::ConfigParser;

/* ========================================================================= */
/*  Generation config                                                        */
/* ========================================================================= */

TEST(GenerationConfig, EmptyStringReturnsEmptyMap) {
    auto props = ConfigParser::parse_generation_config_string("");
    EXPECT_TRUE(props.empty());
}

TEST(GenerationConfig, IntAndFloatScalars) {
    auto props = ConfigParser::parse_generation_config_string("max_new_tokens=128,temperature=0.7,top_k=40");
    ASSERT_TRUE(props.count("max_new_tokens"));
    EXPECT_EQ(props.at("max_new_tokens").as<size_t>(), 128u);
    ASSERT_TRUE(props.count("temperature"));
    EXPECT_FLOAT_EQ(props.at("temperature").as<float>(), 0.7f);
    ASSERT_TRUE(props.count("top_k"));
    EXPECT_EQ(props.at("top_k").as<size_t>(), 40u);
}

TEST(GenerationConfig, NewKeys2026) {
    auto props = ConfigParser::parse_generation_config_string("min_p=0.05,pruning_ratio=30,relevance_weight=0.8");
    ASSERT_TRUE(props.count("min_p"));
    EXPECT_FLOAT_EQ(props.at("min_p").as<float>(), 0.05f);
    ASSERT_TRUE(props.count("pruning_ratio"));
    EXPECT_EQ(props.at("pruning_ratio").as<size_t>(), 30u);
    ASSERT_TRUE(props.count("relevance_weight"));
    EXPECT_FLOAT_EQ(props.at("relevance_weight").as<float>(), 0.8f);
}

TEST(GenerationConfig, LenientBooleanForms) {
    // All of these should parse to true / false via ov::Any (case-insensitive).
    for (const std::string &t : {"true", "TRUE", "1", "yes", "on"}) {
        auto props = ConfigParser::parse_generation_config_string("do_sample=" + t);
        ASSERT_TRUE(props.count("do_sample")) << "form: " << t;
        EXPECT_TRUE(props.at("do_sample").as<bool>()) << "form: " << t;
    }
    for (const std::string &f : {"false", "0", "no", "off"}) {
        auto props = ConfigParser::parse_generation_config_string("do_sample=" + f);
        ASSERT_TRUE(props.count("do_sample")) << "form: " << f;
        EXPECT_FALSE(props.at("do_sample").as<bool>()) << "form: " << f;
    }
}

TEST(GenerationConfig, InvalidBooleanThrows) {
    EXPECT_THROW(ConfigParser::parse_generation_config_string("do_sample=maybe"), std::runtime_error);
}

TEST(GenerationConfig, StopStringsSemicolonSet) {
    auto props = ConfigParser::parse_generation_config_string("stop_strings=STOP;END;DONE");
    ASSERT_TRUE(props.count("stop_strings"));
    auto s = props.at("stop_strings").as<std::set<std::string>>();
    EXPECT_EQ(s.size(), 3u);
    EXPECT_TRUE(s.count("STOP"));
    EXPECT_TRUE(s.count("END"));
    EXPECT_TRUE(s.count("DONE"));
}

TEST(GenerationConfig, StopTokenIdsSemicolonSet) {
    auto props = ConfigParser::parse_generation_config_string("stop_token_ids=1;2;3");
    ASSERT_TRUE(props.count("stop_token_ids"));
    auto s = props.at("stop_token_ids").as<std::set<int64_t>>();
    EXPECT_EQ(s.size(), 3u);
    EXPECT_TRUE(s.count(1));
    EXPECT_TRUE(s.count(2));
    EXPECT_TRUE(s.count(3));
}

TEST(GenerationConfig, StopTokenIdsInvalidThrows) {
    EXPECT_THROW(ConfigParser::parse_generation_config_string("stop_token_ids=1;abc;3"), std::runtime_error);
}

TEST(GenerationConfig, StopCriteriaEnum) {
    auto props = ConfigParser::parse_generation_config_string("stop_criteria=EARLY");
    ASSERT_TRUE(props.count("stop_criteria"));
    EXPECT_EQ(props.at("stop_criteria").as<ov::genai::StopCriteria>(), ov::genai::StopCriteria::EARLY);
}

TEST(GenerationConfig, StopCriteriaInvalidThrows) {
    EXPECT_THROW(ConfigParser::parse_generation_config_string("stop_criteria=SOMETIMES"), std::runtime_error);
}

TEST(GenerationConfig, MalformedPairThrows) {
    EXPECT_THROW(ConfigParser::parse_generation_config_string("max_new_tokens"), std::runtime_error);
}

TEST(GenerationConfig, EmptyKeyThrows) {
    EXPECT_THROW(ConfigParser::parse_generation_config_string("=100"), std::runtime_error);
}

TEST(GenerationConfig, UnknownKeyIsIgnored) {
    // Unknown generation keys are simply not converted (no throw), leaving an empty map.
    auto props = ConfigParser::parse_generation_config_string("not_a_real_key=5");
    EXPECT_TRUE(props.empty());
}

/* ========================================================================= */
/*  Scheduler config                                                         */
/* ========================================================================= */

TEST(SchedulerConfig, EmptyStringReturnsNullopt) {
    auto cfg = ConfigParser::parse_scheduler_config_string("");
    EXPECT_FALSE(cfg.has_value());
}

TEST(SchedulerConfig, ScalarsAndBool) {
    auto cfg = ConfigParser::parse_scheduler_config_string(
        "max_num_batched_tokens=512,cache_size=8,max_num_seqs=64,enable_prefix_caching=yes");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->max_num_batched_tokens, 512u);
    EXPECT_EQ(cfg->cache_size, 8u);
    EXPECT_EQ(cfg->max_num_seqs, 64u);
    EXPECT_TRUE(cfg->enable_prefix_caching);
}

TEST(SchedulerConfig, NewScalars2026) {
    auto cfg =
        ConfigParser::parse_scheduler_config_string("num_linear_attention_blocks=4,cache_interval_multiplier=16");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->num_linear_attention_blocks, 4u);
    ASSERT_TRUE(cfg->cache_interval_multiplier.has_value());
    EXPECT_EQ(cfg->cache_interval_multiplier.value(), 16u);
}

TEST(SchedulerConfig, CacheEvictionBasic) {
    auto cfg = ConfigParser::parse_scheduler_config_string(
        "use_cache_eviction=true,cache_eviction_start_size=32,cache_eviction_recent_size=128,"
        "cache_eviction_max_cache_size=672,cache_eviction_aggregation_mode=SUM");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->use_cache_eviction);
    EXPECT_EQ(cfg->cache_eviction_config.get_start_size(), 32u);
    EXPECT_EQ(cfg->cache_eviction_config.get_recent_size(), 128u);
    EXPECT_EQ(cfg->cache_eviction_config.get_max_cache_size(), 672u);
    EXPECT_EQ(cfg->cache_eviction_config.aggregation_mode, ov::genai::AggregationMode::SUM);
}

TEST(SchedulerConfig, CacheEvictionAdaptiveRKVAndKVCrush) {
    auto cfg = ConfigParser::parse_scheduler_config_string(
        "cache_eviction_aggregation_mode=ADAPTIVE_RKV,"
        "cache_eviction_adaptive_rkv_attention_mass=0.95,cache_eviction_adaptive_rkv_window_size=16,"
        "cache_eviction_kvcrush_budget=64,cache_eviction_kvcrush_anchor_point_mode=MEAN");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->cache_eviction_config.aggregation_mode, ov::genai::AggregationMode::ADAPTIVE_RKV);
    EXPECT_DOUBLE_EQ(cfg->cache_eviction_config.adaptive_rkv_config.attention_mass, 0.95);
    EXPECT_EQ(cfg->cache_eviction_config.adaptive_rkv_config.window_size, 16u);
    EXPECT_EQ(cfg->cache_eviction_config.kvcrush_config.budget, 64u);
    EXPECT_EQ(cfg->cache_eviction_config.kvcrush_config.anchor_point_mode, ov::genai::KVCrushAnchorPointMode::MEAN);
}

TEST(SchedulerConfig, CacheEvictionInvalidAggregationModeThrows) {
    EXPECT_THROW(ConfigParser::parse_scheduler_config_string("cache_eviction_aggregation_mode=BOGUS"),
                 std::runtime_error);
}

TEST(SchedulerConfig, CacheEvictionInvalidKVCrushModeThrows) {
    EXPECT_THROW(ConfigParser::parse_scheduler_config_string("cache_eviction_kvcrush_anchor_point_mode=BOGUS"),
                 std::runtime_error);
}

TEST(SchedulerConfig, SparseAttention) {
    auto cfg = ConfigParser::parse_scheduler_config_string(
        "use_sparse_attention=true,sparse_attention_mode=XATTENTION,"
        "sparse_attention_xattention_threshold=0.7,sparse_attention_xattention_block_size=128,"
        "sparse_attention_xattention_stride=16,sparse_attention_num_last_dense_tokens_in_prefill=200");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->use_sparse_attention);
    EXPECT_EQ(cfg->sparse_attention_config.mode, ov::genai::SparseAttentionMode::XATTENTION);
    EXPECT_FLOAT_EQ(cfg->sparse_attention_config.xattention_threshold, 0.7f);
    EXPECT_EQ(cfg->sparse_attention_config.xattention_block_size, 128u);
    EXPECT_EQ(cfg->sparse_attention_config.xattention_stride, 16u);
    EXPECT_EQ(cfg->sparse_attention_config.num_last_dense_tokens_in_prefill, 200u);
}

TEST(SchedulerConfig, SparseAttentionInvalidModeThrows) {
    EXPECT_THROW(ConfigParser::parse_scheduler_config_string("sparse_attention_mode=BOGUS"), std::runtime_error);
}

TEST(SchedulerConfig, UnknownKeyThrows) {
    EXPECT_THROW(ConfigParser::parse_scheduler_config_string("not_a_real_key=5"), std::runtime_error);
}

TEST(SchedulerConfig, MalformedPairThrows) {
    EXPECT_THROW(ConfigParser::parse_scheduler_config_string("cache_size"), std::runtime_error);
}

/* ========================================================================= */
/*  Pipeline config                                                          */
/* ========================================================================= */

TEST(PipelineConfig, EmptyStringReturnsEmptyMap) {
    auto props = ConfigParser::parse_pipeline_config_string("");
    EXPECT_TRUE(props.empty());
}

TEST(PipelineConfig, FlatStringValues) {
    auto props = ConfigParser::parse_pipeline_config_string("GENERATE_HINT=BEST_PERF,MAX_PROMPT_LEN=2048");
    ASSERT_TRUE(props.count("GENERATE_HINT"));
    EXPECT_EQ(props.at("GENERATE_HINT").as<std::string>(), "BEST_PERF");
    ASSERT_TRUE(props.count("MAX_PROMPT_LEN"));
    // Value is kept as a string; the plugin coerces it. Verify round-trips to size_t too.
    EXPECT_EQ(props.at("MAX_PROMPT_LEN").as<std::string>(), "2048");
    EXPECT_EQ(props.at("MAX_PROMPT_LEN").as<size_t>(), 2048u);
}

TEST(PipelineConfig, DottedKeyNestsUnderDeviceProperties) {
    auto props = ConfigParser::parse_pipeline_config_string(
        "NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512,GENERATE_HINT=BEST_PERF");

    // Top-level key stays top-level.
    ASSERT_TRUE(props.count("GENERATE_HINT"));

    // Dotted keys are folded into ov::device::properties("NPU", ...) -> DEVICE_PROPERTIES_NPU.
    ASSERT_TRUE(props.count("DEVICE_PROPERTIES_NPU"));
    auto npu = props.at("DEVICE_PROPERTIES_NPU").as<ov::AnyMap>();
    ASSERT_TRUE(npu.count("MAX_PROMPT_LEN"));
    EXPECT_EQ(npu.at("MAX_PROMPT_LEN").as<std::string>(), "2048");
    ASSERT_TRUE(npu.count("MIN_RESPONSE_LEN"));
    EXPECT_EQ(npu.at("MIN_RESPONSE_LEN").as<std::string>(), "512");

    // The bare property name must not leak to the top level.
    EXPECT_FALSE(props.count("MAX_PROMPT_LEN"));
}

TEST(PipelineConfig, MalformedPairThrows) {
    EXPECT_THROW(ConfigParser::parse_pipeline_config_string("MAX_PROMPT_LEN"), std::runtime_error);
}

TEST(PipelineConfig, DottedKeyMissingPropThrows) {
    EXPECT_THROW(ConfigParser::parse_pipeline_config_string("NPU.=2048"), std::runtime_error);
}

GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
