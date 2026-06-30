/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dobjectfuser.h"

#include "calibration.h"
#include "object_fuser_impl.h"

#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsbatchmeta.h>
#include <gst/base/gstbasetransform.h>

#include <dlstreamer/gst/metadata/g3d_od_mtd.h>
#include <dlstreamer/gst/metadata/g3d_radarprocess_meta.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <vas/ot.h>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_object_fuser_debug);
#define GST_CAT_DEFAULT gst_g3d_object_fuser_debug

namespace {

constexpr float DEFAULT_IOU_THRESHOLD = 0.3f;
constexpr unsigned DEFAULT_HISTORY_WINDOW = 30;
constexpr GstG3DFuserTrackingType DEFAULT_TRACKING_TYPE = GST_G3D_FUSER_TRACKING_ZERO_TERM_IMAGELESS;

/* The GstG3DFuserTrackingType enum values are passed straight to vas::ot, so
 * they must stay numerically aligned with vas::ot::TrackingType. */
static_assert(static_cast<int>(GST_G3D_FUSER_TRACKING_SHORT_TERM_IMAGELESS) ==
                  static_cast<int>(vas::ot::TrackingType::SHORT_TERM_IMAGELESS),
              "GstG3DFuserTrackingType must match vas::ot::TrackingType");
static_assert(static_cast<int>(GST_G3D_FUSER_TRACKING_ZERO_TERM_IMAGELESS) ==
                  static_cast<int>(vas::ot::TrackingType::ZERO_TERM_IMAGELESS),
              "GstG3DFuserTrackingType must match vas::ot::TrackingType");

/* A 2D detection fed to / returned from the internal tracker. */
struct TrackedDetection {
    cv::Rect rect; /* image-space rect (camera box or projected lidar box) */
    int class_label = -1;
    float confidence = 0.f;
    int64_t track_id = -1;    /* -1 until the tracker assigns one */
    int detection_index = -1; /* index into the caller's detection vector */
};

/* Thin wrapper around vas::ot::ObjectTracker for imageless tracking only.
 * Used for camera 2D boxes and for lidar boxes projected into image space.
 * The tracker still requires a frame argument, so a fixed dummy image is
 * passed — imageless algorithms ignore its pixels. */
class ModalityTracker {
  public:
    explicit ModalityTracker(vas::ot::TrackingType type) {
        vas::ot::ObjectTracker::Builder builder;
        builder.backend_type = vas::BackendType::CPU;
        builder.input_image_format = vas::ColorFormat::BGR;
        builder.max_num_objects = -1;
        builder.tracking_per_class = true;
        tracker_ = builder.Build(type);
    }

    void track(std::vector<TrackedDetection> &detections) {
        std::vector<vas::ot::DetectedObject> in_objs;
        in_objs.reserve(detections.size());
        for (std::size_t i = 0; i < detections.size(); ++i) {
            detections[i].detection_index = static_cast<int>(i);
            in_objs.emplace_back(detections[i].rect, detections[i].class_label);
        }

        if (dummy_mat_.empty())
            dummy_mat_ = cv::Mat(cv::Size(640, 480), CV_8UC3, cv::Scalar::all(0));

        auto tracked = tracker_->Track(dummy_mat_, in_objs);
        for (const auto &t : tracked) {
            if (t.status == vas::ot::TrackingStatus::LOST)
                continue;
            if (t.association_idx >= 0 && t.association_idx < static_cast<int32_t>(detections.size())) {
                detections[t.association_idx].track_id = static_cast<int64_t>(t.tracking_id);
                detections[t.association_idx].rect = t.rect;
            }
        }
    }

  private:
    std::unique_ptr<vas::ot::ObjectTracker> tracker_;
    cv::Mat dummy_mat_;
};

} // namespace

GType gst_g3d_fuser_tracking_type_get_type(void) {
    static gsize type_id = 0;
    static const GEnumValue values[] = {
        {GST_G3D_FUSER_TRACKING_SHORT_TERM_IMAGELESS, "Short-term imageless tracker", "short-term-imageless"},
        {GST_G3D_FUSER_TRACKING_ZERO_TERM_IMAGELESS, "Zero-term imageless tracker", "zero-term-imageless"},
        {0, NULL, NULL}};
    if (g_once_init_enter(&type_id)) {
        GType t = g_enum_register_static("GstG3DFuserTrackingType", values);
        g_once_init_leave(&type_id, t);
    }
    return (GType)type_id;
}

