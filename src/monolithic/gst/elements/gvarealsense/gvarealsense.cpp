/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
/**
 * @file gvarealsense.cpp
 * @brief GStreamer source element for Intel RealSense cameras.
 *
 * This file implements a GStreamer source element that interfaces with Intel RealSense cameras,
 * allowing video and point cloud data to be streamed into a GStreamer pipeline. The element
 * supports property configuration, RealSense pipeline management, and buffer creation for
 * depth and point cloud data.
 *
 * Key Features:
 * - Registers a GStreamer source element named "realsense".
 * - Supports the "camera" property for device selection.
 * - Initializes and manages a RealSense pipeline using librealsense.
 * - Implements buffer creation callbacks to provide depth and point cloud data.
 * - Handles GStreamer caps negotiation.
 *
 * Main Components:
 * - Class initialization and property registration.
 * - RealSense pipeline setup and error handling.
 * - Buffer creation for depth frames and point cloud extraction.
 * - GStreamer base source virtual method implementations.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gvarealsense.h"
#include "gvarealsense_common.h"
#include "gvarealsense_pcd.h"
#include "gvarealsense_utils.h"
#include <glib/gstdio.h>
#include <gst/gst.h>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

// #define struct_stat struct stat

#include <errno.h>
#include <string.h>

// Utlities functions:
gboolean gva_real_sense_is_device_available(gchar *devPath);

static inline gboolean is_device_node(const gchar *camera) {
    return camera && g_str_has_prefix(camera, "/dev/");
}

// Returns TRUE if a RealSense device with the given serial number is currently connected.
static gboolean is_rs_serial_available(const gchar *serial) {
    if (!serial || !*serial)
        return FALSE;

    try {
        rs2::context ctx;
        for (auto &&dev : ctx.query_devices()) {
            const char *sn = dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
            if (sn && g_strcmp0(sn, serial) == 0)
                return TRUE;
        }
    } catch (const rs2::error &e) {
        GST_WARNING("is_rs_serial_available: RealSense error while querying devices: %s", e.what());
    }
    return FALSE;
}

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ RgbZ16 }")));

/* RealSense signals and args */
enum { LAST_SIGNAL };

#define DEFAULT_BLOCKSIZE 4 * 1024

#define RS2_VERTEX_RECOER_SEZE 12 // 3 floats (x, y, z) * 4 bytes each

enum { PROP_0, PROP_CAMERA };

// Converts RS2 video format to GST video format.
static GstVideoFormat get_gst_video_format(rs2_format rsFormat);

// Finalizes the GstRealSense object, freeing any allocated resources.
static void gst_real_sense_finalize(GObject *object);

// Sets a property on the GstRealSense object.
static void gst_real_sense_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
// Gets a property from the GstRealSense object.
static void gst_real_sense_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

// Called when the element transitions to the READY or PAUSED state; initializes resources.
static gboolean gst_real_sense_start(GstBaseSrc *basesrc);
// Called when the element transitions to the READY or NULL state; releases resources.
static gboolean gst_real_sense_stop(GstBaseSrc *basesrc);

// Returns whether the source is seekable.
static gboolean gst_real_sense_is_seekable(GstBaseSrc *src);
// Gets the size of the data stream, if available.
// static gboolean gst_real_sense_get_size (GstBaseSrc * src, guint64 * size);
// Creates a new buffer filled with data from the RealSense device.
static GstFlowReturn gst_real_sense_create(GstBaseSrc *src, guint64 offset, guint length, GstBuffer **buf);

static GstCaps *gst_real_sense_get_caps(GstBaseSrc *src, GstCaps *filter);

void debug_print_caps(GstCaps *caps1);

// Macro to initialize the debug category for the RealSense element
#define _do_init GST_DEBUG_CATEGORY_INIT(gst_real_sense_debug, "realsense", 2, "realsense element");

// Define the parent class for GstRealSense
#define gst_real_sense_parent_class parent_class

