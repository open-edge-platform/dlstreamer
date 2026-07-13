/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * Vendor-neutral C ABI between g3dlidarsrc and a LiDAR backend.
 *
 * g3dlidarsrc has no compile-time dependency on any vendor SDK. Each vendor is
 * implemented as a separate backend shared library (e.g. libg3dlidar_robosense.so)
 * that implements the flat C functions declared (commented out) below. At
 * start(), the element derives the backend library name from the config's
 * `vendor` field (libg3dlidar_<vendor>.so), dlopen()s it, and resolves the
 * functions via dlsym into function pointers.
 *
 * The ABI is deliberately vendor-neutral: it carries only fields common to all
 * UDP LiDAR backends (model, bind address, timestamp policy). Vendor-specific
 * options (port numbers, range clipping, SDK toggles, ...) are passed through
 * verbatim as the raw `params` JSON object string and parsed by each backend
 * itself -- the element does not need to know any vendor's parameter schema.
 *
 * A backend library must:
 *   - implement every function below with C linkage (extern "C"),
 *   - contain all vendor-SDK code and threading internally,
 *   - be named libg3dlidar_<vendor>.so, where <vendor> matches the config.
 */

#ifndef G3D_LIDAR_BACKEND_API_H
#define G3D_LIDAR_BACKEND_API_H

#include <stdint.h>

/* Backends are built in a subtree compiled with -fvisibility=hidden, so the
 * ABI entry points must be explicitly marked default-visibility -- otherwise
 * they are not placed in the .dynsym table and the element's dlsym() calls fail
 * to resolve them. Every backend applies this macro to its function definitions. */
#if defined(_WIN32)
#define G3D_LIDAR_BACKEND_EXPORT __declspec(dllexport)
#else
#define G3D_LIDAR_BACKEND_EXPORT __attribute__((visibility("default")))
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* Error codes returned by the backend entry points. */
typedef enum g3d_lidar_error_code {
    G3D_LIDAR_OK = 0,
    G3D_LIDAR_NULLPTR,     /* a required argument was NULL */
    G3D_LIDAR_BAD_PARAMS,  /* params JSON invalid / unknown key / bad model */
    G3D_LIDAR_INIT_FAIL,   /* vendor SDK init() failed */
    G3D_LIDAR_START_FAIL,  /* vendor SDK start() failed */
} g3d_lidar_error_code;

/* Opaque backend instance (wraps the vendor SDK driver). */
typedef struct g3d_lidar_backend_handle g3d_lidar_backend_handle;

/* Vendor-neutral configuration. Vendor-specific options are NOT here -- they
 * ride in `params_json` and are parsed by the backend. Booleans are int (0/1)
 * to keep the ABI plain C. */
typedef struct g3d_lidar_params {
    const char *model;        /* vendor model string, e.g. "RSE1" */
    const char *bind_address; /* local NIC IP to bind() the UDP socket to */
    int use_lidar_clock;      /* timestamp source: device clock (1) vs host (0) */
    const char *params_json;  /* raw config "params" object as a JSON string
                               * (may be NULL or "{}"); backend parses its own keys */
} g3d_lidar_params;

/* One completed point-cloud frame delivered to the cloud callback. The xyzi
 * block holds point_count * 4 floats laid out as (x, y, z, intensity) per point.
 * The pointer is owned by the backend and is only valid for the duration of the
 * callback -- the consumer must copy what it needs before returning. */
typedef struct g3d_lidar_frame {
    const float *xyzi;
    uint32_t point_count;
    double timestamp; /* frame timestamp in seconds (device or host clock) */
} g3d_lidar_frame;

/* Callback invoked by the backend (from its receive thread) once per completed
 * frame. `user` is the opaque pointer passed to set_callbacks. */
typedef void (*g3d_lidar_cloud_cb)(void *user, const g3d_lidar_frame *frame);

/* Callback invoked by the backend for SDK error/exception notifications. */
typedef void (*g3d_lidar_error_cb)(void *user, const char *msg);

/* Function declarations commented out - resolved via dlopen()/dlsym() by
 * g3dlidarsrc instead of being linked. Every backend defines them with exactly
 * these signatures, C linkage, and the G3D_LIDAR_BACKEND_EXPORT attribute (the
 * backend subtree is built with -fvisibility=hidden, so the attribute is
 * required for dlsym to find them).
 *
 * // Create a backend instance. Returns NULL on allocation failure.
 * g3d_lidar_backend_handle *g3d_lidar_backend_create(void);
 *
 * // Register the frame and error callbacks before init/start. `user` is passed
 * // back verbatim to both callbacks.
 * g3d_lidar_error_code g3d_lidar_backend_set_callbacks(g3d_lidar_backend_handle *h,
 *                                                      g3d_lidar_cloud_cb on_cloud,
 *                                                      g3d_lidar_error_cb on_error,
 *                                                      void *user);
 *
 * // Configure the driver. On G3D_LIDAR_BAD_PARAMS / *_FAIL, a human-readable
 * // reason is written to err_buf (if non-NULL and err_size > 0) for the element
 * // to surface. Returns G3D_LIDAR_OK on success.
 * g3d_lidar_error_code g3d_lidar_backend_init(g3d_lidar_backend_handle *h,
 *                                             const g3d_lidar_params *params,
 *                                             char *err_buf, int err_size);
 *
 * // Begin receiving. Frames arrive asynchronously via the cloud callback.
 * g3d_lidar_error_code g3d_lidar_backend_start(g3d_lidar_backend_handle *h);
 *
 * // Stop receiving. Safe to call if not started.
 * void g3d_lidar_backend_stop(g3d_lidar_backend_handle *h);
 *
 * // Stop (if needed) and destroy the instance.
 * void g3d_lidar_backend_destroy(g3d_lidar_backend_handle *h);
 */

#if defined(__cplusplus)
}
#endif

#endif /* G3D_LIDAR_BACKEND_API_H */
