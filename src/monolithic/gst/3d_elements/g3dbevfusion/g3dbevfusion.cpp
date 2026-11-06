/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * SECTION:element-g3dbevfusion
 *
 * Runs BEVFusion 3D object detection on a gvastreammux container batch that
 * carries six surround-view cameras (BGR) and one lidar stream
 * (application/x-lidar from g3dlidarparse).
 *
 * The calibration file names which mux pad carries which sensor. With six
 * cameras declared, a batch with the lidar and all six cameras runs
 * camera+lidar fusion and a batch whose cameras are missing runs lidar-only for
 * that frame; with no cameras declared every batch runs lidar-only. A batch
 * without the lidar passes through without detections (BEVFusion has no
 * camera-only mode). Detections are
 * attached to the lidar stream buffer as GstAnalytics3DODMtd, and a sticky
 * "g3d/calibration" event lets g3drender project them onto the camera images.
 *
 * |[
 * gst-launch-1.0 \
 *   multifilesrc location=CAM_FRONT/%06d.jpg caps=image/jpeg,framerate=2/1 ! jpegdec ! videoconvert ! \
 *     video/x-raw,format=BGR ! mux.sink_0  ...  (CAM_* on mux.sink_1 .. mux.sink_5) \
 *   multifilesrc location=LIDAR_TOP/%06d.bin caps=application/octet-stream ! g3dlidarparse frame-rate=2 ! mux.sink_6 \
 *   gvastreammux name=mux output-mode=container sync-mode=first-pts ! \
 *   g3dbevfusion config=g3dbevfusion.json calibration=calibration.json ! g3drender ! fakesink
 * ]|
 */

#include "g3dbevfusion.h"

#include "bevfusion_runtime.h"
#include "g3d_lidar_meta.h"
#include "g3d_od_mtd.h"

#include <gst/analytics/analytics.h>
#include <gst/video/video.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <string>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_bevfusion_debug);
#define GST_CAT_DEFAULT gst_g3d_bevfusion_debug

namespace bev = g3d_bevfusion;

#define DEFAULT_DEVICE "GPU"
#define DEFAULT_SCORE_THRESHOLD 0.3f

enum { PROP_0, PROP_CONFIG, PROP_CALIBRATION, PROP_DEVICE, PROP_SCORE_THRESHOLD };

struct _GstG3DBevFusionPrivate {
    std::unique_ptr<bev::Runtime> runtime;
    // Which mux pad (GstAnalyticsBatchStream::index) carries which sensor.
    std::map<guint, int> camera_slot_by_pad;
    guint lidar_pad = 0;
    std::array<bev::CameraCalibration, bev::kNumCameras> cameras{};
    bool calibration_event_sent = false;
    guint64 frames_fusion = 0;
    guint64 frames_lidar_only = 0;
    guint64 frames_without_lidar = 0;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));

G_DEFINE_TYPE_WITH_PRIVATE(GstG3DBevFusion, gst_g3d_bevfusion, GST_TYPE_BASE_TRANSFORM);

namespace {

/* Load the sensor topology and calibration:
 *   { "lidar":   { "pad": <int> },
 *     "cameras": { "<pad>": { "slot": 0..5, "name": str,
 *                             "camera2lidar": [16, row-major], "intrinsics": [9, row-major] }, ... } }
 * "cameras" either lists all six model camera slots exactly once, or is absent
 * or empty for a lidar-only rig. */
void load_calibration(const std::string &path, GstG3DBevFusionPrivate *priv) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot open calibration file " + path);
    const nlohmann::json cfg = nlohmann::json::parse(f);

    priv->lidar_pad = cfg.at("lidar").at("pad").get<guint>();
    priv->camera_slot_by_pad.clear();
    std::array<bool, bev::kNumCameras> seen{};
    const nlohmann::json cameras = cfg.value("cameras", nlohmann::json::object());
    for (const auto &[pad, cam] : cameras.items()) {
        const int slot = cam.at("slot").get<int>();
        if (slot < 0 || slot >= bev::kNumCameras || seen[slot])
            throw std::runtime_error("calibration: camera slot " + std::to_string(slot) +
                                     " is out of range or repeated");
        seen[slot] = true;
        const guint pad_index = static_cast<guint>(std::stoul(pad));
        if (pad_index == priv->lidar_pad)
            throw std::runtime_error("calibration: pad " + pad + " is declared as both a camera and the lidar");
        priv->camera_slot_by_pad[pad_index] = slot;

        bev::CameraCalibration &c = priv->cameras[slot];
        c.name = cam.value("name", "camera" + std::to_string(slot));
        const auto c2l = cam.at("camera2lidar").get<std::vector<double>>();
        const auto k = cam.at("intrinsics").get<std::vector<double>>();
        if (c2l.size() != 16 || k.size() != 9)
            throw std::runtime_error("calibration: camera " + pad + " needs 16 camera2lidar and 9 intrinsics values");
        for (int i = 0; i < 16; i++)
            c.camera2lidar[i] = static_cast<float>(c2l[i]);
        for (int i = 0; i < 9; i++)
            c.intrinsics[i] = static_cast<float>(k[i]);
    }
    if (priv->camera_slot_by_pad.empty())
        return; // lidar-only rig
    for (int s = 0; s < bev::kNumCameras; s++)
        if (!seen[s])
            throw std::runtime_error("calibration: BEVFusion's camera branch needs " +
                                     std::to_string(bev::kNumCameras) + " cameras (slots 0-5), slot " +
                                     std::to_string(s) + " is missing; declare no cameras for a lidar-only rig");
}

