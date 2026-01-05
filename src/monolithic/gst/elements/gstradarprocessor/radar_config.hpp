/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __RADAR_CONFIG_HPP__
#define __RADAR_CONFIG_HPP__

#include <string>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

class RadarConfig {
public:
    // Basic radar parameters
    unsigned int num_rx;
    unsigned int num_tx;
    unsigned int start_frequency;
    unsigned int idle;
    unsigned int adc_start_time;
    unsigned int ramp_end_time;
    unsigned int freq_slope_const;
    unsigned int adc_samples;
    unsigned int adc_sample_rate;
    unsigned int num_chirps;
    unsigned int fps;

    // Detection parameters
    unsigned int range_win_type;
    unsigned int doppler_win_type;
    unsigned int aoa_estimation_type;
    unsigned int doppler_cfar_method;
    unsigned int doppler_pfa;
    unsigned int doppler_win_guard_len;
    unsigned int doppler_win_train_len;
    unsigned int range_cfar_method;
    unsigned int range_pfa;
    unsigned int range_win_guard_len;
    unsigned int range_win_train_len;

    // Clustering parameters
    double eps;
    unsigned int weight;
    unsigned int min_points_in_cluster;
    unsigned int max_clusters;
    unsigned int max_points;

    // Tracking parameters
    unsigned int tracker_association_threshold;
    double measurement_noise_variance;
    unsigned int time_per_frame;
    unsigned int iir_forget_factor;
    unsigned int tracker_active_threshold;
    unsigned int tracker_forget_threshold;

    RadarConfig() : 
        num_rx(4), num_tx(2), start_frequency(77), idle(4),
        adc_start_time(6), ramp_end_time(32), freq_slope_const(30),
        adc_samples(256), adc_sample_rate(10000), num_chirps(64), fps(10),
        range_win_type(1), doppler_win_type(1), aoa_estimation_type(1),
        doppler_cfar_method(1), doppler_pfa(2), doppler_win_guard_len(4),
        doppler_win_train_len(8), range_cfar_method(1), range_pfa(3),
        range_win_guard_len(6), range_win_train_len(10),
        eps(5.0), weight(0), min_points_in_cluster(5), max_clusters(20),
        max_points(1000), tracker_association_threshold(2),
        measurement_noise_variance(0.1), time_per_frame(10),
        iir_forget_factor(1), tracker_active_threshold(0),
        tracker_forget_threshold(0) {}

    bool load_from_json(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            json j;
            file >> j;

            // Parse RadarBasicConfig
            if (j.contains("RadarBasicConfig") && j["RadarBasicConfig"].is_array() && 
                !j["RadarBasicConfig"].empty()) {
                auto basic = j["RadarBasicConfig"][0];
                
                if (basic.contains("numRx")) num_rx = basic["numRx"];
                if (basic.contains("numTx")) num_tx = basic["numTx"];
                if (basic.contains("Start_frequency")) start_frequency = basic["Start_frequency"];
                if (basic.contains("idle")) idle = basic["idle"];
                if (basic.contains("adcStartTime")) adc_start_time = basic["adcStartTime"];
                if (basic.contains("rampEndTime")) ramp_end_time = basic["rampEndTime"];
                if (basic.contains("freqSlopeConst")) freq_slope_const = basic["freqSlopeConst"];
                if (basic.contains("adcSamples")) adc_samples = basic["adcSamples"];
                if (basic.contains("adcSampleRate")) adc_sample_rate = basic["adcSampleRate"];
                if (basic.contains("numChirps")) num_chirps = basic["numChirps"];
                if (basic.contains("fps")) fps = basic["fps"];
            }

            // Parse RadarDetectionConfig
            if (j.contains("RadarDetectionConfig") && j["RadarDetectionConfig"].is_array() && 
                !j["RadarDetectionConfig"].empty()) {
                auto detection = j["RadarDetectionConfig"][0];
                
                if (detection.contains("RangeWinType")) range_win_type = detection["RangeWinType"];
                if (detection.contains("DopplerWinType")) doppler_win_type = detection["DopplerWinType"];
                if (detection.contains("AoaEstimationType")) aoa_estimation_type = detection["AoaEstimationType"];
                if (detection.contains("DopplerCfarMethod")) doppler_cfar_method = detection["DopplerCfarMethod"];
                if (detection.contains("DopplerPfa")) doppler_pfa = detection["DopplerPfa"];
                if (detection.contains("DopplerWinGuardLen")) doppler_win_guard_len = detection["DopplerWinGuardLen"];
                if (detection.contains("DopplerWinTrainLen")) doppler_win_train_len = detection["DopplerWinTrainLen"];
                if (detection.contains("RangeCfarMethod")) range_cfar_method = detection["RangeCfarMethod"];
                if (detection.contains("RangePfa")) range_pfa = detection["RangePfa"];
                if (detection.contains("RangeWinGuardLen")) range_win_guard_len = detection["RangeWinGuardLen"];
                if (detection.contains("RangeWinTrainLen")) range_win_train_len = detection["RangeWinTrainLen"];
            }

            // Parse RadarClusteringConfig
            if (j.contains("RadarClusteringConfig") && j["RadarClusteringConfig"].is_array() && 
                !j["RadarClusteringConfig"].empty()) {
                auto clustering = j["RadarClusteringConfig"][0];
                
                if (clustering.contains("eps")) eps = clustering["eps"];
                if (clustering.contains("weight")) weight = clustering["weight"];
                if (clustering.contains("minPointsInCluster")) min_points_in_cluster = clustering["minPointsInCluster"];
                if (clustering.contains("maxClusters")) max_clusters = clustering["maxClusters"];
                if (clustering.contains("maxPoints")) max_points = clustering["maxPoints"];
            }

            // Parse RadarTrackingConfig
            if (j.contains("RadarTrackingConfig") && j["RadarTrackingConfig"].is_array() && 
                !j["RadarTrackingConfig"].empty()) {
                auto tracking = j["RadarTrackingConfig"][0];
                
                if (tracking.contains("trackerAssociationThreshold")) 
                    tracker_association_threshold = tracking["trackerAssociationThreshold"];
                if (tracking.contains("measurementNoiseVariance")) 
                    measurement_noise_variance = tracking["measurementNoiseVariance"];
                if (tracking.contains("timePerFrame")) time_per_frame = tracking["timePerFrame"];
                if (tracking.contains("iirForgetFactor")) iir_forget_factor = tracking["iirForgetFactor"];
                if (tracking.contains("trackerActiveThreshold")) 
                    tracker_active_threshold = tracking["trackerActiveThreshold"];
                if (tracking.contains("trackerForgetThreshold")) 
                    tracker_forget_threshold = tracking["trackerForgetThreshold"];
            }

            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }
};

#endif /* __RADAR_CONFIG_HPP__ */
