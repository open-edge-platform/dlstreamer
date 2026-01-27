/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/


#include "config.h"


#include <gst/gst.h>

#include "g3dlidarparse.h"
#include <dlstreamer/gst/metadata/g3dlidarmeta.h>
extern "C" {


static gboolean plugin_init(GstPlugin *plugin) {

	if (!gst_element_register(plugin, "g3dlidarparse", GST_RANK_NONE, GST_TYPE_G3D_LIDAR_PARSE))
		return FALSE;

	lidar_meta_get_info();
	lidar_meta_api_get_type();
    return TRUE;
}


GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, 3delements, "DL Streamer 3D Elements", plugin_init,
                  PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)


} // extern "C"

