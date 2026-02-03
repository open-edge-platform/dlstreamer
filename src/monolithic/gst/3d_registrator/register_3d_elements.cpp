/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/


#include "config.h"

#include <gst/gst.h>

#include "g3d_radarprocess_meta.h"
#include "gstradarprocess.h"

#if defined(HAVE_G3DLIDARPARSE)
#include "g3dlidarparse.h"
#include "g3d_lidar_meta.h"
#endif

extern "C" {


static gboolean plugin_init(GstPlugin *plugin) {
	if (!gst_element_register(plugin, "g3dradarprocess", GST_RANK_NONE, GST_TYPE_RADAR_PROCESS))
        return FALSE;
#if defined(HAVE_G3DLIDARPARSE)
	if (!gst_element_register(plugin, "g3dlidarparse", GST_RANK_NONE, GST_TYPE_G3D_LIDAR_PARSE))
		return FALSE;
#endif
	gst_radar_process_meta_get_info();
    gst_radar_process_meta_api_get_type();
#if defined(HAVE_G3DLIDARPARSE)
	lidar_meta_get_info();
	lidar_meta_api_get_type();
#endif
    return TRUE;
}


GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, 3delements, "DL Streamer 3D Elements", plugin_init,
                  PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)


} // extern "C"