template <std::size_t N>
void add_float_array(GstStructure *s, const char *key, const std::array<float, N> &m) {
    GValue arr = G_VALUE_INIT;
    g_value_init(&arr, GST_TYPE_ARRAY);
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_FLOAT);
    for (std::size_t i = 0; i < N; ++i) {
        g_value_set_float(&v, m[i]);
        gst_value_array_append_value(&arr, &v);
    }
    g_value_unset(&v);
    gst_structure_take_value(s, key, &arr);
}

/* The "g3d/calibration" event in the layout g3dobjectfuser uses, so g3drender
 * can consume either. BEVFusion's camera2lidar + K map onto it as
 * tr_velo_to_cam = inverse(camera2lidar), r0_rect = I, p2 = [K | 0].
 * class-names lists the label of each GstAnalytics3DODMtd class_id. */
GstStructure *build_calibration_event(const GstG3DBevFusionPrivate *priv, const bev::ModelParams &params) {
    GstStructure *root = gst_structure_new_empty("g3d/calibration");
    gst_structure_set(root, "modality", G_TYPE_STRING, "lidar", "cameras", G_TYPE_UINT,
                      static_cast<guint>(priv->camera_slot_by_pad.size()), NULL);

    for (const auto &[pad, slot] : priv->camera_slot_by_pad) {
        const bev::CameraCalibration &c = priv->cameras[slot];
        const bev::Mat4 &m = c.camera2lidar;
        // Rigid inverse: [R^T, -R^T t].
        std::array<float, 16> tr{};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++)
                tr[i * 4 + j] = m[j * 4 + i];
            tr[i * 4 + 3] = -(m[0 * 4 + i] * m[3] + m[1 * 4 + i] * m[7] + m[2 * 4 + i] * m[11]);
        }
        tr[15] = 1.0f;
        const std::array<float, 16> r0 = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const bev::Mat3 &k = c.intrinsics;
        const std::array<float, 12> p2 = {k[0], k[1], k[2], 0, k[3], k[4], k[5], 0, k[6], k[7], k[8], 0};

        GstStructure *cam = gst_structure_new("camera", "index", G_TYPE_INT, static_cast<gint>(pad), "name",
                                              G_TYPE_STRING, c.name.c_str(), NULL);
        add_float_array(cam, "tr_velo_to_cam", tr);
        add_float_array(cam, "r0_rect", r0);
        add_float_array(cam, "p2", p2);
        char key[32];
        g_snprintf(key, sizeof(key), "camera-%u", pad);
        GValue cam_val = G_VALUE_INIT;
        g_value_init(&cam_val, GST_TYPE_STRUCTURE);
        g_value_take_boxed(&cam_val, cam);
        gst_structure_take_value(root, key, &cam_val);
    }

    GValue names = G_VALUE_INIT;
    g_value_init(&names, GST_TYPE_ARRAY);
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_STRING);
    for (const std::string &n : params.class_names) {
        g_value_set_string(&v, n.c_str());
        gst_value_array_append_value(&names, &v);
    }
    g_value_unset(&v);
    gst_structure_take_value(root, "class-names", &names);
    return root;
}

const gchar *stream_media_type(GstAnalyticsBatchStream *stream) {
    GstCaps *caps = gst_analytics_batch_stream_get_caps(stream);
    return (caps && gst_caps_get_size(caps) > 0) ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : nullptr;
}

GstBuffer *stream_buffer(GstAnalyticsBatchStream *stream) {
    if (stream->n_objects == 0 || !GST_IS_BUFFER(stream->objects[0]))
        return nullptr;
    return GST_BUFFER_CAST(stream->objects[0]);
}

