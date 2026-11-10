/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaaudiotranscribe.h"

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "gvaaudiotranscribe", GST_RANK_NONE, GST_TYPE_GVA_AUDIO_TRANSCRIBE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvaaudiotranscribe,
                  PRODUCT_FULL_NAME " audio transcription element", plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE,
                  PACKAGE_NAME, GST_PACKAGE_ORIGIN)
