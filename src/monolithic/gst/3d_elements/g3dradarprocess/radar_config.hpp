/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __RADAR_CONFIG_HPP__
#define __RADAR_CONFIG_HPP__

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

enum WinType { Hanning = 1, Hamming = 2, Cheyshev = 3 };
enum AoaEstimationType { FFT = 1, MUSIC = 2, DBF = 3, CAPON = 4 };
enum CfarMethod { CA_CFAR = 1, SO_CFAR = 2, GO_CFAR = 3, OS_CFAR = 4 };

class RadarConfig {
  public:
    // Basic radar parameters (match RadarParam types)
    int num_rx;              // rn
    int num_tx;              // tn
    double start_frequency;  // startFreq
    double idle;             // idle
    double adc_start_time;   // adcStartTime
    double ramp_end_time;    // rampEndTime
    double freq_slope_const; // freqSlopeConst
    int adc_samples;         // sn
    double adc_sample_rate;  // adcSampleRate
    int num_chirps;          // cn
    float fps;               // fps

    // Detection parameters
    WinType range_win_type;
    WinType doppler_win_type;
    AoaEstimationType aoa_estimation_type; // doaType
    CfarMethod doppler_cfar_method;
    float doppler_pfa;         // dFAR
    int doppler_win_guard_len; // dGWL
    int doppler_win_train_len; // dTWL
    CfarMethod range_cfar_method;
    float range_pfa;         // rFAR
    int range_win_guard_len; // rGWL
    int range_win_train_len; // rTWL

    // Clustering parameters
    float eps;                 // eps
    float weight;              // weight
    int min_points_in_cluster; // mpc
    int max_clusters;          // mc
    int max_points;            // mp

    // Tracking parameters
    float tracker_association_threshold; // tat
    float measurement_noise_variance;    // mnv (was double)
    float time_per_frame;                // tpf
    float iir_forget_factor;             // iff
    int tracker_active_threshold;        // at
    int tracker_forget_threshold;        // ft

    RadarConfig()
        : num_rx(4), num_tx(2), start_frequency(77.0), idle(4.0), adc_start_time(6.0), ramp_end_time(32.0),
          freq_slope_const(30.0), adc_samples(256), adc_sample_rate(10000.0), num_chirps(64), fps(10.0),
          range_win_type(static_cast<WinType>(1)), doppler_win_type(static_cast<WinType>(1)),
          aoa_estimation_type(static_cast<AoaEstimationType>(1)), doppler_cfar_method(static_cast<CfarMethod>(1)),
          doppler_pfa(2.0), doppler_win_guard_len(4), doppler_win_train_len(8),
          range_cfar_method(static_cast<CfarMethod>(1)), range_pfa(3.0), range_win_guard_len(6),
          range_win_train_len(10), eps(5.0), weight(0.0), min_points_in_cluster(5), max_clusters(20), max_points(1000),
          tracker_association_threshold(2.0), measurement_noise_variance(0.1), time_per_frame(10.0),
          iir_forget_factor(1.0), tracker_active_threshold(0), tracker_forget_threshold(0) {
    }

