/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "mono3d_calibration.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace post_processing {

Mono3DCalibration parseMono3DIntrinsics(const std::string &intrinsics_file, int default_width, int default_height) {
    Mono3DCalibration calib;
    calib.orig_width = default_width;
    calib.orig_height = default_height;
    // Identity-like default projection (overwritten when a valid file is provided).
    calib.p2 = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

    if (intrinsics_file.empty())
        return calib;

    try {
        std::ifstream f(intrinsics_file);
        if (!f.is_open())
            return calib;

        nlohmann::json j;
        f >> j;

        if (j.contains("projection_matrix") && j["projection_matrix"].is_array()) {
            const auto &m = j["projection_matrix"];
            if (m.size() == 3) {
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c)
                        calib.p2[r * 4 + c] = m[r][c].get<double>();
                calib.valid = true;
            }
        } else if (j.contains("intrinsic_matrix") && j["intrinsic_matrix"].is_array()) {
            const auto &k = j["intrinsic_matrix"];
            if (k.size() == 3) {
                // P2 = [K | 0]
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c)
                        calib.p2[r * 4 + c] = k[r][c].get<double>();
                    calib.p2[r * 4 + 3] = 0.0;
                }
                calib.valid = true;
            }
        }

        if (j.contains("image_size") && j["image_size"].is_array() && j["image_size"].size() == 2) {
            calib.orig_width = j["image_size"][0].get<int>();
            calib.orig_height = j["image_size"][1].get<int>();
        }
    } catch (const std::exception &) {
        // Leave calib.valid as-is; caller falls back to defaults.
    }

    return calib;
}

void applyMono3DCalibrationToStructure(GstStructure *s, const Mono3DCalibration &calib) {
    if (!s)
        return;

    GValue arr = G_VALUE_INIT;
    g_value_init(&arr, GST_TYPE_ARRAY);
    for (double v : calib.p2) {
        GValue item = G_VALUE_INIT;
        g_value_init(&item, G_TYPE_DOUBLE);
        g_value_set_double(&item, v);
        gst_value_array_append_value(&arr, &item);
        g_value_unset(&item);
    }
    gst_structure_set_value(s, "P2", &arr);
    g_value_unset(&arr);

    gst_structure_set(s, "orig_width", G_TYPE_INT, calib.orig_width, "orig_height", G_TYPE_INT, calib.orig_height,
                      NULL);
}

} // namespace post_processing
