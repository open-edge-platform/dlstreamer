/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef LIBRADAR_H
#define LIBRADAR_H
#if defined(__cplusplus)
extern "C" {
#endif
typedef unsigned long ulong;
typedef unsigned short ushort;
typedef enum RadarErrorCode {
    R_SUCCESS = 0,
    R_NULLPTR,
    R_OVERFLOW,
} RadarErrorCode;
typedef enum RadarDoaType { FFT_DOA = 0, MUSIC_DOA, DBF_DOA, CAPON_DOA, MAXLEN_DOA } RadarDoaType;
typedef struct cfloat {
    float real;
    float imag;
} cfloat;
typedef struct RadarParam {
    double startFreq;
    double idle;
    double adcStartTime;
    double rampEndTime;
    double freqSlopeConst;
    double adcSampleRate;
    int rn;    // receiver num
    int tn;    // transmitter num
    int sn;    // number of samples
    int cn;    // sampling rate
    float fps; // radar frame rate

    float dFAR; // doppler False Alarm Rate
    float rFAR; // range False Alarm Rate
    int dGWL;   // doppler Guard Window Len
    int dTWL;   // doppler Train Window Len
    int rGWL;   // range Guard Window Len
    int rTWL;   // range Train Window Len

    RadarDoaType doaType;

    // clustering
    float eps;
    float weight;
    int mpc; // min points in a cluster
    int mc;  // max cluster
    int mp;  // max points

    // tracking
    float tat; // tracker association threshold
    float mnv; // measurement noise variance
    float tpf; // time period per frame
    float iff; // IIR filter forgetting factor
    int at;    // tracker Active Threshold;
    int ft;    // tracker Forget Threshold;
} RadarParam;
typedef struct RadarCube {
    int rn;
    int tn;
    int sn;
    int cn;
    cfloat *mat; // a 3d mat,  cn x sn x (tn x rn)
} RadarCube;
typedef struct RadarPointClouds {
    int len;
    int maxLen;
    ushort *rangeIdx;
    ushort *speedIdx;

    float *range;
    float *speed;
    float *angle;
    float *snr;
} RadarPointClouds;
typedef struct ClusterDescription { // cluster info
    int n;                          // num
    float cx;                       // center x
    float cy;                       // center y
    float rx;                       // radius x
    float ry;                       // radius y
    float av;                       // average velocity
    float vr;                       // variance of range
    float vv;                       // variance of velocity
    float va;                       // variance of angle
} ClusterDescription;
typedef struct ClusterResult {
    int n;    // number of clusters
    int *idx; // indice of clusters
    ClusterDescription *cd;
} ClusterResult;
typedef struct TrackingDescription {
    int tid; // tracker id
    float sHat[4];
    float rx;
    float ry;
} TrackingDescription;
typedef struct TrackingResult {
    int len;
    int maxLen;
    TrackingDescription *td;
} TrackingResult;
typedef struct RadarHandle RadarHandle;

// Function declarations commented out - using dynamic loading (dlopen) instead of static linking
// RadarErrorCode radarGetMemSize(RadarParam* rp, ulong* sz);
// RadarErrorCode radarInitHandle(RadarHandle** h, RadarParam* rp, void* buf, ulong sz);
// RadarErrorCode radarDetection(RadarHandle* h, RadarCube* c, RadarPointClouds* rr);
// RadarErrorCode radarClustering(RadarHandle* h, RadarPointClouds* rr, ClusterResult* cr);
// RadarErrorCode radarTracking(RadarHandle* h, ClusterResult* cr, TrackingResult* tr);
// RadarErrorCode radarProcFrame(RadarHandle* h, RadarCube* c, RadarPointClouds* rr, ClusterResult* cr, TrackingResult*
// tr); RadarErrorCode radarDestroyHandle(RadarHandle* h);
#if defined(__cplusplus)
};
#endif
#endif
