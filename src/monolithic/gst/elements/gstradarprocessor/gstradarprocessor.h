/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_RADAR_PROCESSOR_H__
#define __GST_RADAR_PROCESSOR_H__

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <vector>
#include <complex>
#include "libradar.h"

G_BEGIN_DECLS

#define GST_TYPE_RADAR_PROCESSOR (gst_radar_processor_get_type())
#define GST_RADAR_PROCESSOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RADAR_PROCESSOR, GstRadarProcessor))
#define GST_RADAR_PROCESSOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RADAR_PROCESSOR, GstRadarProcessorClass))
#define GST_IS_RADAR_PROCESSOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RADAR_PROCESSOR))
#define GST_IS_RADAR_PROCESSOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RADAR_PROCESSOR))

typedef struct _GstRadarProcessor GstRadarProcessor;
typedef struct _GstRadarProcessorClass GstRadarProcessorClass;

struct _GstRadarProcessor {
    GstBaseTransform parent;

    // Properties
    gchar *radar_config;
    gdouble frame_rate;

    // Radar parameters from config
    guint num_rx;
    guint num_tx;
    guint num_chirps;
    guint adc_samples;
    guint trn; // Total channels (num_rx * num_tx)

    // Frame rate control
    GstClockTime last_frame_time;
    GstClockTime frame_duration;

    // Frame statistics
    guint64 frame_id;
    guint64 total_frames;
    gdouble total_processing_time;

    // Processing buffers
    std::vector<std::complex<float>> input_data;
    std::vector<std::complex<float>> output_data;

    // Radar parameters for libradar
    RadarParam radar_param;
    RadarCube radar_cube;
    RadarPointClouds radar_point_clouds;
    ClusterResult cluster_result;
    RadarHandle* radar_handle;
    TrackingResult tracking_result;

    // Buffer for TrackingResult
    std::vector<TrackingDescription> tracking_desc_buf;
};

struct _GstRadarProcessorClass {
    GstBaseTransformClass parent_class;
};

GType gst_radar_processor_get_type(void);

G_END_DECLS

#endif /* __GST_RADAR_PROCESSOR_H__ */