// Define the GstRealSense type with the specified parent and initialization code
G_DEFINE_TYPE_WITH_CODE(GstRealSense, gst_real_sense, GST_TYPE_BASE_SRC, _do_init);

// Register the RealSense element with GStreamer under the name "realsense"
GST_ELEMENT_REGISTER_DEFINE(realsense, "realsense", GST_RANK_PRIMARY, GST_TYPE_REAL_SENSE);

/**
 * gst_real_sense_class_init:
 * @klass: (GstRealSenseClass *) The class structure for the GstRealSense element.
 *
 * Initializes the GstRealSenseClass by setting up the GObject, GstElement, and GstBaseSrc
 * class function pointers and properties. This includes installing the "camera" property,
 * setting finalize and property handlers, assigning static metadata and pad templates,
 * and configuring the base source virtual methods such as start, stop, is_seekable, get_size,
 * fill, and create. Also logs a message if large file support is not available.
 *
 * This function is typically called once during the class initialization phase of the
 * GStreamer element registration process.
 */
static void gst_real_sense_class_init(GstRealSenseClass *klass) {
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSrcClass *gstbasesrc_class;

    GST_INFO("gst_real_sense_class_init\n");

    gobject_class = G_OBJECT_CLASS(klass);
    gstelement_class = GST_ELEMENT_CLASS(klass);
    gstbasesrc_class = GST_BASE_SRC_CLASS(klass);

    gobject_class->set_property = gst_real_sense_set_property;
    gobject_class->get_property = gst_real_sense_get_property;
    gstbasesrc_class->get_caps = gst_real_sense_get_caps;
    gobject_class->finalize = gst_real_sense_finalize;

    g_object_class_install_property(
        gobject_class, PROP_CAMERA,
        g_param_spec_string("camera", "Camera device (/dev/video*) or serial", "Real Sense camera selector", NULL,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    gst_element_class_set_static_metadata(gstelement_class, "Real Sense camera", "Real Sense video",
                                          "Read from Real Sense camera",
                                          "Deep Learning Stream engineering team, Intel Corporation");
    gst_element_class_add_static_pad_template(gstelement_class, &srctemplate);

    // Assign GstBaseSrc virtual methods to the corresponding RealSense element functions
    gstbasesrc_class->stop =
        GST_DEBUG_FUNCPTR(gst_real_sense_stop); // Called when the element transitions to READY or NULL
    gstbasesrc_class->start =
        GST_DEBUG_FUNCPTR(gst_real_sense_start); // Called when the element transitions to READY or PAUSED
    gstbasesrc_class->is_seekable =
        GST_DEBUG_FUNCPTR(gst_real_sense_is_seekable); // Returns whether the source is seekable
    gstbasesrc_class->create =
        GST_DEBUG_FUNCPTR(gst_real_sense_create); // Creates a new buffer with data from the RealSense device
} // gst_real_sense_class_init

/**
 * gst_real_sense_init:
 * @src: (GstRealSense*) The instance of the GstRealSense element being initialized.
 *
 * Initializes the GstRealSense object. This function sets the default block size for the base source,
 * configures the default video format and resolution (640x480, GRAY16_LE or GBR_10BE depending on endianness),
 * and attempts to create corresponding GstVideoInfo and GstCaps structures. It prints diagnostic messages
 * for each initialization step and reports errors if any part of the setup fails.
 *
 * This function is called once when a new GstRealSense element instance is created.
 */
static void gst_real_sense_init(GstRealSense *src) {
    GST_INFO("gst_real_sense_init\n");

    if (!src) {
        GST_ERROR("Failed to initialize GstRealSense, src is NULL\n");
    }
    gst_base_src_set_blocksize(GST_BASE_SRC(src), DEFAULT_BLOCKSIZE);

    GstVideoInfo videoInfo;

    GstVideoFormat vidFormat =
        (G_BYTE_ORDER == G_LITTLE_ENDIAN) ? GST_VIDEO_FORMAT_GRAY16_LE : GST_VIDEO_FORMAT_GBR_10BE;

    gst_video_info_init(&videoInfo);
    gboolean ret = gst_video_info_set_format(&videoInfo, vidFormat, 640, 480);
    if (ret == FALSE) {
        GST_ERROR("Failed to set video format to GST_VIDEO_FORMAT_GRAY16_LE\n");
    } else {
        GST_INFO("gst_video_info_set_format: GST_VIDEO_FORMAT_GRAY16_LE, 640x480\n");
    }

    GstCaps *_gstCaps = gst_video_info_to_caps(&videoInfo);
    if (_gstCaps == nullptr) {
        GST_ERROR("Failed to convert video info to GstCaps\n");
    }

    GstCaps *anyCaps = gst_caps_new_any();
    if (anyCaps == nullptr) {
        GST_ERROR("Failed to create GstCaps with gst_caps_new_any\n");
    }

    gst_base_src_set_format(GST_BASE_SRC(src), GST_FORMAT_TIME);
}

/**
 * @brief Finalizes the GstRealSense object.
 *
 * This function is called during the finalization phase of the GObject lifecycle
 * for the GstRealSense element. It should be used to release any resources or
 * perform cleanup tasks specific to the GstRealSense object before it is destroyed.
 *
 * @param object A pointer to the GObject instance being finalized.
 */
static void gst_real_sense_finalize(GObject *object) {
    if (!object) {
        GST_ERROR("Failed to finalize GstRealSense, object is NULL\n");
        return;
    }

    GstRealSense *src = GST_REAL_SENSE(object);

    g_clear_pointer(&src->uri, g_free);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/**
 * gst_real_sense_set_camera:
 * @src: (in): Pointer to the GstRealSense object.
 * @camera: (in): The camera identifier or name as a string.
 * @err: (out) (optional): Location to store a GError in case of failure.
 *
 * Initializes and configures the RealSense camera pipeline for the given GstRealSense
 * object. This function enables the depth stream and attempts to start the RealSense
 * pipeline using the specified camera. If successful, it prints information about
 * connected RealSense devices. In case of failure, it prints an error message and
 * returns FALSE.
 *
 * Returns: TRUE if the camera was successfully configured and the pipeline started,
 * FALSE otherwise.
 */
static gboolean gst_real_sense_set_camera(GstRealSense *src, const gchar *camera, GError **err) {
    rs2::config conf;

    if (!camera) {
        GST_ERROR("gst_real_sense_set_camera: Camera device is NULL\n");
        if (err) {
            g_set_error(err, GST_RESOURCE_ERROR_NOT_FOUND, GST_RESOURCE_ERROR_NOT_FOUND,
                        "Camera device is not specified.\n");
        }
        g_set_error(err, GST_RESOURCE_ERROR_NOT_FOUND, GST_RESOURCE_ERROR_NOT_FOUND,
                    "Camera device is not specified.\n");
        return FALSE;
    }

    try {
        if (!is_device_node(camera)) {
            conf.enable_device(camera);
            GST_INFO("gst_real_sense_set_camera: Selecting RealSense device by serial: %s\n", camera);
        } else {
            GST_INFO("gst_real_sense_set_camera: Camera value %s is a device node; starting default RealSense pipeline. "
                     "Use serial number for explicit device selection.\n",
                     camera);
        }

        conf.enable_stream(RS2_STREAM_DEPTH, RS2_FORMAT_Z16);
        conf.enable_stream(RS2_STREAM_COLOR, RS2_FORMAT_RGB8);

    } catch (const rs2::error &e) {
        g_set_error(err, GST_RESOURCE_ERROR_NOT_FOUND, GST_RESOURCE_ERROR_NOT_FOUND,
                    "Failed to enable depth stream: %s\n", e.what());
        return FALSE;
    }

    try {
        if (src->rsPipeline) {
            src->rsPipeline->stop();
            src->rsPipeline.reset();
        }

        src->rsPipeline = std::make_unique<rs2::pipeline>();
        
        rs2::pipeline_profile profile = src->rsPipeline->start(conf);
        rs2::device device = profile.get_device();

        GST_INFO("Started RealSense device: %s\n", device.get_info(RS2_CAMERA_INFO_NAME));

    } // try
    catch (const rs2::error &e) {
        GST_ERROR("Failed to start Real Sense pipeline: %s\n", e.what());
        return FALSE;
    } // catch
    return TRUE;
}

/**
 * gst_real_sense_set_property:
 * @object: (GObject *) The GObject instance to set the property on.
 * @prop_id: (guint) The property identifier.
 * @value: (const GValue *) The value to set for the property.
 * @pspec: (GParamSpec *) The GParamSpec describing the property.
 *
 * Sets a property on the GstRealSense object based on the provided property ID.
 * This function is typically called by the GObject property system when a property
 * is set via g_object_set(). It handles the assignment of property values to the
 * GstRealSense instance, dispatching to the appropriate setter function depending
 * on the property ID. If the property ID is not recognized, a warning is issued.
 */
static void gst_real_sense_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GST_INFO("gst_real_sense_set_property\n");

    GstRealSense *src;

    g_return_if_fail(GST_IS_REAL_SENSE(object));

    GST_INFO("gst_real_sense_set_property: prop_id: %u\n", prop_id);
    src = GST_REAL_SENSE(object);

    switch (prop_id) {
    case PROP_CAMERA: {
        g_free(src->uri);
        src->uri = g_value_dup_string(value);

        GST_INFO("gst_real_sense_set_property: Camera device set to %s\n", GST_STR_NULL(src->uri));

        if (!src->uri || !*src->uri) {
            GST_ERROR("gst_real_sense_set_property: Camera selector is empty.\n");
        } else if (is_device_node(src->uri)) {
            // A /dev/videoN node: confirm the node exists on the filesystem.
            if (gva_real_sense_is_device_available(src->uri))
                GST_INFO("gst_real_sense_set_property: Camera device node %s is available.\n", src->uri);
            else
                GST_ERROR("gst_real_sense_set_property: Camera device node %s is not available.\n", src->uri);
        } else {
            // A serial number: confirm it against the connected RealSense devices.
            if (is_rs_serial_available(src->uri))
                GST_INFO("gst_real_sense_set_property: RealSense serial %s is available.\n", src->uri);
            else
                GST_ERROR("gst_real_sense_set_property: RealSense serial %s is not connected.\n", src->uri);
        }

        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * gst_real_sense_get_property:
 * @object: (GObject *) The instance of the GstRealSense object.
 * @prop_id: (guint) The property identifier to get.
 * @value: (GValue *) The value to be set for the property.
 * @pspec: (GParamSpec *) The parameter specification for the property.
 *
 * Retrieves the value of a property for the GstRealSense object.
 * This function is called by the GObject property system when a property
 * value is requested. It handles property retrieval based on the property
 * ID and sets the corresponding value in the provided GValue.
 * If the property ID is not recognized, a warning is issued.
 */
static void gst_real_sense_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GST_INFO("gst_real_sense_get_property\n");

    if (!value) {
        GST_ERROR("gst_real_sense_get_property: value is NULL\n");
        return;
    }

    g_return_if_fail(GST_IS_REAL_SENSE(object));

    GstRealSense *src = GST_REAL_SENSE(object);

    switch (prop_id) {
    case PROP_CAMERA:
        g_value_set_string(value, src->uri);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
} // gst_real_sense_get_property

/**
 * gst_real_sense_is_seekable:
 * @src: (GstBaseSrc*) The source element to check for seekability.
 *
 * Determines whether the RealSense source element supports seeking.
 *
 * Returns: FALSE, indicating that seeking is not supported by this source.
 */
gboolean gst_real_sense_is_seekable(GstBaseSrc *src) {
    if (!src) {
        GST_ERROR("gst_real_sense_is_seekable: src is NULL\n");
        return FALSE;
    }
    return FALSE;
}

/**
 * @brief Captures a depth frame from a RealSense camera, processes it into a point cloud,
 *        and outputs the point cloud data as a GstBuffer.
 *
 * This function is called by the GStreamer pipeline to create a new buffer containing
 * point cloud data derived from the latest depth frame captured by the RealSense camera.
 * It waits for a new set of frames, extracts the depth frame, and calculates the distance
 * to the object at the center of the image. It then generates a point cloud from the depth
 * frame, allocates a buffer of appropriate size, copies the point cloud vertex data into
 * the buffer, and sets the buffer as the output.
 *
 * @param basesrc  Pointer to the GstBaseSrc element.
 * @param offset   Offset in the stream (unused).
 * @param length   Length of the buffer to create (unused).
 * @param buf      Pointer to the location where the created GstBuffer will be stored.
 * @return         GST_FLOW_OK on success, GST_FLOW_ERROR on failure.
 */
static GstFlowReturn gst_real_sense_create(GstBaseSrc *basesrc, guint64 offset, guint length, GstBuffer **buf) {
    GstBuffer *buffer = nullptr;
    GstRealSense *src = GST_REAL_SENSE_CAST(basesrc);
    GstMapInfo map;

    guint width = 0;
    guint height = 0;

    if (length == 0) {
        GST_ERROR("gst_real_sense_create: Invalid length value: %u\n", length);
        return GST_FLOW_ERROR;
    }

    rs2::frameset frames = src->rsPipeline->wait_for_frames();

    // Try to get a frame of a depth image
    rs2::depth_frame depth = frames.get_depth_frame();

    width = depth.get_width();
    height = depth.get_height();

    GST_INFO("Depth frame dimensions: width = %u, height = %u\n", width, height);
    if (width == 0 || height == 0) {
        GST_ERROR("gst_real_sense_create: Incorrect width/height value.\n");
        GST_ERROR("gst_real_sense_create: width: %u, height: %u\n", width, height);
        return GST_FLOW_ERROR;
    }

    // Get RGB frame if available

    std::vector<PointXYZRGB> pointCloud;
    try {
        rs2::video_frame colorFrame = frames.get_color_frame();
        if (colorFrame) {
            GST_INFO("gst_real_sense_create: Color frame dimensions: width = %u, height = %u\n", colorFrame.get_width(),
                     colorFrame.get_height());

            pointCloud = GvaRealSensePcd::convertToPointXYZRGB(depth, colorFrame);
        }
    } catch (const rs2::error &e) {
        GST_ERROR("gst_real_sense_create: No color frame available: %s\n", e.what());
    }

    if (offset > G_MAXUINT64) {
        GST_ERROR("gst_real_sense_create: Invalid offset value: %" G_GINT64_FORMAT "\n", offset);
        return GST_FLOW_ERROR;
    }

    // Create a point cloud from the depth frame
    std::string pcdBuffer = GvaRealSensePcd::buildPcdBuffer(pointCloud);

    size_t size_to_allocate = pcdBuffer.size();

    buffer = gst_buffer_new_allocate(NULL, size_to_allocate, NULL);

    if (!buffer) {
        GST_ERROR("gst_real_sense_create: Failed to allocate GstBuffer\n");
        return GST_FLOW_ERROR;
    }

    // Map the buffer for writing
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

    memcpy(map.data, pcdBuffer.data(), size_to_allocate);

    gst_buffer_unmap(buffer, &map);

    // Set buffer as output
    gst_buffer_set_size(buffer, size_to_allocate);
    *buf = buffer;

    return GST_FLOW_OK;
}

/**
 * @brief Initializes and starts the RealSense video source element.
 *
 * This function is called when the GStreamer pipeline transitions the RealSense
 * source element to the playing state. It sets the video format for the source
 * to RS2_FORMAT_Z16 and logs the selected format for debugging purposes.
 *
 * @param basesrc Pointer to the GstBaseSrc base class of the RealSense source element.
 * @return TRUE if the source was successfully started, FALSE otherwise.
 */
static gboolean gst_real_sense_start(GstBaseSrc *basesrc) {
    auto *src = GST_REAL_SENSE(basesrc);

    if (!src->uri || !*src->uri) {
        GST_ERROR("gst_real_sense_start: Camera device is not configured\n");
        return FALSE;
    }

    if (!gst_real_sense_set_camera(src, src->uri, NULL)) {
        GST_ERROR("gst_real_sense_start: Failed to start camera %s\n", src->uri);
        return FALSE;
    }

    src->gstVideoFormat = get_gst_video_format(RS2_FORMAT_Z16);
    GST_DEBUG("gst_real_sense_start: Using video format: %s\n", gst_video_format_to_string(src->gstVideoFormat));
    return TRUE;
}

/**
 * @brief Stops the RealSense source element.
 *
 * This function is called when the GStreamer pipeline requests the RealSense
 * source element to stop streaming or shut down. It performs any necessary
 * cleanup or resource deallocation required to gracefully stop the source.
 *
 * @param basesrc Pointer to the GstBaseSrc element representing the RealSense source.
 * @return TRUE if the stop operation was successful, otherwise FALSE.
 */
static gboolean gst_real_sense_stop(GstBaseSrc *basesrc) {
    if (!basesrc) {
        GST_ERROR("gst_real_sense_stop: basesrc is NULL\n");
        return FALSE;
    }

    auto *src = GST_REAL_SENSE(basesrc);

    if (src->rsPipeline) {
        try {
            src->rsPipeline->stop();
        } catch (const rs2::error &e) {
            GST_WARNING("gst_real_sense_stop: Failed to stop RealSense pipeline cleanly: %s\n", e.what());
        }
        src->rsPipeline.reset();
    }

    return TRUE;
}

/**
 * @brief Converts a RealSense video format (rs2_format) to the corresponding GStreamer video format
 * (GstVideoFormat).
 *
 * This function maps a given RealSense video format to its equivalent GStreamer video format.
 * If the provided RealSense format is not supported, the function logs a debug message and returns
 * GST_VIDEO_FORMAT_UNKNOWN.
 *
 * @param rsFormat The RealSense video format to be converted.
 * @return GstVideoFormat The corresponding GStreamer video format, or GST_VIDEO_FORMAT_UNKNOWN if unsupported.
 */
static GstVideoFormat get_gst_video_format(rs2_format rsFormat) {
    switch (rsFormat) {
    case RS2_FORMAT_Z16:
        return GST_VIDEO_FORMAT_RGB16; // Format must be changed to GST_VIDEO_FORMAT_RGB_Z16 once it is fixed in
                                       // GStreamer code
    default:
        GST_DEBUG("gst_real_sense_start: Unsupported RealSense video format: %d\n", rsFormat);
        return GST_VIDEO_FORMAT_UNKNOWN;
    }
}

/**
 * gst_real_sense_get_caps:
 * @bsrc: (GstBaseSrc *) The base source element.
 * @filter: (GstCaps *) Optional filter caps to intersect with.
 *
 * Retrieves the capabilities (caps) supported by the RealSense source element.
 * This function obtains the pad template caps for the source pad, optionally
 * intersects them with a provided filter, and returns the resulting caps.
 * If the intersection results in no caps, an empty caps is returned.
 * The returned caps are made writable before being returned to the caller.
 *
 * Returns: (transfer full) (GstCaps *) The capabilities supported by the element,
 * possibly filtered, or an empty caps if none are supported.
 */
static GstCaps *gst_real_sense_get_caps(GstBaseSrc *bsrc, GstCaps *filter) {
    GstRealSense *src = GST_REAL_SENSE(bsrc);
    GstCaps *caps;

    caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));

    if (caps == nullptr) {
        return gst_caps_new_empty();
    }

    GST_DEBUG_OBJECT(src, "The caps before filtering");
    if (filter) {
        GstCaps *tmp = gst_caps_intersect(caps, filter);
        gst_caps_unref(caps);
        caps = tmp;
    }

    caps = gst_caps_make_writable(caps);
    return caps;
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "gvarealsense", GST_RANK_NONE, GST_TYPE_GVAREALSENSE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvarealsense, PRODUCT_FULL_NAME " gvarealsense element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)