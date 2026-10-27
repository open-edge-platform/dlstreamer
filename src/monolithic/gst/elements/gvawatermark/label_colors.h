/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

/*
 * Deterministic mapping from an object label to a bounding box color.
 *
 * The color depends only on the label text, so a given class gets the same color in every run and for every
 * model. Well-known labels (COCO-80 and common synonyms) use a fixed table that keeps the colors YOLO models
 * historically rendered with (palette[class_id % 20]); any other label is hashed into the palette without
 * black and white. Black and white are never used for labels.
 */
namespace label_colors {

struct Rgb {
    uint8_t r, g, b;
    constexpr bool operator==(const Rgb &) const = default;
};

enum ColorIdx : uint8_t {
    RED = 0,
    GREEN,
    BLUE,
    YELLOW,
    CYAN,
    MAGENTA,
    ORANGE,
    PINK,
    MINT,
    LIME,
    VIOLET,
    AZURE,
    ORANGE_RED,
    CHARTREUSE,
    SPRING_GREEN,
    ROYAL_BLUE,
    INDIGO,
    ROSE,
    BLACK,
    WHITE,
};

inline constexpr std::array<Rgb, 20> kPalette = {{
    {255, 0, 0},   {0, 255, 0},   {0, 0, 255},   {255, 255, 0}, {0, 255, 255}, {255, 0, 255},   {255, 170, 0},
    {255, 0, 170}, {0, 255, 170}, {170, 255, 0}, {170, 0, 255}, {0, 170, 255}, {255, 85, 0},    {85, 255, 0},
    {0, 255, 85},  {0, 85, 255},  {85, 0, 255},  {255, 0, 85},  {0, 0, 0},     {255, 255, 255},
}};

// Palette entries other than black and white (indices 0..17).
inline constexpr size_t kNumChromaticColors = 18;
// Colors used for labels that are not in the well-known table.
inline constexpr size_t kNumFallbackColors = kNumChromaticColors;
static_assert(kNumChromaticColors == BLACK);

// Keys are normalized labels (see normalize_label()).
inline constexpr std::pair<std::string_view, ColorIdx> kWellKnownLabels[] = {
    {"person", RED},
    {"bicycle", GREEN},
    {"car", BLUE},
    {"motorcycle", YELLOW},
    {"airplane", CYAN},
    {"bus", MAGENTA},
    {"train", ORANGE},
    {"truck", PINK},
    {"boat", MINT},
    {"trafficlight", LIME},
    {"firehydrant", VIOLET},
    {"stopsign", AZURE},
    {"parkingmeter", ORANGE_RED},
    {"bench", CHARTREUSE},
    {"bird", SPRING_GREEN},
    {"cat", ROYAL_BLUE},
    {"dog", INDIGO},
    {"horse", ROSE},
    {"sheep", CYAN},
    {"cow", MAGENTA},
    {"elephant", RED},
    {"bear", GREEN},
    {"zebra", BLUE},
    {"giraffe", YELLOW},
    {"backpack", CYAN},
    {"umbrella", MAGENTA},
    {"handbag", ORANGE},
    {"tie", PINK},
    {"suitcase", MINT},
    {"frisbee", LIME},
    {"skis", VIOLET},
    {"snowboard", AZURE},
    {"sportsball", ORANGE_RED},
    {"kite", CHARTREUSE},
    {"baseballbat", SPRING_GREEN},
    {"baseballglove", ROYAL_BLUE},
    {"skateboard", INDIGO},
    {"surfboard", ROSE},
    {"tennisracket", YELLOW},
    {"bottle", AZURE},
    {"wineglass", RED},
    {"cup", GREEN},
    {"fork", BLUE},
    {"knife", YELLOW},
    {"spoon", CYAN},
    {"bowl", MAGENTA},
    {"banana", ORANGE},
    {"apple", PINK},
    {"sandwich", MINT},
    {"orange", LIME},
    {"broccoli", VIOLET},
    {"carrot", AZURE},
    {"hotdog", ORANGE_RED},
    {"pizza", CHARTREUSE},
    {"donut", SPRING_GREEN},
    {"cake", ROYAL_BLUE},
    {"chair", INDIGO},
    {"couch", ROSE},
    {"pottedplant", GREEN},
    {"bed", ORANGE},
    {"diningtable", RED},
    {"toilet", GREEN},
    {"tv", BLUE},
    {"laptop", YELLOW},
    {"mouse", CYAN},
    {"remote", MAGENTA},
    {"keyboard", ORANGE},
    {"cellphone", PINK},
    {"microwave", MINT},
    {"oven", LIME},
    {"toaster", VIOLET},
    {"sink", AZURE},
    {"refrigerator", ORANGE_RED},
    {"book", CHARTREUSE},
    {"clock", SPRING_GREEN},
    {"vase", ROYAL_BLUE},
    {"scissors", INDIGO},
    {"teddybear", ROSE},
    {"hairdrier", MINT},
    {"toothbrush", LIME},
    {"motorbike", YELLOW},
    {"aeroplane", CYAN},
    {"sofa", ROSE},
    {"tvmonitor", BLUE},
    {"hairdryer", MINT},
    {"vehicle", BLUE},
    {"pedestrian", RED},
    {"bike", GREEN},
    {"motor", YELLOW},
    {"face", GREEN},
    {"licenseplate", YELLOW},
    {"box", ORANGE},
    {"defect", MAGENTA},
    {"shippinglabel", CYAN},
};

// Lower-case ASCII and drop everything outside [a-z0-9], so that e.g. "traffic light", "traffic_light",
// "trafficlight" and "Traffic-Light" map to the same key.
inline std::string normalize_label(std::string_view label) {
    std::string key;
    key.reserve(label.size());
    for (char ch : label) {
        if (ch >= 'A' && ch <= 'Z')
            key.push_back(static_cast<char>(ch - 'A' + 'a'));
        else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            key.push_back(ch);
    }
    return key;
}

// 32-bit FNV-1a. Implemented here (instead of std::hash / g_str_hash) so the result never changes.
constexpr uint32_t fnv1a_32(std::string_view s) {
    uint32_t hash = 2166136261u;
    for (char ch : s) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= 16777619u;
    }
    return hash;
}

// Index into kPalette for a given object label. Empty labels map to red.
inline size_t label_to_palette_index(std::string_view label) {
    const std::string key = normalize_label(label);
    if (key.empty())
        return RED;
    for (const auto &[name, idx] : kWellKnownLabels) {
        if (name == key)
            return idx;
    }
    return fnv1a_32(key) % kNumFallbackColors;
}

inline Rgb label_to_rgb(std::string_view label) {
    return kPalette[label_to_palette_index(label)];
}

// Index into kPalette for a tracked object, skipping black and white.
inline size_t tracking_id_to_palette_index(int64_t object_id) {
    const int64_t n = static_cast<int64_t>(kNumChromaticColors);
    return static_cast<size_t>(((object_id % n) + n) % n);
}

} // namespace label_colors
