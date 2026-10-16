/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "safe_arithmetic.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>

TEST(SafeArithmetic, safe_convert_positive_test) {
    const float float_value = 1234567.0f;
    const double double_value = 123456789.0;
    const int int_value = 12345;
    const unsigned int uint_value = 12345;

    uint32_t expected_uint_value_from_float = static_cast<uint32_t>(float_value);
    uint32_t uint_value_from_float = safe_convert<uint32_t>(float_value);

    float expected_float_value_from_double = static_cast<float>(double_value);
    float float_value_from_double = safe_convert<float>(double_value);

    uint32_t expected_uint_value_from_int = static_cast<uint32_t>(int_value);
    uint32_t uint_value_from_int = safe_convert<uint32_t>(int_value);

    int expected_int_value_from_uint = static_cast<int>(uint_value);
    int int_value_from_uint = safe_convert<int>(uint_value);

    uint32_t expected_uint_value_from_size_t = static_cast<uint32_t>(int_value);
    uint32_t uint_value_from_size_t = safe_convert<uint32_t>(int_value);

    ASSERT_EQ(uint_value_from_float, expected_uint_value_from_float);
    ASSERT_EQ(float_value_from_double, expected_float_value_from_double);
    ASSERT_EQ(uint_value_from_int, expected_uint_value_from_int);
    ASSERT_EQ(int_value_from_uint, expected_int_value_from_uint);
    ASSERT_EQ(uint_value_from_size_t, expected_uint_value_from_size_t);
}

TEST(SafeArithmetic, safe_convert_negative_test) {
    const float positive_float_value = 12345678901.0f;
    const float negative_float_value = -12345678901.0f;

    int32_t expected_positive_value_int = std::numeric_limits<int32_t>::max();
    int32_t expected_negative_value_int = std::numeric_limits<int32_t>::min();

    int32_t positive_int_value_from_float = safe_convert<int32_t>(positive_float_value);
    int32_t negative_int_value_from_float = safe_convert<int32_t>(negative_float_value);

    EXPECT_EQ(positive_int_value_from_float, expected_positive_value_int);
    EXPECT_EQ(negative_int_value_from_float, expected_negative_value_int);

    const double positive_double_value = std::numeric_limits<float>::max() + 12345678901.0f;
    const double negative_double_value = std::numeric_limits<float>::min() - 12345678901.0f;

    float expected_positive_value_float = std::numeric_limits<float>::max();
    float expected_negative_value_float = std::numeric_limits<float>::min();

    float positive_float_value_from_double = safe_convert<float>(positive_double_value);
    float negative_float_value_from_double = safe_convert<float>(negative_double_value);

    EXPECT_EQ(positive_float_value_from_double, expected_positive_value_float);
    EXPECT_EQ(negative_float_value_from_double, expected_negative_value_float);

    const int positive_int_value = 214748364;
    const int negative_int_value = -214748364;

    uint8_t expected_positive_value_uint8_t = std::numeric_limits<uint8_t>::max();
    uint8_t expected_negative_value_uint8_t = std::numeric_limits<uint16_t>::min();

    uint8_t positive_uint8_t_value_from_int = safe_convert<uint8_t>(positive_int_value);
    uint8_t negative_uint8_t_value_from_int = safe_convert<uint16_t>(negative_int_value);

    EXPECT_EQ(positive_uint8_t_value_from_int, expected_positive_value_uint8_t);
    EXPECT_EQ(negative_uint8_t_value_from_int, expected_negative_value_uint8_t);

    const unsigned int positive_uint_value = 4294967290U;

    int expected_positive_value_uint_from_int = std::numeric_limits<int>::max();

    int positive_value_uint_from_int = safe_convert<int>(positive_uint_value);

    EXPECT_EQ(positive_value_uint_from_int, expected_positive_value_uint_from_int);

    uint8_t expected_positive_value_uint_from_uint = std::numeric_limits<uint8_t>::max();

    uint8_t positive_value_uint_from_uint = safe_convert<uint8_t>(positive_uint_value);

    EXPECT_EQ(positive_value_uint_from_uint, expected_positive_value_uint_from_uint);
}