    bool load_from_json(const std::string &filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            json j;
            file >> j;

            // Parse RadarBasicConfig
            if (j.contains("RadarBasicConfig") && j["RadarBasicConfig"].is_array() && !j["RadarBasicConfig"].empty()) {
                auto basic = j["RadarBasicConfig"][0];

                if (basic.contains("numRx"))
                    num_rx = basic["numRx"];
                if (basic.contains("numTx"))
                    num_tx = basic["numTx"];
                if (basic.contains("Start_frequency"))
                    start_frequency = basic["Start_frequency"];
                if (basic.contains("idle"))
                    idle = basic["idle"];
                if (basic.contains("adcStartTime"))
                    adc_start_time = basic["adcStartTime"];
                if (basic.contains("rampEndTime"))
                    ramp_end_time = basic["rampEndTime"];
                if (basic.contains("freqSlopeConst"))
                    freq_slope_const = basic["freqSlopeConst"];
                if (basic.contains("adcSamples"))
                    adc_samples = basic["adcSamples"];
                if (basic.contains("adcSampleRate"))
                    adc_sample_rate = basic["adcSampleRate"];
                if (basic.contains("numChirps"))
                    num_chirps = basic["numChirps"];
                if (basic.contains("fps"))
                    fps = basic["fps"];
            }

            // Parse RadarDetectionConfig
            if (j.contains("RadarDetectionConfig") && j["RadarDetectionConfig"].is_array() &&
                !j["RadarDetectionConfig"].empty()) {
                auto detection = j["RadarDetectionConfig"][0];

                if (detection.contains("RangeWinType"))
                    range_win_type = detection["RangeWinType"];
                if (detection.contains("DopplerWinType"))
                    doppler_win_type = detection["DopplerWinType"];
                if (detection.contains("AoaEstimationType"))
                    aoa_estimation_type = detection["AoaEstimationType"];
                if (detection.contains("DopplerCfarMethod"))
                    doppler_cfar_method = detection["DopplerCfarMethod"];
                if (detection.contains("DopplerPfa"))
                    doppler_pfa = detection["DopplerPfa"];
                if (detection.contains("DopplerWinGuardLen"))
                    doppler_win_guard_len = detection["DopplerWinGuardLen"];
                if (detection.contains("DopplerWinTrainLen"))
                    doppler_win_train_len = detection["DopplerWinTrainLen"];
                if (detection.contains("RangeCfarMethod"))
                    range_cfar_method = detection["RangeCfarMethod"];
                if (detection.contains("RangePfa"))
                    range_pfa = detection["RangePfa"];
                if (detection.contains("RangeWinGuardLen"))
                    range_win_guard_len = detection["RangeWinGuardLen"];
                if (detection.contains("RangeWinTrainLen"))
                    range_win_train_len = detection["RangeWinTrainLen"];
            }

            // Parse RadarClusteringConfig
            if (j.contains("RadarClusteringConfig") && j["RadarClusteringConfig"].is_array() &&
                !j["RadarClusteringConfig"].empty()) {
                auto clustering = j["RadarClusteringConfig"][0];

                if (clustering.contains("eps"))
                    eps = clustering["eps"];
                if (clustering.contains("weight"))
                    weight = clustering["weight"];
                if (clustering.contains("minPointsInCluster"))
                    min_points_in_cluster = clustering["minPointsInCluster"];
                if (clustering.contains("maxClusters"))
                    max_clusters = clustering["maxClusters"];
                if (clustering.contains("maxPoints"))
                    max_points = clustering["maxPoints"];
            }

            // Parse RadarTrackingConfig
            if (j.contains("RadarTrackingConfig") && j["RadarTrackingConfig"].is_array() &&
                !j["RadarTrackingConfig"].empty()) {
                auto tracking = j["RadarTrackingConfig"][0];

                if (tracking.contains("trackerAssociationThreshold"))
                    tracker_association_threshold = tracking["trackerAssociationThreshold"];
                if (tracking.contains("measurementNoiseVariance"))
                    measurement_noise_variance = tracking["measurementNoiseVariance"];
                if (tracking.contains("timePerFrame"))
                    time_per_frame = tracking["timePerFrame"];
                if (tracking.contains("iirForgetFactor"))
                    iir_forget_factor = tracking["iirForgetFactor"];
                if (tracking.contains("trackerActiveThreshold"))
                    tracker_active_threshold = tracking["trackerActiveThreshold"];
                if (tracking.contains("trackerForgetThreshold"))
                    tracker_forget_threshold = tracking["trackerForgetThreshold"];
            }

            return true;
        } catch (const std::exception &e) {
            return false;
        }
    }
};

#endif /* __RADAR_CONFIG_HPP__ */
