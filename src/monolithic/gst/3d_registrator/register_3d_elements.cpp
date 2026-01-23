/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "config.h"

#include <gst/gst.h>

#include "g3d_radarprocess_meta.h"
#include "gstradarprocess.h"

extern "C" {

static gboolean plugin_init(GstPlugin *plugin) {
    // Register g3dradarprocess element
    if (!gst_element_register(plugin, "g3dradarprocess", GST_RANK_NONE, GST_TYPE_RADAR_PROCESS))
        return FALSE;

    // Register metadata
    gst_radar_process_meta_get_info();
    gst_radar_process_meta_api_get_type();

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, 3delements, "DL Streamer 3D Elements", plugin_init,
                  PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)

} // extern "C"