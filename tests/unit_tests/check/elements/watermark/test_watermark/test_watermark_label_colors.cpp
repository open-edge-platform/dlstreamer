/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "label_colors.h"
#include "test_common.h"
#include "test_utils.h"

#include <cstring>
#include <gst/analytics/analytics.h>
#include <gst/video/gstvideometa.h>
#include <string>
#include <vector>

using label_colors::kPalette;
using label_colors::label_to_palette_index;
using label_colors::label_to_rgb;
using label_colors::Rgb;

/* COCO-80 class names in YOLO class-id order. Kept inline so the test does not depend on sample label files. */
static const char *coco80[] = {
    "person",         "bicycle",    "car",           "motorcycle",    "airplane",     "bus",           "train",
    "truck",          "boat",       "traffic light", "fire hydrant",  "stop sign",    "parking meter", "bench",
    "bird",           "cat",        "dog",           "horse",         "sheep",        "cow",           "elephant",
    "bear",           "zebra",      "giraffe",       "backpack",      "umbrella",     "handbag",       "tie",
    "suitcase",       "frisbee",    "skis",          "snowboard",     "sports ball",  "kite",          "baseball bat",
    "baseball glove", "skateboard", "surfboard",     "tennis racket", "bottle",       "wine glass",    "cup",
    "fork",           "knife",      "spoon",         "bowl",          "banana",       "apple",         "sandwich",
    "orange",         "broccoli",   "carrot",        "hot dog",       "pizza",        "donut",         "cake",
    "chair",          "couch",      "potted plant",  "bed",           "dining table", "toilet",        "tv",
    "laptop",         "mouse",      "remote",        "keyboard",      "cell phone",   "microwave",     "oven",
    "toaster",        "sink",       "refrigerator",  "book",          "clock",        "vase",          "scissors",
    "teddy bear",     "hair drier", "toothbrush"};
static_assert(sizeof(coco80) / sizeof(coco80[0]) == 80);

/* COCO classes that used to be rendered black or white (class_id % 20 in {18, 19}) and their new colors. */
struct Remapped {
    const char *label;
    label_colors::ColorIdx idx;
};
static const Remapped coco_remapped[] = {{"sheep", label_colors::CYAN},           {"cow", label_colors::MAGENTA},
                                         {"tennis racket", label_colors::YELLOW}, {"bottle", label_colors::AZURE},
                                         {"potted plant", label_colors::GREEN},   {"bed", label_colors::ORANGE},
                                         {"hair drier", label_colors::MINT},      {"toothbrush", label_colors::LIME}};

static const Remapped *find_remapped(const char *label) {
    for (const auto &r : coco_remapped)
        if (strcmp(r.label, label) == 0)
            return &r;
    return nullptr;
}

/* ========================================================================= */
/*  Pure mapping tests                                                       */
/* ========================================================================= */

GST_START_TEST(test_coco80_keeps_yolo_colors) {
    for (size_t id = 0; id < 80; id++) {
        const Remapped *remapped = find_remapped(coco80[id]);
        const size_t expected = remapped ? remapped->idx : id % 20;
        ck_assert_msg(label_to_palette_index(coco80[id]) == expected, "COCO class %zu '%s': expected %zu, got %zu", id,
                      coco80[id], expected, label_to_palette_index(coco80[id]));
    }
}
GST_END_TEST;

GST_START_TEST(test_label_spelling_variants_match) {
    const char *variants[][5] = {
        {"traffic light", "trafficlight", "traffic_light", "Traffic-Light", " TRAFFIC LIGHT "},
        {"license plate", "license-plate", "license_plate", "licenseplate", "License Plate"},
        {"teddy bear", "teddybear", "teddy_bear", "Teddy Bear", "TEDDY-BEAR"},
    };
    for (const auto &group : variants) {
        const size_t expected = label_to_palette_index(group[0]);
        for (const char *v : group)
            ck_assert_msg(label_to_palette_index(v) == expected, "'%s' differs from '%s'", v, group[0]);
    }
}
GST_END_TEST;