// Holds the six mapped camera frames for the duration of one inference. Not
// copyable or movable: a copy would unmap the same frames twice.
struct CameraFrames {
    std::array<GstVideoFrame, bev::kNumCameras> frames{};
    std::array<bool, bev::kNumCameras> mapped{};
    CameraFrames() = default;
    CameraFrames(const CameraFrames &) = delete;
    CameraFrames &operator=(const CameraFrames &) = delete;
    CameraFrames(CameraFrames &&) = delete;
    CameraFrames &operator=(CameraFrames &&) = delete;
    ~CameraFrames() {
        for (int i = 0; i < bev::kNumCameras; i++)
            if (mapped[i])
                gst_video_frame_unmap(&frames[i]);
    }
};

} // namespace

static void gst_g3d_bevfusion_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(object);
    switch (prop_id) {
    case PROP_CONFIG:
        g_free(self->config);
        self->config = g_value_dup_string(value);
        break;
    case PROP_CALIBRATION:
        g_free(self->calibration);
        self->calibration = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(self->device);
        self->device = g_value_dup_string(value);
        break;
    case PROP_SCORE_THRESHOLD:
        self->score_threshold = g_value_get_float(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void gst_g3d_bevfusion_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(object);
    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_string(value, self->config);
        break;
    case PROP_CALIBRATION:
        g_value_set_string(value, self->calibration);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, self->device);
        break;
    case PROP_SCORE_THRESHOLD:
        g_value_set_float(value, self->score_threshold);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static gboolean gst_g3d_bevfusion_start(GstBaseTransform *trans) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(trans);
    GstG3DBevFusionPrivate *priv = self->priv;
    if (!self->config || !*self->config || !self->calibration || !*self->calibration) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("The config and calibration properties are required"), (NULL));
        return FALSE;
    }
    const gchar *device = (self->device && *self->device) ? self->device : DEFAULT_DEVICE;
    try {
        load_calibration(self->calibration, priv);
        priv->runtime = std::make_unique<bev::Runtime>(self->config, device);
        if (!priv->camera_slot_by_pad.empty())
            priv->runtime->set_calibration(priv->cameras);
        // Debug aid: per-frame boxes (and with G3DBEVFUSION_DUMP_TENSORS=1 every
        // stage's tensors, ~90 MB a frame) as .npy files.
        if (const char *dump = g_getenv("G3DBEVFUSION_DUMP_DIR"))
            priv->runtime->set_dump_dir(dump, g_strcmp0(g_getenv("G3DBEVFUSION_DUMP_TENSORS"), "1") == 0);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("Failed to initialise BEVFusion"), ("%s", e.what()));
        priv->runtime.reset();
        return FALSE;
    }
    priv->calibration_event_sent = false;
    priv->frames_fusion = priv->frames_lidar_only = priv->frames_without_lidar = 0;
    GST_INFO_OBJECT(self, "BEVFusion ready on %s; lidar on pad %u, %zu cameras", device, priv->lidar_pad,
                    priv->camera_slot_by_pad.size());
    return TRUE;
}

static gboolean gst_g3d_bevfusion_stop(GstBaseTransform *trans) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(trans);
    GstG3DBevFusionPrivate *priv = self->priv;
    GST_INFO_OBJECT(self,
                    "Processed %" G_GUINT64_FORMAT " camera+lidar frames, %" G_GUINT64_FORMAT
                    " lidar-only frames, %" G_GUINT64_FORMAT " frames without lidar",
                    priv->frames_fusion, priv->frames_lidar_only, priv->frames_without_lidar);
    priv->runtime.reset();
    return TRUE;
}