enum {
    PROP_0,
    PROP_CALIBRATION,
    PROP_ASSOC_IOU_THRESHOLD,
    PROP_TRACK_HISTORY_WINDOW,
    PROP_TRACKING_TYPE,
};

struct _GstG3DObjectFuserPrivate {
    dlstreamer::CalibrationStore calibration;
    std::unique_ptr<dlstreamer::ObjectFuser> fuser;

    /* Per-camera 2D trackers keyed by stream index. A new tracker is created
     * lazily for each camera index seen in the batch. */
    std::map<int, std::unique_ptr<ModalityTracker>> camera_trackers;

    /* Single tracker for the LiDAR stream, operating on 3D boxes projected to
     * one canonical camera's image plane so its track ids are stable across
     * all cameras. */
    std::unique_ptr<ModalityTracker> threed_tracker;

    bool calibration_loaded = false;
    /* Whether the sticky "g3d/calibration" event has been pushed downstream. */
    bool calibration_event_sent = false;
};

/* The fuser sits after gvastreammux, which has already merged the camera
 * stream(s) and the 3D-sensor stream into one container buffer
 * (multistream/x-analytics-batch). We consume that container in place. */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"));

static void gst_g3d_object_fuser_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_object_fuser_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_g3d_object_fuser_finalize(GObject *object);

static gboolean gst_g3d_object_fuser_start(GstBaseTransform *trans);
static gboolean gst_g3d_object_fuser_stop(GstBaseTransform *trans);
static GstFlowReturn gst_g3d_object_fuser_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

G_DEFINE_TYPE_WITH_PRIVATE(GstG3DObjectFuser, gst_g3d_object_fuser, GST_TYPE_BASE_TRANSFORM);