GST_START_TEST(test_synonyms_and_common_labels) {
    const std::pair<const char *, const char *> same_as[] = {
        {"motorbike", "motorcycle"}, {"aeroplane", "airplane"},    {"sofa", "couch"},
        {"tvmonitor", "tv"},         {"hair dryer", "hair drier"}, {"vehicle", "car"},
        {"pedestrian", "person"},    {"bike", "bicycle"},          {"motor", "motorcycle"},
    };
    for (const auto &[label, reference] : same_as)
        ck_assert_msg(label_to_palette_index(label) == label_to_palette_index(reference), "'%s' should match '%s'",
                      label, reference);

    ck_assert(label_to_palette_index("face") == label_colors::GREEN);
    ck_assert(label_to_palette_index("license-plate") == label_colors::YELLOW);
    ck_assert(label_to_palette_index("box") == label_colors::ORANGE);
    ck_assert(label_to_palette_index("defect") == label_colors::MAGENTA);
    ck_assert(label_to_palette_index("shipping_label") == label_colors::CYAN);
    ck_assert(label_to_palette_index("person") == label_colors::RED);
    ck_assert(label_to_palette_index("car") == label_colors::BLUE);
}
GST_END_TEST;

GST_START_TEST(test_empty_label_is_red) {
    ck_assert(label_to_palette_index("") == label_colors::RED);
    ck_assert(label_to_palette_index(" _-") == label_colors::RED);
}
GST_END_TEST;

GST_START_TEST(test_no_black_or_white_for_labels) {
    std::vector<std::string> labels(std::begin(coco80), std::end(coco80));
    for (const auto &[name, idx] : label_colors::kWellKnownLabels)
        labels.emplace_back(name);
    for (int i = 0; i < 1000; i++)
        labels.push_back("label_" + std::to_string(i));

    for (const auto &label : labels) {
        const Rgb c = label_to_rgb(label);
        ck_assert_msg(!(c == Rgb{0, 0, 0}) && !(c == Rgb{255, 255, 255}), "'%s' mapped to black/white", label.c_str());
    }
}
GST_END_TEST;

GST_START_TEST(test_unknown_labels_use_fallback_palette_and_are_stable) {
    /* Hard-coded expectations: changing the hash or the fallback palette must be a conscious decision. */
    ck_assert_uint_eq(label_colors::fnv1a_32(""), 2166136261u);
    ck_assert_uint_eq(label_colors::fnv1a_32("a"), 0xe40c292cu);
    ck_assert_uint_eq(label_colors::fnv1a_32("foobar"), 0xbf9cf968u);

    const std::pair<const char *, label_colors::ColorIdx> expected[] = {
        {"text", label_colors::CYAN},
        {"non-vehicle", label_colors::RED},
        {"helmet", label_colors::BLUE},
    };
    for (const auto &[label, idx] : expected)
        ck_assert_msg(label_to_palette_index(label) == idx, "'%s': expected %d, got %zu", label, idx,
                      label_to_palette_index(label));

    std::vector<int> usage(label_colors::kNumChromaticColors, 0);
    for (int i = 0; i < 1000; i++) {
        const std::string label = "unknown_" + std::to_string(i);
        const size_t idx = label_to_palette_index(label);
        ck_assert(idx < label_colors::kNumFallbackColors);
        usage[idx]++;
    }
    for (size_t idx = 0; idx < usage.size(); idx++)
        ck_assert_msg(usage[idx] > 0, "palette entry %zu never used by fallback", idx);
}
GST_END_TEST;

GST_START_TEST(test_tracking_ids_skip_black_and_white) {
    for (int64_t id = -100; id < 1000; id++)
        ck_assert(label_colors::tracking_id_to_palette_index(id) < label_colors::kNumChromaticColors);
    for (int64_t id = 0; id < 18; id++)
        ck_assert(label_colors::tracking_id_to_palette_index(id) == static_cast<size_t>(id));
}
GST_END_TEST;

/* ========================================================================= */
/*  Rendering tests: color of the rendered bounding box                      */
/* ========================================================================= */

constexpr char impl_name[] = "gvawatermarkimpl";

#define WATERMARK_BGR_CAPS GST_VIDEO_CAPS_MAKE("BGR")

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));
static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WATERMARK_BGR_CAPS));

/* BGR, width * 3 needs no row padding. */
constexpr guint FRAME_W = 320, FRAME_H = 240;
static Resolution test_resolution = {(gint)FRAME_W, (gint)FRAME_H};

constexpr guint ROI_X = 50, ROI_Y = 50, ROI_W = 100, ROI_H = 100;