static GstFlowReturn gst_g3d_bevfusion_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(trans);
    GstG3DBevFusionPrivate *priv = self->priv;

    if (!priv->calibration_event_sent) {
        GstStructure *s = build_calibration_event(priv, priv->runtime->params());
        gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(trans),
                           gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, s));
        priv->calibration_event_sent = true;
    }

    GstAnalyticsBatchMeta *batch = gst_buffer_get_analytics_batch_meta(buf);
    if (!batch) {
        GST_WARNING_OBJECT(self, "Buffer has no GstAnalyticsBatchMeta; passing it through");
        return GST_FLOW_OK;
    }

    GstAnalyticsBatchStream *lidar_stream = nullptr;
    std::array<GstAnalyticsBatchStream *, bev::kNumCameras> camera_streams{};
    for (gsize i = 0; i < batch->n_streams; ++i) {
        GstAnalyticsBatchStream *stream = &batch->streams[i];
        // A stream without a buffer this batch counts as missing.
        if (!stream_buffer(stream))
            continue;
        const gchar *type = stream_media_type(stream);
        if (stream->index == priv->lidar_pad && g_strcmp0(type, "application/x-lidar") == 0) {
            lidar_stream = stream;
        } else if (type && g_str_has_prefix(type, "video/")) {
            auto it = priv->camera_slot_by_pad.find(stream->index);
            if (it != priv->camera_slot_by_pad.end())
                camera_streams[it->second] = stream;
        }
    }

    if (!lidar_stream) {
        // BEVFusion needs the lidar in both of its modes.
        if (priv->frames_without_lidar++ == 0)
            GST_WARNING_OBJECT(self,
                               "Batch at %" GST_TIME_FORMAT " has no lidar (pad %u); passing it through "
                               "without detections (further occurrences are counted, see the summary at stop)",
                               GST_TIME_ARGS(GST_BUFFER_PTS(buf)), priv->lidar_pad);
        return GST_FLOW_OK;
    }

    /* gvastreammux hands each source buffer's only reference to the batch, so
     * the lidar buffer is normally writable already. */
    GstBuffer *lidar_buf = stream_buffer(lidar_stream);
    if (!lidar_buf)
        return GST_FLOW_OK;
    if (!gst_buffer_is_writable(lidar_buf)) {
        lidar_buf = gst_buffer_make_writable(lidar_buf);
        lidar_stream->objects[0] = GST_MINI_OBJECT_CAST(lidar_buf);
    }
    LidarMeta *lidar_meta = reinterpret_cast<LidarMeta *>(gst_buffer_get_meta(lidar_buf, LIDAR_META_API_TYPE));
    if (!lidar_meta) {
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Lidar buffer has no LidarMeta"),
                          ("Feed the lidar through g3dlidarparse or g3dlidarsrc before gvastreammux"));
        return GST_FLOW_ERROR;
    }

    bool all_cameras = true;
    for (GstAnalyticsBatchStream *s : camera_streams)
        all_cameras = all_cameras && s;

    GstMapInfo lidar_map;
    if (!gst_buffer_map(lidar_buf, &lidar_map, GST_MAP_READ)) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to map the lidar buffer"), (NULL));
        return GST_FLOW_ERROR;
    }
    const size_t num_points = lidar_meta->lidar_point_count;
    if (lidar_map.size < num_points * 4 * sizeof(float)) {
        gst_buffer_unmap(lidar_buf, &lidar_map);
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Lidar buffer is smaller than its LidarMeta point count"),
                          ("%zu bytes for %zu points", lidar_map.size, num_points));
        return GST_FLOW_ERROR;
    }

    bev::FrameInput in;
    in.points = reinterpret_cast<const float *>(lidar_map.data);
    in.num_points = num_points;
    in.point_stride = 4;

    CameraFrames frames;
    if (all_cameras) {
        std::array<bev::ImageView, bev::kNumCameras> views{};
        for (int slot = 0; slot < bev::kNumCameras; slot++) {
            GstAnalyticsBatchStream *stream = camera_streams[slot];
            GstVideoInfo info;
            GstCaps *caps = gst_analytics_batch_stream_get_caps(stream);
            if (!caps || !gst_video_info_from_caps(&info, caps) ||
                GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_BGR ||
                gst_caps_features_contains(gst_caps_get_features(caps, 0), "memory:VAMemory")) {
                gst_buffer_unmap(lidar_buf, &lidar_map);
                GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Cameras must be BGR in system memory"),
                                  ("pad %u has caps %" GST_PTR_FORMAT, stream->index, caps));
                return GST_FLOW_ERROR;
            }
            GstBuffer *cam_buf = stream_buffer(stream);
            if (!cam_buf || !gst_video_frame_map(&frames.frames[slot], &info, cam_buf, GST_MAP_READ)) {
                gst_buffer_unmap(lidar_buf, &lidar_map);
                GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to map camera frame on pad %u", stream->index),
                                  (NULL));
                return GST_FLOW_ERROR;
            }
            frames.mapped[slot] = true;
            views[slot] = {static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frames.frames[slot], 0)),
                           GST_VIDEO_FRAME_WIDTH(&frames.frames[slot]), GST_VIDEO_FRAME_HEIGHT(&frames.frames[slot]),
                           static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&frames.frames[slot], 0))};
        }
        in.cameras = views;
        priv->frames_fusion++;
    } else {
        if (priv->frames_lidar_only++ == 0 && !priv->camera_slot_by_pad.empty())
            GST_WARNING_OBJECT(self,
                               "Batch at %" GST_TIME_FORMAT " is missing cameras; running lidar-only for it "
                               "(further occurrences are counted, see the summary at stop)",
                               GST_TIME_ARGS(GST_BUFFER_PTS(buf)));
    }

    std::vector<bev::Box3D> boxes;
    try {
        boxes = priv->runtime->infer(in, self->score_threshold);
    } catch (const std::exception &e) {
        gst_buffer_unmap(lidar_buf, &lidar_map);
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("BEVFusion inference failed"), ("%s", e.what()));
        return GST_FLOW_ERROR;
    }
    gst_buffer_unmap(lidar_buf, &lidar_map);

    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(lidar_buf);
    if (!rmeta)
        rmeta = gst_buffer_add_analytics_relation_meta(lidar_buf);
    for (const bev::Box3D &b : boxes) {
        GstAnalytics3DODMtd mtd;
        // BEVFusion reports z at the box bottom; the mtd stores the centre.
        if (!gst_analytics_relation_meta_add_3d_od_mtd(rmeta, b.x, b.y, b.z_bottom + 0.5f * b.dz, /*length=*/b.dx,
                                                       /*width=*/b.dy, /*height=*/b.dz, b.yaw, 0.0f, 0.0f, b.label,
                                                       b.score, GST_ANALYTICS_3D_SENSOR_LIDAR, &mtd))
            GST_WARNING_OBJECT(self, "Failed to attach a 3D detection");
    }
    GST_LOG_OBJECT(self, "%s frame at %" GST_TIME_FORMAT ": %zu detections", in.cameras ? "fusion" : "lidar-only",
                   GST_TIME_ARGS(GST_BUFFER_PTS(buf)), boxes.size());
    return GST_FLOW_OK;
}

