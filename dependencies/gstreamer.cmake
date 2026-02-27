# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

include(ExternalProject)

# When changing version, you will also need to change the download hash
set(DESIRED_VERSION 1.26.6)

# Note: the dependency scripts follow a template, this is left here should other
# dependencies be added in the future and this file used as a reference.
#
# find_package(PkgConfig)
# pkg_check_modules(GSTREAMER gstreamer-1.0=${DESIRED_VERSION})

# if (GSTREAMER_FOUND)
#     return()
# endif()

ExternalProject_Add(
	gstreamer
	PREFIX ${CMAKE_BINARY_DIR}/gstreamer
	GIT_REPOSITORY  https://gitlab.freedesktop.org/gstreamer/gstreamer.git
	GIT_TAG         ${DESIRED_VERSION}
	# Use external script for patch to avoid quoting issues; script handles idempotency and logging
	PATCH_COMMAND bash -c "bash ${CMAKE_CURRENT_SOURCE_DIR}/patches/apply_gst_patch.sh $(find ${CMAKE_CURRENT_SOURCE_DIR}/patches -maxdepth 1 -name '*.patch' -type f | sort)"
    BUILD_COMMAND       ninja
    INSTALL_COMMAND     meson install
    TEST_COMMAND        ""
    CONFIGURE_COMMAND   meson setup
                        --prefix ${CMAKE_BINARY_DIR}/gstreamer-bin
		                -Dexamples=disabled
    	                -Dtests=disabled
    	                -Dvaapi=enabled
    	                -Dlibnice=enabled
    	                -Dgst-examples=disabled
    	                -Ddevtools=disabled
    	                -Dorc=disabled
    	                -Dgpl=disabled
                      -Dpython=enabled
    	                -Dgst-plugins-base:nls=disabled
    	                -Dgst-plugins-base:gl=disabled
    	                -Dgst-plugins-base:xvideo=enabled
    	                -Dgst-plugins-base:vorbis=enabled
    	                -Dgst-plugins-base:pango=disabled
    	                -Dgst-plugins-good:nls=disabled
    	                -Dgst-plugins-good:libcaca=disabled
    	                -Dgst-plugins-good:vpx=enabled
    	                -Dgst-plugins-good:rtp=enabled
    	                -Dgst-plugins-good:rtpmanager=enabled
    	                -Dgst-plugins-good:adaptivedemux2=disabled
    	                -Dgst-plugins-good:lame=disabled
    	                -Dgst-plugins-good:flac=disabled
    	                -Dgst-plugins-good:dv=disabled
    	                -Dgst-plugins-good:soup=enabled
    	                -Dgst-plugins-bad:gpl=disabled
    	                -Dgst-plugins-bad:va=enabled
    	                -Dgst-plugins-bad:doc=disabled
    	                -Dgst-plugins-bad:nls=disabled
    	                -Dgst-plugins-bad:neon=disabled
    	                -Dgst-plugins-bad:directfb=disabled
    	                -Dgst-plugins-bad:openni2=disabled
    	                -Dgst-plugins-bad:fdkaac=disabled
    	                -Dgst-plugins-bad:ladspa=disabled
    	                -Dgst-plugins-bad:assrender=disabled
    	                -Dgst-plugins-bad:bs2b=disabled
    	                -Dgst-plugins-bad:flite=disabled
    	                -Dgst-plugins-bad:rtmp=disabled
    	                -Dgst-plugins-bad:sbc=disabled
    	                -Dgst-plugins-bad:teletext=disabled
    	                -Dgst-plugins-bad:hls-crypto=openssl
    	                -Dgst-plugins-bad:libde265=enabled
    	                -Dgst-plugins-bad:openh264=enabled
    	                -Dgst-plugins-bad:uvch264=enabled
    	                -Dgst-plugins-bad:x265=disabled
    	                -Dgst-plugins-bad:curl=enabled
    	                -Dgst-plugins-bad:curl-ssh2=enabled
    	                -Dgst-plugins-bad:opus=enabled
    	                -Dgst-plugins-bad:dtls=enabled
    	                -Dgst-plugins-bad:srtp=enabled
    	                -Dgst-plugins-bad:webrtc=enabled
    	                -Dgst-plugins-bad:webrtcdsp=disabled
    	                -Dgst-plugins-bad:dash=disabled
    	                -Dgst-plugins-bad:aja=disabled
    	                -Dgst-plugins-bad:openjpeg=disabled
    	                -Dgst-plugins-bad:analyticsoverlay=disabled
    	                -Dgst-plugins-bad:closedcaption=disabled
    	                -Dgst-plugins-bad:ttml=disabled
    	                -Dgst-plugins-bad:codec2json=disabled
    	                -Dgst-plugins-bad:qroverlay=disabled
    	                -Dgst-plugins-bad:soundtouch=disabled
    	                -Dgst-plugins-bad:isac=disabled
		                -Dgst-plugins-bad:openexr=disabled
    	                -Dgst-plugins-ugly:nls=disabled
    	                -Dgst-plugins-ugly:x264=disabled
    	                -Dgst-plugins-ugly:gpl=disabled
    	                -Dgstreamer-vaapi:encoders=enabled
    	                -Dgstreamer-vaapi:drm=enabled
    	                -Dgstreamer-vaapi:glx=enabled
    	                -Dgstreamer-vaapi:wayland=enabled
    	                -Dgstreamer-vaapi:egl=enabled
		                --buildtype=release
		                --libdir=lib/
		                --libexecdir=bin/
                        <SOURCE_DIR>
)

if (INSTALL_DLSTREAMER)
    execute_process(COMMAND mkdir -p ${DLSTREAMER_INSTALL_PREFIX}/gstreamer
                    COMMAND cp -r ${CMAKE_BINARY_DIR}/gstreamer-bin/. ${DLSTREAMER_INSTALL_PREFIX}/gstreamer)
endif()
