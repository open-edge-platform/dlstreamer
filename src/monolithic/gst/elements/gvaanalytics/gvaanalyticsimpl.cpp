/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvaanalyticsimpl.h"

#include "dlstreamer/gst/metadata/gva_tripwire_meta.h"
#include "dlstreamer/gst/metadata/gva_zone_meta.h"
#include "dlstreamer/gst/metadata/watermark_circle_meta.h"
#include "dlstreamer/gst/metadata/watermark_draw_meta.h"
#include <gst/gst.h>

// Helper function: Check if point is inside polygon
bool point_in_polygon(const Point &point, const std::vector<Point> &polygon) {
    if (polygon.size() < 3)
        return false;

    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        if ((polygon[i].y > point.y) != (polygon[j].y > point.y) &&
            point.x < (polygon[j].x - polygon[i].x) * (point.y - polygon[i].y) / (polygon[j].y - polygon[i].y) +
                          polygon[i].x) {
            inside = !inside;
        }
    }
    return inside;
}

// Helper function: Check if point is inside circle
bool point_in_circle(const Point &point, const Point &center, int radius) {
    long dx = point.x - center.x;
    long dy = point.y - center.y;
    long distance_squared = dx * dx + dy * dy;
    long radius_squared = (long)radius * radius;
    return distance_squared <= radius_squared;
}

// Helper function: Check if point is inside zone (polygon or circle)
bool point_in_zone(const Point &point, const Zone &zone) {
    if (zone.type == CIRCLE) {
        return point_in_circle(point, zone.center, zone.radius);
    } else {
        return point_in_polygon(point, zone.points);
    }
}

// Helper function: Check if line segment intersects tripwire
bool segment_intersects_tripwire(const Point &p1, const Point &p2, const Tripwire &tripwire) {
    if (tripwire.points.size() != 2)
        return false;

    const Point &t1 = tripwire.points[0];
    const Point &t2 = tripwire.points[1];

    auto side = [](const Point &p, const Point &a, const Point &b) -> long {
        return (long)(b.x - a.x) * (p.y - a.y) - (long)(b.y - a.y) * (p.x - a.x);
    };

    // Both endpoints of the movement segment must be on opposite sides of the tripwire line,
    // AND both tripwire endpoints must be on opposite sides of the movement line.
    long s1 = side(p1, t1, t2);
    long s2 = side(p2, t1, t2);
    long s3 = side(t1, p1, p2);
    long s4 = side(t2, p1, p2);

    return ((s1 < 0 && s2 > 0) || (s1 > 0 && s2 < 0)) && ((s3 < 0 && s4 > 0) || (s3 > 0 && s4 < 0));
}

// Attach zone drawing metadata to buffer
void attach_zone_drawing_metadata(GstBaseTransform *base, GstBuffer *buf, const std::vector<Zone> &zones) {
    if (zones.empty()) {
        return;
    }

    for (const auto &zone : zones) {
        if (zone.type == CIRCLE) {
            WatermarkCircleMeta *circle_meta = watermark_circle_meta_add(buf, zone.center.x, zone.center.y, zone.radius,
                                                                         zone.r, zone.g, zone.b, zone.thickness);
            if (circle_meta) {
                GST_DEBUG_OBJECT(base, "Added circle draw primitive for zone '%s' at (%d,%d) radius %d color(%d,%d,%d)",
                                 zone.id.c_str(), zone.center.x, zone.center.y, zone.radius, zone.r, zone.g, zone.b);
            }
        } else {
            if (zone.points.size() >= 3) {
                std::vector<guint32> coords;
                for (const auto &pt : zone.points) {
                    coords.push_back((guint32)pt.x);
                    coords.push_back((guint32)pt.y);
                }
                WatermarkDrawMeta *draw_meta =
                    watermark_draw_meta_add(buf, coords.data(), coords.size(), zone.r, zone.g, zone.b, zone.thickness);
                if (draw_meta) {
                    GST_DEBUG_OBJECT(base, "Added polygon draw primitive for zone '%s' with %zu points color(%d,%d,%d)",
                                     zone.id.c_str(), zone.points.size(), zone.r, zone.g, zone.b);
                }
            }
        }
    }
}

