/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @class GvaRealSensePcd
 * @brief Utility class for reading and writing PCD (Point Cloud Data) files with XYZRGB points.
 *
 * This class provides static methods to read from and write to PCD files in ASCII format,
 * specifically handling point clouds where each point contains x, y, z coordinates and RGB color values.
 *
 * - The `readFile` method parses a PCD file and returns a vector of PointXYZRGB structures.
 * - The `writeFile` method serializes a vector of PointXYZRGB structures into a PCD file.
 *
 * Copy operations are deleted to prevent accidental copying, but move operations are allowed.
 *
 * Usage example:
 * @code
 * std::vector<PointXYZRGB> points = GvaRealSensePcd::readFile("input.pcd");
 * GvaRealSensePcd::writeFile("output.pcd", points);
 * @endcode
 */

#ifndef __GVAREALSENSE_PCD_H__
#define __GVAREALSENSE_PCD_H__

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gvarealsense_common.h"
#include <librealsense2/rs.hpp>

class GvaRealSensePcd {
  public:
    GvaRealSensePcd() = default;
    ~GvaRealSensePcd() = default;
    GvaRealSensePcd(const GvaRealSensePcd &) = delete;
    GvaRealSensePcd &operator=(const GvaRealSensePcd &) = delete;
    GvaRealSensePcd(GvaRealSensePcd &&) = default;
    GvaRealSensePcd &operator=(GvaRealSensePcd &&) = default;

    static std::vector<PointXYZRGB> readFile(const std::string &filename);
    static void writeFile(const std::string &filename, const std::vector<PointXYZRGB> &points);
    static void writeRGBFile(const std::string &filename, const std::vector<PointXYZRGB> &points);

    // Reads frame from RealSense camera and returns a vector of PointXYZRGB points.
    static std::vector<PointXYZRGB> readRSFrame();

    // Returns the header string for a PCD file, which includes metadata such as version, fields, and data type.
    static std::string getPcdHeader(guint width, guint pointCount);

    static std::vector<PointXYZRGB> convertToPointXYZRGB(rs2::depth_frame &depthFrame, rs2::video_frame &rgbFrame);

    static std::string buildPcdBuffer(std::vector<PointXYZRGB> &points);
};

#endif // __GVAREALSENSE_PCD_H__