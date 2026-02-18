/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __G3D_LIDAR_PARSE_H__
#define __G3D_LIDAR_PARSE_H__

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <vector>

G_BEGIN_DECLS

#define GST_TYPE_G3D_LIDAR_PARSE (gst_g3d_lidar_parse_get_type())
#define GST_G3D_LIDAR_PARSE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_LIDAR_PARSE, GstG3DLidarParse))
#define GST_G3D_LIDAR_PARSE_CLASS(klass)                                                                               \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_LIDAR_PARSE, GstG3DLidarParseClass))
#define GST_IS_G3D_LIDAR_PARSE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_LIDAR_PARSE))
#define GST_IS_G3D_LIDAR_PARSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_LIDAR_PARSE))

typedef enum { FILE_TYPE_BIN, FILE_TYPE_PCD } FileType;

typedef struct _GstG3DLidarParse GstG3DLidarParse;
typedef struct _GstG3DLidarParseClass GstG3DLidarParseClass;

struct _GstG3DLidarParse {
    GstBaseTransform parent;

    FileType file_type;
    gint stride;
    gfloat frame_rate;
    GMutex mutex;

    size_t current_index;
    gboolean is_single_file;
    guint stream_id;
};

struct _GstG3DLidarParseClass {
    GstBaseTransformClass parent_class;
};

GType gst_g3d_lidar_parse_get_type(void);
GType file_type_get_type(void);

G_END_DECLS

#endif /* __G3D_LIDAR_PARSE_H__ */
