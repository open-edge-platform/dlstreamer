#ifndef __G3D_LIDAR_VALIDATE_H__
#define __G3D_LIDAR_VALIDATE_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_LIDAR_VALIDATE (gst_g3d_lidar_validate_get_type())
#define GST_G3D_LIDAR_VALIDATE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_LIDAR_VALIDATE, GstG3DLidarValidate))
#define GST_G3D_LIDAR_VALIDATE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_LIDAR_VALIDATE, GstG3DLidarValidateClass))
#define GST_IS_G3D_LIDAR_VALIDATE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_LIDAR_VALIDATE))
#define GST_IS_G3D_LIDAR_VALIDATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_LIDAR_VALIDATE))

typedef struct _GstG3DLidarValidate {
    GstBaseSink parent;

    guint expected_point_count;
    guint preview_count;
    gboolean fail_on_mismatch;
    gboolean silent;

    guint64 frames_seen;
    guint64 frames_with_meta;
} GstG3DLidarValidate;

typedef struct _GstG3DLidarValidateClass {
    GstBaseSinkClass parent_class;
} GstG3DLidarValidateClass;

GType gst_g3d_lidar_validate_get_type(void);

G_END_DECLS

#endif /* __G3D_LIDAR_VALIDATE_H__ */