// Attach tripwire drawing metadata to buffer
void attach_tripwire_drawing_metadata(GstBaseTransform *base, GstBuffer *buf, const std::vector<Tripwire> &tripwires) {
    if (tripwires.empty()) {
        return;
    }

    for (const auto &tripwire : tripwires) {
        if (tripwire.points.size() == 2) {
            guint32 coords[4] = {(guint32)tripwire.points[0].x, (guint32)tripwire.points[0].y,
                                 (guint32)tripwire.points[1].x, (guint32)tripwire.points[1].y};
            WatermarkDrawMeta *draw_meta =
                watermark_draw_meta_add(buf, coords, 4, tripwire.r, tripwire.g, tripwire.b, tripwire.thickness);
            if (draw_meta) {
                GST_DEBUG_OBJECT(base,
                                 "Added line draw primitive for tripwire '%s' from (%d,%d) to (%d,%d) color(%d,%d,%d)",
                                 tripwire.id.c_str(), tripwire.points[0].x, tripwire.points[0].y, tripwire.points[1].x,
                                 tripwire.points[1].y, tripwire.r, tripwire.g, tripwire.b);
            }
        }
    }
}
// Process object detections and check zone membership
void process_object_detections(GstBaseTransform *base, GstAnalyticsRelationMeta *analytics_meta,
                               const std::vector<Zone> &zones, const std::vector<Tripwire> &tripwires,
                               std::map<guint64, ObjectTrackingState> &tracking_states) {
    if (!analytics_meta) {
        GST_DEBUG_OBJECT(base, "No analytics metadata found in buffer");
        return;
    }

    // Iterate through object detection metadata
    gpointer od_state = nullptr;
    GstAnalyticsODMtd od_mtd;
    while (
        gst_analytics_relation_meta_iterate(analytics_meta, &od_state, gst_analytics_od_mtd_get_mtd_type(), &od_mtd)) {
        // Extract object center position from OD metadata (bounding box center)
        gint x, y, w, h;
        gfloat rotation;
        gst_analytics_od_mtd_get_oriented_location(&od_mtd, &x, &y, &w, &h, &rotation, nullptr);
        Point object_center = {x + w / 2, y + h / 2};

        GST_LOG_OBJECT(base, "OD at (%d,%d) size %dx%d, center (%d,%d)", x, y, w, h, object_center.x, object_center.y);

        // ZONE DETECTION: Check zones (polygon or circular) - works with OD only
        for (const auto &zone : zones) {
            if (point_in_zone(object_center, zone)) {
                GST_DEBUG_OBJECT(base, "Object center (%d,%d) is in zone '%s'", object_center.x, object_center.y,
                                 zone.id.c_str());

                // Create zone metadata in relation meta
                GstAnalyticsZoneMtd zone_mtd;
                if (gst_analytics_relation_meta_add_zone_mtd(analytics_meta, zone.id.c_str(), &zone_mtd)) {
                    // Create relation between OD and zone metadata
                    gst_analytics_relation_meta_set_relation(analytics_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                             od_mtd.id, zone_mtd.id);
                }
            }
        }

        // TRIPWIRE DETECTION: Requires tracking (frame-to-frame movement)
        // Check if tracking metadata exists for this detection
        GstAnalyticsTrackingMtd tracking_mtd;
        if (gst_analytics_relation_meta_get_direct_related(analytics_meta, od_mtd.id, GST_ANALYTICS_REL_TYPE_ANY,
                                                           gst_analytics_tracking_mtd_get_mtd_type(), nullptr,
                                                           &tracking_mtd)) {
            // Extract tracking ID
            guint64 tracking_id;
            GstClockTime tracking_first_seen, tracking_last_seen;
            gboolean tracking_lost;
            if (gst_analytics_tracking_mtd_get_info(&tracking_mtd, &tracking_id, &tracking_first_seen,
                                                    &tracking_last_seen, &tracking_lost)) {
                // Check tripwire crossings using tracking history
                auto it = tracking_states.find(tracking_id);
                if (it != tracking_states.end() && it->second.has_previous_position) {
                    // We have previous position, check for crossings
                    const Point &prev_center = it->second.last_center;

                    for (const auto &tripwire : tripwires) {
                        // Check if movement crosses this tripwire
                        if (segment_intersects_tripwire(prev_center, object_center, tripwire)) {
                            // Determine crossing direction
                            // Direction: 1 = left to right, -1 = right to left
                            int direction = 0;
                            if (tripwire.points.size() == 2) {
                                const Point &t1 = tripwire.points[0];
                                const Point &t2 = tripwire.points[1];

                                // Calculate perpendicular direction
                                auto get_side = [](const Point &p, const Point &a, const Point &b) {
                                    return (long)(b.x - a.x) * (p.y - a.y) - (long)(b.y - a.y) * (p.x - a.x);
                                };

                                long prev_side = get_side(prev_center, t1, t2);
                                long curr_side = get_side(object_center, t1, t2);

                                if (prev_side < 0 && curr_side > 0) {
                                    direction = 1; // right-hand side → left-hand side of t1→t2
                                } else if (prev_side > 0 && curr_side < 0) {
                                    direction = -1; // left-hand side → right-hand side of t1→t2
                                }
                            }

                            if (direction != 0) {
                                GST_DEBUG_OBJECT(base, "Tripwire '%s' crossing detected, direction: %d",
                                                 tripwire.id.c_str(), direction);

                                // Create tripwire metadata as relation
                                GstAnalyticsTripwireMtd tripwire_mtd;
                                if (gst_analytics_relation_meta_add_tripwire_mtd(analytics_meta, tripwire.id.c_str(),
                                                                                 direction, &tripwire_mtd)) {
                                    // Create relation between OD and tripwire metadata
                                    gst_analytics_relation_meta_set_relation(
                                        analytics_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd.id, tripwire_mtd.id);
                                }
                            }
                        }
                    }
                }

                // Update tracking state with current position
                ObjectTrackingState state;
                state.tracking_id = tracking_id;
                state.last_center = object_center;
                state.has_previous_position = true;
                tracking_states[tracking_id] = state;
            }
        } else {
            GST_LOG_OBJECT(base, "No tracking metadata for this OD, skipping tripwire detection");
        }
    }
}