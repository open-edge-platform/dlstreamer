# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# GStreamer Source Archives
# Shared definitions for the upstream GStreamer source tarballs bundled with the
# installer. Included by both download_deps.cmake (to fetch them) and
# install_targets.cmake (to package them), so the module list and hashes live in
# one place. Requires GSTREAMER_VERSION and DLSTREAMER_DEPS_DIR to be set.

set(GSTREAMER_SOURCE_BASE_URL "https://gstreamer.freedesktop.org/src")

# Modules redistributed by the bundled GStreamer runtime installer. All of
# them follow the GStreamer release version, so the tarball name is always
# <module>-${GSTREAMER_VERSION}.tar.xz
set(GSTREAMER_SOURCE_MODULES
    gstreamer
    gst-plugins-base
    gst-plugins-good
    gst-plugins-bad
    gst-plugins-ugly
    gst-libav
    gst-rtsp-server
    gst-editing-services
    gst-devtools
    gst-python
)

# Get each hash from
# ${GSTREAMER_SOURCE_BASE_URL}/<module>/<module>-${GSTREAMER_VERSION}.tar.xz.sha256sum
# Refresh all of them whenever GSTREAMER_VERSION changes; the
# update_gstreamer_source_hashes target prints a ready-to-paste block.
set(GSTREAMER_SRC_HASH_gstreamer            "ce5cd44d4ffeafdcc3dddaa072b2179c0b7cb1abf4e6c5d18d4375f8a39fe491")
set(GSTREAMER_SRC_HASH_gst-plugins-base     "4db76b3619280037a4047de7d9dbb38613a4272dcc40efb333257124635a888d")
set(GSTREAMER_SRC_HASH_gst-plugins-good     "1ace2d8ec74f632d82eab5006753a27fe0c2402db4ca94d63271e494b62f50bf")
set(GSTREAMER_SRC_HASH_gst-plugins-bad      "6467e3964828f4d7d08bfe1fbb4d76287a1c8fa76674e59e101a149c020fefd7")
set(GSTREAMER_SRC_HASH_gst-plugins-ugly     "fe39a5ee7115e37de9eb65d899ec84c93e6e26ed3ffe25c6d5176cececbab572")
set(GSTREAMER_SRC_HASH_gst-libav            "45ba65535870aa7c026119d2e90b35dc760e1cf6f50bffbfe8d71223a3043a4e")
set(GSTREAMER_SRC_HASH_gst-rtsp-server      "917c58b9ff14f91a6b5cd1c3af16c9fcfdf5d8d78d3d167c7e8fa5bdda35f947")
set(GSTREAMER_SRC_HASH_gst-editing-services "ca1236f7e7364fc2734bb204d016bb74c7be9f0fc2a646e78a9449e21bda88fb")
set(GSTREAMER_SRC_HASH_gst-devtools         "8e012bdcb55503f466d53f1f05e13e8993c69811b9db77cd16a8f6467723bf91")
set(GSTREAMER_SRC_HASH_gst-python           "12fdd8e19af97d797a6b2c195228e6c9edc4cddfa68274912b78ef66068ad822")

# Where the tarballs are staged at build time and installed at runtime
set(GSTREAMER_SOURCE_STAGE_DIR "${DLSTREAMER_DEPS_DIR}/src")
set(GSTREAMER_SOURCE_INSTALL_DIR "src/gstreamer")
