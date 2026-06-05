/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __G3D_RENDER_H__
#define __G3D_RENDER_H__

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_RENDER (gst_g3d_render_get_type())
#define GST_G3D_RENDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_RENDER, GstG3DRender))
#define GST_G3D_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_RENDER, GstG3DRenderClass))
#define GST_IS_G3D_RENDER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_RENDER))
#define GST_IS_G3D_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_RENDER))

#define GST_TYPE_G3D_RENDER_VIEW_MODE (gst_g3d_render_view_mode_get_type())

typedef struct _GstG3DRender GstG3DRender;
typedef struct _GstG3DRenderClass GstG3DRenderClass;

struct _GstG3DRender {
    GstBaseTransform parent;

    gint    width;
    gint    height;
    gfloat  range_x_min;
    gfloat  range_x_max;
    gfloat  range_y_min;
    gfloat  range_y_max;
    gint    point_radius;
    gint    point_stride;
    gfloat  zoom;

    gint    view_mode;
    gfloat  cam_distance;
    gfloat  cam_elevation;
    gfloat  cam_azimuth;
    gfloat  cam_azimuth_step;
    gfloat  cam_fov;

    guint64 frame_count;
};

struct _GstG3DRenderClass {
    GstBaseTransformClass parent_class;
};

GType gst_g3d_render_get_type(void);
GType gst_g3d_render_view_mode_get_type(void);

G_END_DECLS

#endif /* __G3D_RENDER_H__ */