static void gst_g3d_object_fuser_class_init(GstG3DObjectFuserClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_g3d_object_fuser_debug, "g3dobjectfuser", 0,
                            "3D camera + radar/lidar object fusion element");

    gobject_class->set_property = gst_g3d_object_fuser_set_property;
    gobject_class->get_property = gst_g3d_object_fuser_get_property;
    gobject_class->finalize = gst_g3d_object_fuser_finalize;

    g_object_class_install_property(
        gobject_class, PROP_CALIBRATION,
        g_param_spec_string("calibration", "Calibration",
                            "Path to JSON file containing calibration matrices for camera <-> 3D sensor projection",
                            NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ASSOC_IOU_THRESHOLD,
        g_param_spec_float("assoc-iou-threshold", "Association IoU threshold",
                           "Minimum IoU between projected 3D box and 2D camera box for association", 0.0f, 1.0f,
                           DEFAULT_IOU_THRESHOLD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TRACK_HISTORY_WINDOW,
        g_param_spec_uint("track-history-window", "Track history window",
                          "Frames retained for the cross-modal track-to-track association table", 1, 1000,
                          DEFAULT_HISTORY_WINDOW, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_TRACKING_TYPE,
        g_param_spec_enum("tracking-type", "Tracking type",
                          "Tracking algorithm used to identify the same object in multiple frames.",
                          GST_TYPE_G3D_FUSER_TRACKING_TYPE, DEFAULT_TRACKING_TYPE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(gstelement_class, "G3D Object Fuser", "Filter/Analytics/3D",
                                          "Spatially associates camera 2D detections with radar/lidar 3D detections "
                                          "inside a gvastreammux batch (g3dobjectfuser)",
                                          "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_g3d_object_fuser_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_object_fuser_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_g3d_object_fuser_transform_ip);
}

static void gst_g3d_object_fuser_init(GstG3DObjectFuser *self) {
    self->calibration_path = nullptr;
    self->assoc_iou_threshold = DEFAULT_IOU_THRESHOLD;
    self->track_history_window = DEFAULT_HISTORY_WINDOW;
    self->tracking_type = DEFAULT_TRACKING_TYPE;

    self->priv = static_cast<GstG3DObjectFuserPrivate *>(gst_g3d_object_fuser_get_instance_private(self));
    new (self->priv) GstG3DObjectFuserPrivate();

    /* The container buffer is modified in place (metas added to its inner
     * stream buffers); we never change the container's memory or caps. */
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}

static void gst_g3d_object_fuser_finalize(GObject *object) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(object);
    g_free(self->calibration_path);
    self->priv->~GstG3DObjectFuserPrivate();
    G_OBJECT_CLASS(gst_g3d_object_fuser_parent_class)->finalize(object);
}

static void gst_g3d_object_fuser_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(object);
    switch (prop_id) {
    case PROP_CALIBRATION:
        g_free(self->calibration_path);
        self->calibration_path = g_value_dup_string(value);
        break;
    case PROP_ASSOC_IOU_THRESHOLD:
        self->assoc_iou_threshold = g_value_get_float(value);
        break;
    case PROP_TRACK_HISTORY_WINDOW:
        self->track_history_window = g_value_get_uint(value);
        break;
    case PROP_TRACKING_TYPE:
        self->tracking_type = static_cast<GstG3DFuserTrackingType>(g_value_get_enum(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_g3d_object_fuser_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(object);
    switch (prop_id) {
    case PROP_CALIBRATION:
        g_value_set_string(value, self->calibration_path);
        break;
    case PROP_ASSOC_IOU_THRESHOLD:
        g_value_set_float(value, self->assoc_iou_threshold);
        break;
    case PROP_TRACK_HISTORY_WINDOW:
        g_value_set_uint(value, self->track_history_window);
        break;
    case PROP_TRACKING_TYPE:
        g_value_set_enum(value, self->tracking_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_g3d_object_fuser_start(GstBaseTransform *trans) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(trans);

    if (!self->calibration_path || self->calibration_path[0] == '\0') {
        GST_ERROR_OBJECT(self, "calibration property is required");
        return FALSE;
    }

    if (!self->priv->calibration.load(self->calibration_path)) {
        GST_ERROR_OBJECT(self, "Failed to load calibration: %s", self->priv->calibration.last_error().c_str());
        return FALSE;
    }
    self->priv->calibration_loaded = true;

    self->priv->fuser = std::make_unique<dlstreamer::ObjectFuser>();
    self->priv->fuser->set_iou_threshold(self->assoc_iou_threshold);
    self->priv->fuser->set_history_window(self->track_history_window);
    self->priv->fuser->set_calibration_store(&self->priv->calibration);
    self->priv->calibration_event_sent = false;

    GST_INFO_OBJECT(self, "Started g3dobjectfuser; calibration=%s", self->calibration_path);
    return TRUE;
}

static gboolean gst_g3d_object_fuser_stop(GstBaseTransform *trans) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(trans);
    self->priv->camera_trackers.clear();
    self->priv->threed_tracker.reset();
    self->priv->fuser.reset();
    self->priv->calibration_loaded = false;
    self->priv->calibration_event_sent = false;
    return TRUE;
}

namespace {

/* Append a flat float array as a GST_TYPE_ARRAY field into @s. */
template <std::size_t N>
void add_matrix_field(GstStructure *s, const char *key, const std::array<float, N> &m) {
    GValue arr_val = G_VALUE_INIT;
    g_value_init(&arr_val, GST_TYPE_ARRAY);

    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_FLOAT);
    for (std::size_t i = 0; i < N; ++i) {
        g_value_set_float(&v, m[i]);
        gst_value_array_append_value(&arr_val, &v);
    }
    g_value_unset(&v);

    gst_structure_take_value(s, key, &arr_val);
}

/* Build the sticky "g3d/calibration" GstStructure carrying per-camera
 * projection matrices so a downstream renderer can reproject 3D boxes.
 *
 * Layout:
 *   name:      "g3d/calibration"
 *   modality:  "lidar" | "radar"
 *   cameras:   uint, number of camera entries
 *   camera-<idx> : GstStructure per camera, each holding either
 *                  { tr_velo_to_cam[16], r0_rect[16], p2[12] }  (lidar)
 *                  or { homography[9] }                         (radar)
 */
GstStructure *build_calibration_structure(const dlstreamer::CalibrationStore &store, bool is_lidar) {
    GstStructure *root = gst_structure_new_empty("g3d/calibration");
    gst_structure_set(root, "modality", G_TYPE_STRING, is_lidar ? "lidar" : "radar", NULL);

    const std::vector<int> indices = store.camera_indices();
    gst_structure_set(root, "cameras", G_TYPE_UINT, static_cast<guint>(indices.size()), NULL);

    for (int idx : indices) {
        const dlstreamer::CameraCalibration *cal = store.get(idx);
        if (!cal)
            continue;
        GstStructure *cam = gst_structure_new_empty("camera");
        gst_structure_set(cam, "index", G_TYPE_INT, idx, NULL);
        if (cal->has_lidar_calib) {
            add_matrix_field(cam, "tr_velo_to_cam", cal->tr_velo_to_cam);
            add_matrix_field(cam, "r0_rect", cal->r0_rect);
            add_matrix_field(cam, "p2", cal->p2);
        }
        if (cal->has_radar_calib) {
            add_matrix_field(cam, "homography", cal->homography);
        }

        char key[32];
        g_snprintf(key, sizeof(key), "camera-%d", idx);
        GValue cam_val = G_VALUE_INIT;
        g_value_init(&cam_val, GST_TYPE_STRUCTURE);
        g_value_take_boxed(&cam_val, cam);
        gst_structure_take_value(root, key, &cam_val);
    }

    return root;
}

void collect_camera_detections(GstBuffer *buf, std::vector<dlstreamer::Box2D> &out) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
    if (!rmeta)
        return;

    gpointer state = nullptr;
    GstAnalyticsODMtd od;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(), &od)) {
        gint x = 0, y = 0, w = 0, h = 0;
        gfloat conf = 0.f;
        if (!gst_analytics_od_mtd_get_location(&od, &x, &y, &w, &h, &conf))
            continue;

        dlstreamer::Box2D b;
        b.rect = cv::Rect2f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        b.confidence = conf;
        b.class_label = static_cast<int>(gst_analytics_od_mtd_get_obj_type(&od));
        b.mtd_id = od.id;
        out.push_back(b);
    }
}

bool collect_lidar_detections(GstBuffer *buf, std::vector<dlstreamer::Box3D> &out) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
    if (!rmeta)
        return false;

    bool found = false;
    gpointer state = nullptr;
    GstAnalytics3DODMtd od;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_3d_od_mtd_get_mtd_type(), &od)) {
        gfloat x = 0, y = 0, z = 0, length = 0, width = 0, height = 0, yaw = 0, pitch = 0, roll = 0;
        gint class_id = -1;
        gfloat conf = 0.f;
        if (!gst_analytics_3d_od_mtd_get_location(&od, &x, &y, &z, &length, &width, &height, &yaw, &pitch, &roll))
            continue;
        gst_analytics_3d_od_mtd_get_class(&od, &class_id, &conf);

        dlstreamer::Box3D b;
        b.x = x;
        b.y = y;
        b.z = z;
        b.length = length;
        b.width = width;
        b.height = height;
        b.yaw = yaw;
        b.confidence = conf;
        b.class_label = class_id;
        b.mtd_id = od.id; /* reuse g3dinference's existing 3D OD mtd */
        out.push_back(b);
        found = true;
    }
    return found;
}

bool collect_radar_detections(GstBuffer *buf, std::vector<dlstreamer::Box3D> &out) {
    GstRadarProcessMeta *rmeta =
        reinterpret_cast<GstRadarProcessMeta *>(gst_buffer_get_meta(buf, GST_RADAR_PROCESS_META_API_TYPE));
    if (!rmeta)
        return false;

    /* Radar reports only a tracked position (tracker_x/y), no extents. Assume a
     * fixed average-car footprint of 4.2 m (length) x 1.7 m (width). Height is
     * not used by the radar->image homography projection, 1.5 m is a nominal
     * vehicle height. */
    constexpr float kRadarBoxLength = 4.2f;
    constexpr float kRadarBoxWidth = 1.7f;
    constexpr float kRadarBoxHeight = 1.5f;
    for (gint i = 0; i < rmeta->num_tracked_objects; ++i) {
        dlstreamer::Box3D b;
        b.x = rmeta->tracker_x[i];
        b.y = rmeta->tracker_y[i];
        b.z = 0.f;
        b.length = kRadarBoxLength;
        b.width = kRadarBoxWidth;
        b.height = kRadarBoxHeight;
        b.yaw = 0.f;
        b.class_label = 0;
        b.confidence = 1.f;
        b.track_id = static_cast<int64_t>(rmeta->tracker_ids[i]);
        out.push_back(b);
    }
    return true;
}

/* Ensure each 3D box has a GstAnalytics3DODMtd on @rmeta and, when it has a
 * track id, a #GstAnalyticsTrackingMtd linked via GST_ANALYTICS_REL_TYPE_RELATE_TO.
 * Boxes that already carry a mtd id (LiDAR, emitted by g3dinference) reuse it;
 * boxes without one (radar, from GstRadarProcessMeta) get a freshly added mtd.
 * mtd_ids_out[i] holds each box's 3D OD mtd id (kInvalidMtdId on failure). */
void emit_3d_mtds(GstAnalyticsRelationMeta *rmeta, const std::vector<dlstreamer::Box3D> &boxes, GstClockTime pts,
                  GstAnalytics3DSensorModality modality, std::vector<guint> &mtd_ids_out) {
    mtd_ids_out.assign(boxes.size(), dlstreamer::kInvalidMtdId);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto &b = boxes[i];
        guint mtd_id = b.mtd_id;
        if (mtd_id == dlstreamer::kInvalidMtdId) {
            GstAnalytics3DODMtd mtd;
            if (!gst_analytics_relation_meta_add_3d_od_mtd(rmeta, b.x, b.y, b.z, b.length, b.width, b.height, b.yaw,
                                                           /*pitch=*/0.f, /*roll=*/0.f, b.class_label, b.confidence,
                                                           modality, &mtd))
                continue;
            mtd_id = mtd.id;
        }
        mtd_ids_out[i] = mtd_id;

        if (b.track_id >= 0) {
            GstAnalyticsTrackingMtd tmtd;
            if (gst_analytics_relation_meta_add_tracking_mtd(rmeta, static_cast<guint64>(b.track_id), pts, &tmtd)) {
                gst_analytics_tracking_mtd_update_last_seen(&tmtd, pts);
                gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_RELATE_TO, mtd_id, tmtd.id);
            }
        }
    }
}