static void gst_g3d_bevfusion_finalize(GObject *object) {
    GstG3DBevFusion *self = GST_G3D_BEVFUSION(object);
    g_free(self->config);
    g_free(self->calibration);
    g_free(self->device);
    self->priv->~GstG3DBevFusionPrivate();
    G_OBJECT_CLASS(gst_g3d_bevfusion_parent_class)->finalize(object);
}

static void gst_g3d_bevfusion_class_init(GstG3DBevFusionClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_g3d_bevfusion_debug, "g3dbevfusion", 0, "BEVFusion camera+lidar 3D detection");

    gobject_class->set_property = gst_g3d_bevfusion_set_property;
    gobject_class->get_property = gst_g3d_bevfusion_get_property;
    gobject_class->finalize = gst_g3d_bevfusion_finalize;

    const GParamFlags flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_string("config", "Config",
                                                        "Runtime JSON: model_dir, extensions and gpu_custom_layers "
                                                        "of the OpenVINO BEVFusion model",
                                                        NULL, flags));
    g_object_class_install_property(gobject_class, PROP_CALIBRATION,
                                    g_param_spec_string("calibration", "Calibration",
                                                        "JSON naming the lidar's and each camera's mux pad, with each "
                                                        "camera's model slot, camera2lidar and intrinsics",
                                                        NULL, flags));
    g_object_class_install_property(gobject_class, PROP_DEVICE,
                                    g_param_spec_string("device", "Device",
                                                        "Device for the dense networks (camera encoder, geometry, "
                                                        "detection head); the custom voxelize/bev_pool ops always run "
                                                        "on the GPU",
                                                        DEFAULT_DEVICE, flags));
    g_object_class_install_property(gobject_class, PROP_SCORE_THRESHOLD,
                                    g_param_spec_float("score-threshold", "Score threshold",
                                                       "Minimum detection score to attach", 0.0f, 1.0f,
                                                       DEFAULT_SCORE_THRESHOLD, flags));

    gst_element_class_set_static_metadata(element_class, "G3D BEVFusion", "Filter/Analytics/3D",
                                          "BEVFusion 3D object detection on a gvastreammux batch of six cameras and "
                                          "one lidar (lidar-only when cameras are missing)",
                                          "Intel Corporation");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    base_class->start = GST_DEBUG_FUNCPTR(gst_g3d_bevfusion_start);
    base_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_bevfusion_stop);
    base_class->transform_ip = GST_DEBUG_FUNCPTR(gst_g3d_bevfusion_transform_ip);
}

static void gst_g3d_bevfusion_init(GstG3DBevFusion *self) {
    self->config = nullptr;
    self->calibration = nullptr;
    self->device = g_strdup(DEFAULT_DEVICE);
    self->score_threshold = DEFAULT_SCORE_THRESHOLD;
    self->priv = static_cast<GstG3DBevFusionPrivate *>(gst_g3d_bevfusion_get_instance_private(self));
    new (self->priv) GstG3DBevFusionPrivate();
    // Detections are attached to the batch's inner lidar buffer; the container
    // itself is not changed.
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}