struct ColorProbe {
    const char *label;
    bool with_class_descriptor; /* attach a dlstreamer-style class descriptor that maps label to index 0 */
    Rgb rendered;               /* filled by the check callback (RGB order) */
};

static void setup_buffer(GstBuffer *buffer, gpointer user_data) {
    ColorProbe *probe = static_cast<ColorProbe *>(user_data);

    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_WRITE));
    memset(info.data, 0x00, info.size);
    gst_buffer_unmap(buffer, &info);

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd od_mtd;
    ck_assert(gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_string(probe->label), ROI_X, ROI_Y,
                                                     ROI_W, ROI_H, 0.95f, &od_mtd));

    if (probe->with_class_descriptor) {
        gfloat level = 0.0f;
        GQuark quark = g_quark_from_string(probe->label);
        GstAnalyticsClsMtd descriptor;
        ck_assert(gst_analytics_relation_meta_add_cls_mtd(relation_meta, 1, &level, &quark, &descriptor));
        gst_analytics_mtd_set_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&descriptor), "class_descriptor");
        ck_assert(gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd.id,
                                                           descriptor.id));
    }

    GstVideoRegionOfInterestMeta *roi_meta =
        gst_buffer_add_video_region_of_interest_meta(buffer, probe->label, ROI_X, ROI_Y, ROI_W, ROI_H);
    roi_meta->id = od_mtd.id;
}

static void read_left_edge_color(GstBuffer *buffer, gpointer user_data) {
    ColorProbe *probe = static_cast<ColorProbe *>(user_data);

    /* middle of the left edge: away from the label text drawn above the box */
    const gsize offset = (ROI_Y + ROI_H / 2) * FRAME_W * 3 + ROI_X * 3;
    GstMapInfo info;
    ck_assert(gst_buffer_map(buffer, &info, GST_MAP_READ));
    ck_assert(offset + 2 < info.size);
    probe->rendered = Rgb{info.data[offset + 2], info.data[offset + 1], info.data[offset]};
    gst_buffer_unmap(buffer, &info);
}

static void check_rendered_color(const char *label, bool with_class_descriptor, Rgb expected) {
    ColorProbe probe = {label, with_class_descriptor, {0, 0, 0}};
    run_test(impl_name, WATERMARK_BGR_CAPS, test_resolution, &srctemplate, &sinktemplate, setup_buffer,
             read_left_edge_color, &probe, NULL);
    ck_assert_msg(probe.rendered == expected, "'%s': expected RGB(%d,%d,%d), rendered RGB(%d,%d,%d)", label, expected.r,
                  expected.g, expected.b, probe.rendered.r, probe.rendered.g, probe.rendered.b);
}

GST_START_TEST(test_render_uses_label_color) {
    check_rendered_color("person", false, kPalette[label_colors::RED]);
    check_rendered_color("car", false, kPalette[label_colors::BLUE]);
    check_rendered_color("sheep", false, kPalette[label_colors::CYAN]);
    check_rendered_color("helmet", false, label_to_rgb("helmet"));
}
GST_END_TEST;

GST_START_TEST(test_render_ignores_class_descriptor_index) {
    /* The descriptor places "car" at index 0, which used to render it red. */
    check_rendered_color("car", true, kPalette[label_colors::BLUE]);
}
GST_END_TEST;

static Suite *watermark_label_colors_suite(void) {
    Suite *s = suite_create("watermark_label_colors");

    TCase *tc_mapping = tcase_create("mapping");
    suite_add_tcase(s, tc_mapping);
    tcase_add_test(tc_mapping, test_coco80_keeps_yolo_colors);
    tcase_add_test(tc_mapping, test_label_spelling_variants_match);
    tcase_add_test(tc_mapping, test_synonyms_and_common_labels);
    tcase_add_test(tc_mapping, test_empty_label_is_red);
    tcase_add_test(tc_mapping, test_no_black_or_white_for_labels);
    tcase_add_test(tc_mapping, test_unknown_labels_use_fallback_palette_and_are_stable);
    tcase_add_test(tc_mapping, test_tracking_ids_skip_black_and_white);

    TCase *tc_render = tcase_create("rendering");
    tcase_set_timeout(tc_render, 30);
    suite_add_tcase(s, tc_render);
    tcase_add_test(tc_render, test_render_uses_label_color);
    tcase_add_test(tc_render, test_render_ignores_class_descriptor_index);

    return s;
}

GST_CHECK_MAIN(watermark_label_colors);