/* Ensure the inner stream buffer is writable so metas can be added to it.
 * gvastreammux transfers sole ownership of each source buffer into the batch
 * meta, so it is normally writable; guard and store back the (possibly new)
 * buffer pointer to be safe. */
GstBuffer *ensure_writable_stream_buffer(GstAnalyticsBatchStream *stream) {
    if (stream->n_objects == 0 || !GST_IS_BUFFER(stream->objects[0]))
        return nullptr;
    GstBuffer *buf = GST_BUFFER_CAST(stream->objects[0]);
    if (!gst_buffer_is_writable(buf)) {
        buf = gst_buffer_make_writable(buf);
        stream->objects[0] = GST_MINI_OBJECT_CAST(buf);
    }
    return buf;
}

} // namespace

/* Run camera tracking and write per-camera tracking mtds + cross-modal
 * IS_PART_OF relations onto the camera buffer's own GstAnalyticsRelationMeta.
 * 3D mtds live on the 3D buffer's relation meta and are referenced by id from
 * across the stream boundary. */
static void process_one_camera(GstG3DObjectFuser *self, GstBuffer *cam_buf, int camera_index, bool is_lidar,
                               std::vector<dlstreamer::Box3D> &threed_boxes, const std::vector<guint> &threed_mtd_ids) {
    std::vector<dlstreamer::Box2D> cam_boxes;
    collect_camera_detections(cam_buf, cam_boxes);

    /* Per-camera 2D tracker, keyed by camera_index so each camera maintains its
     * own ID space. */
    auto &cam_tracker = self->priv->camera_trackers[camera_index];
    if (!cam_tracker)
        cam_tracker = std::make_unique<ModalityTracker>(static_cast<vas::ot::TrackingType>(self->tracking_type));

    std::vector<TrackedDetection> cam_track_in;
    cam_track_in.reserve(cam_boxes.size());
    for (std::size_t i = 0; i < cam_boxes.size(); ++i) {
        TrackedDetection td;
        td.rect = cv::Rect(static_cast<int>(cam_boxes[i].rect.x), static_cast<int>(cam_boxes[i].rect.y),
                           static_cast<int>(cam_boxes[i].rect.width), static_cast<int>(cam_boxes[i].rect.height));
        td.class_label = cam_boxes[i].class_label;
        td.confidence = cam_boxes[i].confidence;
        td.detection_index = static_cast<int>(i);
        cam_track_in.push_back(td);
    }
    cam_tracker->track(cam_track_in);
    for (const auto &t : cam_track_in)
        if (t.detection_index >= 0)
            cam_boxes[t.detection_index].track_id = t.track_id;

    auto fres = self->priv->fuser->fuse(camera_index, cam_boxes, threed_boxes, is_lidar);

    int n_assoc = 0;
    for (int c : fres.camera_to_3d)
        if (c >= 0)
            ++n_assoc;
    GST_DEBUG_OBJECT(self, "camera_index=%d: %zu cam dets, %zu 3D dets, %d cross-modal associations", camera_index,
                     cam_boxes.size(), threed_boxes.size(), n_assoc);

    GstAnalyticsRelationMeta *cam_rmeta = gst_buffer_get_analytics_relation_meta(cam_buf);
    if (!cam_rmeta)
        cam_rmeta = gst_buffer_add_analytics_relation_meta(cam_buf);
    GstClockTime cam_pts = GST_BUFFER_PTS_IS_VALID(cam_buf) ? GST_BUFFER_PTS(cam_buf) : GST_CLOCK_TIME_NONE;

    /* Tracking: attach a GstAnalyticsTrackingMtd directly to each existing
     * GstAnalyticsODMtd from gvadetect. */
    for (std::size_t i = 0; i < cam_boxes.size(); ++i) {
        const auto &cb = cam_boxes[i];
        if (cb.mtd_id == dlstreamer::kInvalidMtdId || cb.track_id < 0)
            continue;
        GstAnalyticsTrackingMtd tmtd;
        if (gst_analytics_relation_meta_add_tracking_mtd(cam_rmeta, static_cast<guint64>(cb.track_id), cam_pts,
                                                         &tmtd)) {
            gst_analytics_tracking_mtd_update_last_seen(&tmtd, cam_pts);
            gst_analytics_relation_meta_set_relation(cam_rmeta, GST_ANALYTICS_REL_TYPE_RELATE_TO, cb.mtd_id, tmtd.id);
        }
    }

    /* Cross-modal IS_PART_OF: relation ids are scoped to a single
     * GstAnalyticsRelationMeta, so a camera OD mtd cannot directly point at a
     * 3D OD mtd that lives on the 3D-sensor buffer's relation meta. We
     * materialise the cross-stream link as a tracking mtd on the camera side
     * whose tracking_id is the 3D OD mtd's id; downstream readers look that id
     * up on the 3D-sensor stream's relation meta. */
    for (std::size_t r = 0; r < fres.camera_to_3d.size(); ++r) {
        int c = fres.camera_to_3d[r];
        if (c < 0)
            continue;
        if (cam_boxes[r].mtd_id == dlstreamer::kInvalidMtdId || threed_mtd_ids[c] == dlstreamer::kInvalidMtdId)
            continue;
        GstAnalyticsTrackingMtd link_mtd;
        if (gst_analytics_relation_meta_add_tracking_mtd(cam_rmeta, static_cast<guint64>(threed_mtd_ids[c]), cam_pts,
                                                         &link_mtd)) {
            gst_analytics_relation_meta_set_relation(cam_rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, cam_boxes[r].mtd_id,
                                                     link_mtd.id);
        }
    }
}

