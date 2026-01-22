// Ensure dynamic plugin descriptor is exported (must be before any GStreamer headers)
#ifdef GST_PLUGIN_BUILD_STATIC
#undef GST_PLUGIN_BUILD_STATIC
#endif

#include <gst/gst.h>

#include "config.h"
#include "g3dlidarparse.h"
#include <dlstreamer/gst/metadata/g3dlidarmeta.h>

static gboolean plugin_init(GstPlugin *plugin) {
	if (!gst_element_register(plugin, "g3dlidarparse", GST_RANK_NONE, GST_TYPE_G3D_LIDAR_PARSE))
		return FALSE;

	lidar_meta_get_info();
	lidar_meta_api_get_type();

	return TRUE;
}

extern "C" {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
GST_PLUGIN_EXPORT GstPluginDesc gst_plugin_desc = {
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"gst3delements",
	"Intel DL Streamer 3D Elements",
	plugin_init,
	"1.0",
	"LGPL",
	"dlstreamer",
	"https://github.com/dlstreamer/dlstreamer",
	"2026-01-22",
	{0}
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
