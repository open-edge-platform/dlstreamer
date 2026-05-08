/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/analytics/analytics.h>
#include <gst/base/gstbasetransform.h>
#include <map>
#include <string>
#include <vector>

// Data structures for zones and tripwires
struct Point {
    int x;
    int y;
};

struct Tripwire {
    std::string id;
    std::vector<Point> points;
    // Color components (0-255)
    int r = 255; // Default red
    int g = 0;
    int b = 0;
    int thickness = 1;
};

enum ZoneType { POLYGON, CIRCLE };

struct Zone {
    std::string id;
    ZoneType type;
    // For polygon zones
    std::vector<Point> points;
    // For circular zones
    Point center;
    int radius;
    // Color components (0-255)
    int r = 0; // Default green
    int g = 255;
    int b = 0;
    int thickness = 1;

    Zone() : type(POLYGON), radius(0) {
    }
};

// Geometry helper functions
bool point_in_polygon(const Point &point, const std::vector<Point> &polygon);
bool point_in_circle(const Point &point, const Point &center, int radius);
bool point_in_zone(const Point &point, const Zone &zone);
bool segment_intersects_tripwire(const Point &p1, const Point &p2, const Tripwire &tripwire);

// Structure to store object tracking history for tripwire crossing detection
struct ObjectTrackingState {
    guint64 tracking_id;
    Point last_center;
    bool has_previous_position = false;
};

// Metadata attachment functions
void attach_zone_drawing_metadata(GstBaseTransform *base, GstBuffer *buf, const std::vector<Zone> &zones);
void attach_tripwire_drawing_metadata(GstBaseTransform *base, GstBuffer *buf, const std::vector<Tripwire> &tripwires);

// Detection processing function
void process_object_detections(GstBaseTransform *base, GstAnalyticsRelationMeta *analytics_meta,
                               const std::vector<Zone> &zones, const std::vector<Tripwire> &tripwires,
                               std::map<guint64, ObjectTrackingState> &tracking_states);
