/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_OBJECT_FUSER (gst_g3d_object_fuser_get_type())
#define GST_G3D_OBJECT_FUSER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_OBJECT_FUSER, GstG3DObjectFuser))
#define GST_G3D_OBJECT_FUSER_CLASS(klass)                                                                              \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_OBJECT_FUSER, GstG3DObjectFuserClass))
#define GST_IS_G3D_OBJECT_FUSER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_OBJECT_FUSER))
#define GST_IS_G3D_OBJECT_FUSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_OBJECT_FUSER))

/**
 * GstG3DFuserTrackingType:
 * @GST_G3D_FUSER_TRACKING_SHORT_TERM_IMAGELESS: short-term imageless tracker
 * @GST_G3D_FUSER_TRACKING_ZERO_TERM_IMAGELESS: zero-term imageless tracker
 *
 * Tracking algorithm used by the internal vas::ot tracker. Only imageless types
 * are supported. The numeric values match vas::ot::TrackingType.
 */
typedef enum {
    GST_G3D_FUSER_TRACKING_SHORT_TERM_IMAGELESS = 4,
    GST_G3D_FUSER_TRACKING_ZERO_TERM_IMAGELESS = 5,
} GstG3DFuserTrackingType;

#define GST_TYPE_G3D_FUSER_TRACKING_TYPE (gst_g3d_fuser_tracking_type_get_type())
GType gst_g3d_fuser_tracking_type_get_type(void);

typedef struct _GstG3DObjectFuser GstG3DObjectFuser;
typedef struct _GstG3DObjectFuserClass GstG3DObjectFuserClass;
typedef struct _GstG3DObjectFuserPrivate GstG3DObjectFuserPrivate;

struct _GstG3DObjectFuser {
    GstBaseTransform parent;

    /* Properties */
    gchar *calibration_path;
    gfloat assoc_iou_threshold;
    guint track_history_window;
    GstG3DFuserTrackingType tracking_type;

    /* Pimpl: holds C++ object fuser, trackers, calibration, etc. */
    GstG3DObjectFuserPrivate *priv;
};

struct _GstG3DObjectFuserClass {
    GstBaseTransformClass parent_class;
};

GType gst_g3d_object_fuser_get_type(void);

G_END_DECLS