TEST(SafeArithmetic, safe_add_positive_test) {
    int32_t val1 = 12345678;
    int32_t val2 = 87654321;
    int32_t expected_sum = val1 + val2;

    EXPECT_EQ(safe_add(val1, val2), expected_sum);
    EXPECT_EQ(safe_add(-val1, -val2), -expected_sum);
    EXPECT_EQ(safe_add((uint32_t)val1, (uint32_t)val2), (uint32_t)expected_sum);

    EXPECT_EQ(safe_add(std::numeric_limits<int32_t>::min(), val1), std::numeric_limits<int32_t>::min() + val1);
    EXPECT_EQ(safe_add(val1, std::numeric_limits<int32_t>::min()), val1 + std::numeric_limits<int32_t>::min());

    EXPECT_EQ(safe_add(std::numeric_limits<int32_t>::max(), -val1), std::numeric_limits<int32_t>::max() - val1);
    EXPECT_EQ(safe_add(val1, std::numeric_limits<int32_t>::min()), val1 + std::numeric_limits<int32_t>::min());

    EXPECT_EQ(safe_add(std::numeric_limits<int64_t>::max(), (int64_t)(-1)), std::numeric_limits<int64_t>::max() - 1);
    EXPECT_EQ(safe_add(std::numeric_limits<int64_t>::min(), (int64_t)1), std::numeric_limits<int64_t>::min() + 1);
}

TEST(SafeArithmetic, safe_add_negative_test) {
    uint32_t uint_max = std::numeric_limits<uint32_t>::max();
    uint32_t val1 = 123456;
    ASSERT_THROW(safe_add(uint_max, val1), std::overflow_error);

    int32_t int_min = std::numeric_limits<int32_t>::min();
    int32_t val2 = -1;
    ASSERT_THROW(safe_add(int_min, val2), std::overflow_error);
    ASSERT_THROW(safe_add(val2, int_min), std::overflow_error);
}

TEST(SafeArithmetic, safe_mul_positive_test) {
    int32_t val1 = 123456;
    int32_t val2 = 17390;
    int32_t expected_mul = val1 * val2;

    EXPECT_EQ(safe_mul(val1, val2), expected_mul);
    EXPECT_EQ(safe_mul(-val1, val2), -expected_mul);
    EXPECT_EQ(safe_mul(val1, -val2), -expected_mul);
    EXPECT_EQ(safe_mul(-val1, -val2), expected_mul);
    EXPECT_EQ(safe_mul((uint32_t)val1, (uint32_t)val2), (uint32_t)expected_mul);

    EXPECT_EQ(safe_mul(std::numeric_limits<int32_t>::max(), 1), std::numeric_limits<int32_t>::max() * 1);
    EXPECT_EQ(safe_mul(std::numeric_limits<int64_t>::max(), (int64_t)1), std::numeric_limits<int64_t>::max() * 1);
    EXPECT_EQ(safe_mul(std::numeric_limits<int32_t>::min(), 1), std::numeric_limits<int32_t>::min() * 1);
    EXPECT_EQ(safe_mul(1, std::numeric_limits<int32_t>::min()), 1 * std::numeric_limits<int32_t>::min());
}

TEST(SafeArithmetic, safe_mul_negative_test) {
    EXPECT_THROW(safe_mul(std::numeric_limits<uint32_t>::max(), 2U), std::overflow_error);
    EXPECT_THROW(safe_mul(std::numeric_limits<uint64_t>::max(), (uint64_t)2), std::overflow_error);
    EXPECT_THROW(safe_mul(std::numeric_limits<int32_t>::max(), 2), std::overflow_error);
    EXPECT_THROW(safe_mul(std::numeric_limits<int32_t>::max(), -2), std::overflow_error);
    EXPECT_THROW(safe_mul(std::numeric_limits<int32_t>::min(), 2), std::overflow_error);
    EXPECT_THROW(safe_mul(-1, std::numeric_limits<int32_t>::min()), std::overflow_error);
    EXPECT_THROW(safe_mul(std::numeric_limits<int64_t>::min(), (int64_t)-1), std::overflow_error);
}

int main(int argc, char *argv[]) {
    std::cout << "Running Components::SafeArithmetic from " << __FILE__ << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
