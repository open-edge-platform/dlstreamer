/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/*******************************************************************************
 * @file gvarealsense_pcd.cpp
 * @brief Implementation of GvaRealSensePcd class for reading and writing PCD files.
 *
 * This file contains the implementation of the GvaRealSensePcd class, which provides
 * static methods to read from and write to PCD (Point Cloud Data) files in ASCII format.
 * The class handles point clouds with XYZ coordinates and RGB color values.
 ******************************************************************************/

#include "gvarealsense_pcd.h"
#include "gvarealsense_common.h"
#include <librealsense2/rs.hpp>

/**
 * @brief Reads a PCD (Point Cloud Data) file in ASCII format and extracts 3D points with RGB color information.
 *
 * This method opens the specified PCD file, parses its header to determine the number of points,
 * and reads the point data in ASCII format. Each point is expected to have x, y, z coordinates
 * followed by r, g, b color values. The method returns a vector of PointXYZRGB structures
 * containing the parsed data.
 *
 * @param filename The path to the PCD file to read.
 * @return std::vector<PointXYZRGB> A vector containing all points read from the file.
 * @throws std::runtime_error If the file cannot be opened or if the file format is invalid or unsupported.
 */
std::vector<PointXYZRGB> GvaRealSensePcd::readFile(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open PCD file for reading: " + filename);

    std::string line;
    size_t point_count = 0;
    bool header_ended = false;
    std::vector<PointXYZRGB> points;

    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "POINTS") {
            std::istringstream iss(line);
            std::string dummy;
            iss >> dummy >> point_count;
        }
        if (line == "DATA ascii") {
            header_ended = true;
            break;
        }
    }

    if (!header_ended)
        throw std::runtime_error("Invalid or unsupported PCD file (no DATA ascii header)");

    points.reserve(point_count);

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        PointXYZRGB pt;
        int r, g, b;
        if (!(iss >> pt.x >> pt.y >> pt.z >> r >> g >> b))
            continue;
        pt.r = static_cast<gint8>(r);
        pt.g = static_cast<gint8>(g);
        pt.b = static_cast<gint8>(b);
        points.push_back(pt);
    }
    return points;
}; // read

/**
 * @brief Writes a point cloud to a PCD (Point Cloud Data) file.
 *
 * This method serializes a vector of PointXYZRGB points into a PCD file format.
 * It generates and writes the appropriate PCD header, then writes each point's
 * coordinates and RGB color values to the file.
 *
 * @param filename The path to the output PCD file.
 * @param points A vector containing the point cloud data, where each point includes
 *        x, y, z coordinates and r, g, b color components.
 *
 * @throws std::runtime_error If the file cannot be opened for writing or if the PCD header
 *         cannot be generated.
 */
void GvaRealSensePcd::writeFile(const std::string &filename, const std::vector<PointXYZRGB> &points) {
    std::ofstream file(filename);

    if (!file.is_open())
        throw std::runtime_error("Cannot open PCD file for writing: " + filename);

    std::string header = getPcdHeader(points.size(), points.size());
    if (header.empty())
        throw std::runtime_error("Failed to generate PCD header");

    // Write the header to the file
    file << header;
    for (const auto &pt : points) {
        file << pt.x << " " << pt.y << " " << pt.z << " " << static_cast<int>(pt.r) << " " << static_cast<int>(pt.g)
             << " " << static_cast<int>(pt.b) << "\n";
    }
}; // write

/**
 * @brief Generates the header string for a PCD (Point Cloud Data) file in ASCII format.
 *
 * The header includes metadata such as version, fields (x, y, z, r, g, b), data types,
 * and the provided width and point count values.
 *
 * @return A string containing the PCD file header with width and point count values.
 */
std::string GvaRealSensePcd::getPcdHeader(guint width, guint pointCount) {
    std::string header = "# .PCD v0.7 - Point Cloud Data file format\n";
    header += "VERSION 0.7\n";
    header += "FIELDS x y z r g b\n";
    header += "SIZE 4 4 4 1 1 1\n";
    header += "TYPE F F F U U U\n";
    header += "COUNT 1 1 1 1 1 1\n";
    header += "WIDTH " + std::to_string(width) + "\n";
    header += "HEIGHT 1\n";
    header += "VIEWPOINT 0 0 0 1 0 0 0\n";
    header += "POINTS " + std::to_string(pointCount) + "\n";
    header += "DATA ascii\n";
    return header;
} // getPcdHeader

/**
 * @brief Converts a RealSense depth frame and corresponding RGB frame into a vector of PointXYZRGB.
 *
 * This function generates a 3D point cloud from the provided depth frame using the RealSense SDK.
 * Each point in the resulting vector contains the 3D coordinates (x, y, z) and RGB color values.
 *
 * @param depthFrame The RealSense depth frame containing depth information.
 * @param rgbFrame The RealSense RGB frame containing color information.
 * @return std::vector<PointXYZRGB> A vector containing the 3D points with RGB values.
 */
std::vector<PointXYZRGB> GvaRealSensePcd::convertToPointXYZRGB(rs2::depth_frame &depthFrame,
                                                               rs2::video_frame &rgbFrame) {
    std::vector<PointXYZRGB> pointCloud;
    rs2::pointcloud pc;
    rs2::points points = pc.calculate(depthFrame);

    size_t size = points.size();
    if (size == 0) {
        GST_ERROR("convertToPointXYZRGB: No points in the point cloud.\n");
        return pointCloud;
    }

    // Access point cloud data
    const rs2::vertex *vertices = points.get_vertices();

    const uint8_t *rgb_data = reinterpret_cast<const uint8_t *>(rgbFrame.get_data());

    pointCloud.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        PointXYZRGB pt;
        pt.x = vertices[i].x;
        pt.y = vertices[i].y;
        pt.z = vertices[i].z;
        pt.r = static_cast<guint8>(rgb_data[i * 3]);
        pt.g = static_cast<guint8>(rgb_data[i * 3] + 1);
        pt.b = static_cast<guint8>(rgb_data[i * 3] + 2);
        pointCloud.push_back(pt);
    }

    return pointCloud;
}

/**
 * @brief Builds a PCD (Point Cloud Data) buffer from a vector of PointXYZRGB points.
 *
 * This method generates a string representation of the point cloud in PCD format,
 * including the appropriate header and the list of points with their XYZ coordinates
 * and RGB color values. Each point is written as a line in the format:
 * "x y z r g b", where r, g, and b are cast to integers.
 *
 * @param points A vector of PointXYZRGB structures representing the point cloud data.
 * @return A string containing the complete PCD file content.
 */
std::string GvaRealSensePcd::buildPcdBuffer(std::vector<PointXYZRGB> &points) {
    std::ostringstream oss;
    oss << getPcdHeader(points.size(), points.size());

    for (const auto &pt : points) {
        oss << pt.x << " " << pt.y << " " << pt.z << " " << static_cast<int>(pt.r) << " " << static_cast<int>(pt.g)
            << " " << static_cast<int>(pt.b) << "\n";
    }

    return oss.str();
}

void GvaRealSensePcd::writeRGBFile(const std::string &filename, const std::vector<PointXYZRGB> &points) {
    std::ofstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open PCD file for writing: " + filename);

    for (const auto &pt : points) {
        file << "0x" << std::hex << static_cast<int>(pt.r) << ",0x" << std::hex << static_cast<int>(pt.g) << ",0x"
             << std::hex << static_cast<int>(pt.b) << "\n";
    }
}; // write