static GstFlowReturn gst_g3d_object_fuser_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstG3DObjectFuser *self = GST_G3D_OBJECT_FUSER(trans);

    GstAnalyticsBatchMeta *batch = gst_buffer_get_analytics_batch_meta(buf);
    if (!batch || batch->n_streams == 0) {
        GST_WARNING_OBJECT(self, "Buffer has no GstAnalyticsBatchMeta; passing through unchanged");
        return GST_FLOW_OK;
    }

    /* Classify the batch's streams into camera (video) buffers and the single
     * 3D-sensor (lidar/radar) buffer, keyed by each stream's caps. */
    std::vector<GstBuffer *> cam_bufs;
    std::vector<int> cam_indices;
    GstBuffer *threed_buf = nullptr;
    bool is_lidar = false;
    bool have_3d = false;

    for (gsize i = 0; i < batch->n_streams; ++i) {
        GstAnalyticsBatchStream *stream = &batch->streams[i];
        GstBuffer *sbuf = ensure_writable_stream_buffer(stream);
        if (!sbuf)
            continue;

        GstCaps *caps = gst_analytics_batch_stream_get_caps(stream);
        const gchar *name =
            (caps && gst_caps_get_size(caps) > 0) ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : nullptr;
        if (name && g_str_has_prefix(name, "video/")) {
            cam_bufs.push_back(sbuf);
            cam_indices.push_back(static_cast<int>(stream->index));
        } else if (name && g_strcmp0(name, "application/x-lidar") == 0) {
            threed_buf = sbuf;
            is_lidar = true;
            have_3d = true;
        } else if (name && g_strcmp0(name, "application/x-radar-processed") == 0) {
            threed_buf = sbuf;
            is_lidar = false;
            have_3d = true;
        }
    }

    /* Extract 3D detections and run per-modality 3D tracking on the 3D stream. */
    std::vector<dlstreamer::Box3D> threed_boxes;
    std::vector<guint> threed_mtd_ids;
    if (have_3d) {
        if (is_lidar)
            collect_lidar_detections(threed_buf, threed_boxes);
        else
            collect_radar_detections(threed_buf, threed_boxes);

        /* For lidar, track in 2D image space: project each 3D box's 8 corners to
         * the camera image, take the bounding rect, and feed those rects to the
         * same vas::ot tracker the camera path uses. Project with one canonical
         * camera's calibration (the lowest stream index present) so the LiDAR
         * track-id space is a single consistent frame across all cameras. */
        if (is_lidar) {
            if (!self->priv->threed_tracker)
                self->priv->threed_tracker =
                    std::make_unique<ModalityTracker>(static_cast<vas::ot::TrackingType>(self->tracking_type));

            const std::vector<int> cam_calib_indices = self->priv->calibration.camera_indices();
            const int canonical_cam = cam_calib_indices.empty() ? 0 : cam_calib_indices.front();
            const dlstreamer::CameraCalibration *cal = self->priv->calibration.get(canonical_cam);

            std::vector<TrackedDetection> three_in;
            three_in.reserve(threed_boxes.size());
            for (std::size_t i = 0; i < threed_boxes.size(); ++i) {
                cv::Rect2f proj;
                if (!cal || !dlstreamer::ObjectFuser::project_lidar_box_to_image(threed_boxes[i], *cal, proj))
                    continue; /* box behind the image plane or no calibration */
                TrackedDetection td;
                td.rect =
                    cv::Rect(static_cast<int>(proj.x), static_cast<int>(proj.y),
                             std::max(1, static_cast<int>(proj.width)), std::max(1, static_cast<int>(proj.height)));
                td.class_label = threed_boxes[i].class_label;
                td.confidence = threed_boxes[i].confidence;
                td.detection_index = static_cast<int>(i);
                three_in.push_back(td);
            }
            self->priv->threed_tracker->track(three_in);
            for (const auto &t : three_in)
                if (t.detection_index >= 0)
                    threed_boxes[t.detection_index].track_id = t.track_id;
        }
        /* Radar: tracker_ids are already populated by g3dradarprocess. */

        GstClockTime threed_pts =
            GST_BUFFER_PTS_IS_VALID(threed_buf) ? GST_BUFFER_PTS(threed_buf) : GST_CLOCK_TIME_NONE;
        GstAnalyticsRelationMeta *threed_rmeta = gst_buffer_get_analytics_relation_meta(threed_buf);
        if (!threed_rmeta)
            threed_rmeta = gst_buffer_add_analytics_relation_meta(threed_buf);
        GstAnalytics3DSensorModality modality =
            is_lidar ? GST_ANALYTICS_3D_SENSOR_LIDAR : GST_ANALYTICS_3D_SENSOR_RADAR;
        emit_3d_mtds(threed_rmeta, threed_boxes, threed_pts, modality, threed_mtd_ids);

        /* Push the calibration as a sticky downstream event exactly once, now
         * that the sensor modality is known. Downstream caches it to reproject
         * the 3D boxes back onto each camera image. */
        if (!self->priv->calibration_event_sent) {
            GstStructure *cal_s = build_calibration_structure(self->priv->calibration, is_lidar);
            GstEvent *cal_ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, cal_s);
            gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(trans), cal_ev);
            self->priv->calibration_event_sent = true;
            GST_INFO_OBJECT(self, "Pushed g3d/calibration sticky event downstream");
        }
    }

    /* Per camera: tracking + cross-modal relations onto its own relation meta.
     * Video-only batches (no coincident 3D frame) still get camera tracking so
     * track IDs stay continuous; fusion is simply a no-op with no 3D boxes. */
    for (std::size_t i = 0; i < cam_bufs.size(); ++i)
        process_one_camera(self, cam_bufs[i], cam_indices[i], is_lidar, threed_boxes, threed_mtd_ids);

    return GST_FLOW_OK;
}
