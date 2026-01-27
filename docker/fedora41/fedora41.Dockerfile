# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# Build flow:
#                 fedora:41
#                     |
#                     |
#                     V
#                  builder -----------------------------------------------
#                     |                             |                    |
#                     |                             |                    |
#                     V                             |                    |
#                ffmpeg-builder                     V                    V
#                /           \                 kafka-builder     realsense-builder
#               V             V                     |                    |
#      gstreamer-builder  opencv-builder            |                    |
#                \            /                     |                    |
#      (copy libs)\          /(copy libs)           |                    |
#                  V        V        (copy libs)    |                    |
#                dlstreamer-dev <-------------------|--------------------|
#                      |
#                      |
#                      V
#                  deb-builder
#                      |
#                      | (copy rpms)
#                      V
#                  dlstreamer
# ==============================================================================
ARG DOCKER_REGISTRY
FROM ${DOCKER_REGISTRY}fedora:41 AS builder

ARG BUILD_ARG=Release

LABEL description="This is the development image of Deep Learning Streamer (DL Streamer) Pipeline Framework"
LABEL vendor="Intel Corporation"

ARG GST_VERSION=1.26.6
ARG FFMPEG_VERSION=6.1.1

ARG OPENVINO_VERSION=2025.4.0
ARG REALSENSE_VERSION=v2.57.5

ARG DLSTREAMER_VERSION=2025.2.0
ARG DLSTREAMER_BUILD_NUMBER

ENV DLSTREAMER_DIR=/home/dlstreamer/dlstreamer
ENV GSTREAMER_DIR=/opt/intel/dlstreamer/gstreamer
ENV INTEL_OPENVINO_DIR=/opt/intel/openvino_$OPENVINO_VERSION
ENV LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
ENV LIBVA_DRIVER_NAME=iHD
ENV GST_VA_ALL_DRIVERS=1

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# hadolint ignore=DL3041
RUN \
    dnf install -y \
    "https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
    "https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm" && \
    dnf install -y libva-utils xz python3-pip python3-gobject gcc gcc-c++ glibc-devel glib2-devel \
    flex bison autoconf automake libtool libogg-devel make libva-devel yasm mesa-libGL-devel libdrm-devel \
    python3-gobject-devel python3-devel tbb gnupg2 unzip gflags-devel openssl-devel openssl-devel-engine \
    gobject-introspection-devel x265-devel x264-devel libde265-devel libgudev-devel libusb1 libusb1-devel nasm python3-virtualenv \
    cairo-devel cairo-gobject-devel libXt-devel mesa-libGLES-devel wayland-protocols-devel libcurl-devel which \
    libssh2-devel cmake git valgrind numactl libvpx-devel opus-devel libsrtp-devel libXv-devel paho-c-devel ocl-icd-devel \
    kernel-headers pmix pmix-devel hwloc hwloc-libs hwloc-devel libxcb-devel libX11-devel libatomic intel-media-driver libsoup3 && \
    dnf clean all

RUN \
    useradd -ms /bin/bash dlstreamer && \
    mkdir /python3venv && \
    chown dlstreamer: /python3venv && \
    chmod u+w /python3venv

USER dlstreamer

RUN \
    python3 -m venv /python3venv && \
    /python3venv/bin/pip3 install --no-cache-dir --upgrade pip==24.0 && \
    /python3venv/bin/pip3 install --no-cache-dir --no-dependencies \
    meson==1.4.1 \
    ninja==1.11.1.1 \
    numpy==2.2.0 \
    tabulate==0.9.0 \
    tqdm==4.67.1 \
    junit-xml==1.9 \
    opencv-python==4.11.0.86 \
    XlsxWriter==3.2.0 \
    zxing-cpp==2.2.0 \
    pyzbar==0.1.9 \
    six==1.16.0 \
    pycairo==1.26.0 \
    PyGObject==3.50.0 \
    setuptools==78.1.1 \
    pytest==8.3.3 \
    pluggy==1.5.0 \
    exceptiongroup==1.2.2 \
    iniconfig==2.0.0

# hadolint ignore=DL3002
USER root

ENV PATH="/python3venv/bin:${PATH}"

# ==============================================================================
FROM builder AS ffmpeg-builder
#Build ffmpeg
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN \
    mkdir -p /src/ffmpeg && \
    curl -sSL --insecure "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.gz" -o "/src/ffmpeg/ffmpeg-${FFMPEG_VERSION}.tar.gz" && \
    tar -xf "/src/ffmpeg/ffmpeg-${FFMPEG_VERSION}.tar.gz" -C /src/ffmpeg && \
    rm "/src/ffmpeg/ffmpeg-${FFMPEG_VERSION}.tar.gz"

WORKDIR /src/ffmpeg/ffmpeg-${FFMPEG_VERSION}

RUN \
    ./configure \
    --enable-pic \
    --enable-shared \
    --enable-static \
    --enable-avfilter \
    --enable-vaapi \
    --extra-cflags="-I/include" \
    --extra-ldflags="-L/lib" \
    --extra-libs=-lpthread \
    --extra-libs=-lm \
    --bindir="/bin" && \
    make -j "$(nproc)" && \
    make install

ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

WORKDIR /copy_libs
RUN cp -a /usr/local/lib/libav* ./ && \
    cp -a /usr/local/lib/libswscale* ./ && \
    cp -a /usr/local/lib/libswresample* ./

# ==============================================================================
FROM ffmpeg-builder AS opencv-builder
# OpenCV
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

WORKDIR /

RUN \
    curl -sSL --insecure -o opencv.zip https://github.com/opencv/opencv/archive/4.12.0.zip && \
    curl -sSL --insecure -o opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.12.0.zip && \
    unzip opencv.zip && \
    unzip opencv_contrib.zip && \
    rm opencv.zip opencv_contrib.zip && \
    mv opencv-4.12.0 opencv && \
    mv opencv_contrib-4.12.0 opencv_contrib && \
    mkdir -p opencv/build

WORKDIR /opencv/build

RUN \
    cmake \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_opencv_apps=OFF \
    -DWITH_VA=ON \
    -DWITH_VA_INTEL=ON \
    -DWITH_FFMPEG=OFF \
    -DWITH_TBB=ON \
    -DWITH_OPENMP=OFF \
    -DWITH_IPP=OFF \
    -DOPENCV_EXTRA_MODULES_PATH=/opencv_contrib/modules \
    -DOPENCV_GENERATE_PKGCONFIG=YES \
    -GNinja .. && \
    ninja -j "$(nproc)" && \
    ninja install

WORKDIR /copy_libs
RUN cp -a /usr/local/lib64/libopencv* ./

# ==============================================================================
FROM opencv-builder AS gstreamer-builder
# Build GStreamer
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]
WORKDIR /home/dlstreamer

# Copy GStreamer patches
RUN mkdir -p /tmp/patches
COPY dependencies/patches/ /tmp/patches/

RUN \
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git

ENV PKG_CONFIG_PATH=/usr/lib64/pkgconfig/:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

RUN ldconfig

WORKDIR /home/dlstreamer/gstreamer

RUN \
    git switch -c "$GST_VERSION" "tags/$GST_VERSION" && \
    git apply /tmp/patches/*.patch && \
    meson setup \
    -Dexamples=disabled \
    -Dtests=disabled \
    -Dvaapi=enabled \
    -Dlibnice=enabled \
    -Dgst-examples=disabled \
    -Ddevtools=disabled \
    -Dorc=disabled \
    -Dgpl=enabled \
    -Dgst-plugins-base:nls=disabled \
    -Dgst-plugins-base:gl=disabled \
    -Dgst-plugins-base:xvideo=enabled \
    -Dgst-plugins-base:vorbis=enabled \
    -Dgst-plugins-base:pango=disabled \
    -Dgst-plugins-good:nls=disabled \
    -Dgst-plugins-good:libcaca=disabled \
    -Dgst-plugins-good:vpx=enabled \
    -Dgst-plugins-good:rtp=enabled \
    -Dgst-plugins-good:rtpmanager=enabled \
    -Dgst-plugins-good:adaptivedemux2=disabled \
    -Dgst-plugins-good:lame=disabled \
    -Dgst-plugins-good:flac=disabled \
    -Dgst-plugins-good:dv=disabled \
    -Dgst-plugins-good:soup=enabled \
    -Dgst-plugins-bad:gpl=enabled \
    -Dgst-plugins-bad:va=enabled \
    -Dgst-plugins-bad:doc=disabled \
    -Dgst-plugins-bad:nls=disabled \
    -Dgst-plugins-bad:neon=disabled \
    -Dgst-plugins-bad:directfb=disabled \
    -Dgst-plugins-bad:openni2=disabled \
    -Dgst-plugins-bad:fdkaac=disabled \
    -Dgst-plugins-bad:ladspa=disabled \
    -Dgst-plugins-bad:assrender=disabled \
    -Dgst-plugins-bad:bs2b=disabled \
    -Dgst-plugins-bad:flite=disabled \
    -Dgst-plugins-bad:rtmp=disabled \
    -Dgst-plugins-bad:opencv=enabled \
    -Dgst-plugins-bad:sbc=disabled \
    -Dgst-plugins-bad:teletext=disabled \
    -Dgst-plugins-bad:hls-crypto=openssl \
    -Dgst-plugins-bad:libde265=enabled \
    -Dgst-plugins-bad:openh264=enabled \
    -Dgst-plugins-bad:uvch264=enabled \
    -Dgst-plugins-bad:x265=enabled \
    -Dgst-plugins-bad:curl=enabled \
    -Dgst-plugins-bad:curl-ssh2=enabled \
    -Dgst-plugins-bad:opus=enabled \
    -Dgst-plugins-bad:dtls=enabled \
    -Dgst-plugins-bad:srtp=enabled \
    -Dgst-plugins-bad:webrtc=enabled \
    -Dgst-plugins-bad:webrtcdsp=disabled \
    -Dgst-plugins-bad:dash=disabled \
    -Dgst-plugins-bad:aja=disabled \
    -Dgst-plugins-bad:openjpeg=disabled \
    -Dgst-plugins-bad:analyticsoverlay=disabled \
    -Dgst-plugins-bad:closedcaption=disabled \
    -Dgst-plugins-bad:ttml=disabled \
    -Dgst-plugins-bad:codec2json=disabled \
    -Dgst-plugins-bad:qroverlay=disabled \
    -Dgst-plugins-bad:soundtouch=disabled \
    -Dgst-plugins-bad:isac=disabled \
    -Dgst-plugins-ugly:nls=disabled \
    -Dgst-plugins-ugly:x264=enabled \
    -Dgst-plugins-ugly:gpl=enabled \
    -Dgstreamer-vaapi:encoders=enabled \
    -Dgstreamer-vaapi:drm=enabled \
    -Dgstreamer-vaapi:glx=enabled \
    -Dgstreamer-vaapi:wayland=enabled \
    -Dgstreamer-vaapi:egl=enabled \
    --buildtype="${BUILD_ARG,}" \
    --prefix="${GSTREAMER_DIR}" \
    --libdir=lib/ \
    --libexecdir=bin/ \
    build/ && \
    ninja -C build && \
    meson install -C build/ && \
    rm -r subprojects/gst-devtools subprojects/gst-examples && \
    rm -rf /tmp/patches/

ENV PKG_CONFIG_PATH="${GSTREAMER_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH}"

# Installing gst-rswebrtc-plugins
ENV RUSTFLAGS="-L ${GSTREAMER_DIR}/lib"

WORKDIR $GSTREAMER_DIR/src/gst-plugins-rs
# hadolint ignore=SC1091
RUN \
    git clone https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git && \
    shopt -s dotglob && \
    mv gst-plugins-rs/* . && \
    git checkout "tags/gstreamer-$GST_VERSION" && \
    curl -sSL --insecure https://sh.rustup.rs | sh -s -- -y --default-toolchain 1.88.0 && \
    source "$HOME"/.cargo/env && \
    cargo install cargo-c --version=0.10.11 --locked && \
    cargo update && \
    cargo cinstall -p gst-plugin-webrtc -p gst-plugin-rtp --libdir="${GSTREAMER_DIR}"/lib/ && \
    rm "${GSTREAMER_DIR}"/lib/gstreamer-1.0/libgstrs*.a && \
    rustup self uninstall -y && \
    rm -rf ./* && \
    strip -g "${GSTREAMER_DIR}"/lib/gstreamer-1.0/libgstrs*.so

# ==============================================================================
FROM builder AS kafka-builder
# Build rdkafka
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN curl -sSL https://github.com/edenhill/librdkafka/archive/v2.3.0.tar.gz | tar -xz
WORKDIR /librdkafka-2.3.0
RUN ./configure && \
    make && make INSTALL=install install

WORKDIR /copy_libs
RUN cp -a /usr/local/lib/librdkafka* ./
# ==============================================================================

FROM builder AS realsense-builder
# Build rdkafka
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# Build librealsense
WORKDIR /home/dlstreamer

RUN dnf install -y systemd-devel gtk3-devel && \
    dnf clean all

RUN git clone https://github.com/IntelRealSense/librealsense.git librealsense

WORKDIR /home/dlstreamer/librealsense

RUN mkdir build

WORKDIR /home/dlstreamer/librealsense/build

RUN \
    git switch -c "$REALSENSE_VERSION" "tags/$REALSENSE_VERSION" && \
    cmake ../ -DCMAKE_BUILD_TYPE="${BUILD_ARG}" -DBUILD_EXAMPLES=false -DBUILD_GRAPHICAL_EXAMPLES=false && \
    make -j "$(nproc)" && \
    make install
WORKDIR /copy_libs
RUN cp -a /usr/local/lib64/librealsense* ./

# ==============================================================================

FROM builder AS dlstreamer-dev

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

COPY --from=ffmpeg-builder /copy_libs/ /usr/local/lib/
COPY --from=ffmpeg-builder /usr/local/lib/pkgconfig/libswresample* /usr/local/lib/pkgconfig/
COPY --from=ffmpeg-builder /usr/local/lib/pkgconfig/libav* /usr/local/lib/pkgconfig/
COPY --from=ffmpeg-builder /usr/local/lib/pkgconfig/libswscale* /usr/local/lib/pkgconfig/
COPY --from=ffmpeg-builder /usr/local/include/ /usr/local/include/
COPY --from=gstreamer-builder ${GSTREAMER_DIR} ${GSTREAMER_DIR}
COPY --from=opencv-builder /usr/local/include/opencv4 /usr/local/include/opencv4
COPY --from=opencv-builder /copy_libs/ /usr/local/lib64/
COPY --from=opencv-builder /usr/local/lib64/cmake/opencv4 /usr/local/lib64/cmake/opencv4
COPY --from=kafka-builder /copy_libs/ /usr/local/lib/
COPY --from=kafka-builder /usr/local/include/librdkafka /usr/local/include/librdkafka
COPY --from=realsense-builder /copy_libs/ /usr/local/lib64/
COPY --from=realsense-builder /usr/local/include/librealsense2 /usr/local/include/librealsense2

# Intel® Distribution of OpenVINO™ Toolkit
RUN \
    printf "[OpenVINO]\n\
name=Intel(R) Distribution of OpenVINO\n\
baseurl=https://yum.repos.intel.com/openvino\n\
enabled=1\n\
gpgcheck=1\n\
repo_gpgcheck=1\n\
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB\n" >/tmp/openvino.repo && \
    mv /tmp/openvino.repo /etc/yum.repos.d

RUN dnf install -y "openvino-${OPENVINO_VERSION}" && \
    dnf clean all


# Deep Learning Streamer
WORKDIR "$DLSTREAMER_DIR"

COPY . "${DLSTREAMER_DIR}"

WORKDIR $DLSTREAMER_DIR/build

# DLStreamer environment variables
ENV LIBDIR=${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}/lib
ENV BINDIR=${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}/bin
ENV PATH=${GSTREAMER_DIR}/bin:${BINDIR}:${PATH}
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:${LIBDIR}/pkgconfig:/usr/lib64/pkgconfig:${GSTREAMER_DIR}/lib/pkgconfig:/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}
ENV LIBRARY_PATH=${GSTREAMER_DIR}/lib:${LIBDIR}:/usr/lib:/usr/local/lib:${LIBRARY_PATH}
ENV LD_LIBRARY_PATH=${GSTREAMER_DIR}/lib:${LIBDIR}:/usr/lib:/usr/local/lib:/usr/local/lib64/:${LD_LIBRARY_PATH}
ENV LIB_PATH=$LIBDIR
ENV GST_PLUGIN_PATH=${LIBDIR}:${GSTREAMER_DIR}/lib/gstreamer-1.0:/usr/lib64/gstreamer-1.0:${GST_PLUGIN_PATH}
ENV LC_NUMERIC=C
ENV C_INCLUDE_PATH=/usr/local/include:${DLSTREAMER_DIR}/include:${DLSTREAMER_DIR}/include/dlstreamer/gst/metadata:${C_INCLUDE_PATH}
ENV CPLUS_INCLUDE_PATH=/usr/local/include:${DLSTREAMER_DIR}/include:${DLSTREAMER_DIR}/include/dlstreamer/gst/metadata:${CPLUS_INCLUDE_PATH}
ENV GST_PLUGIN_SCANNER=${GSTREAMER_DIR}/bin/gstreamer-1.0/gst-plugin-scanner
ENV GI_TYPELIB_PATH=${GSTREAMER_DIR}/lib/girepository-1.0
ENV PYTHONPATH=${GSTREAMER_DIR}/lib/python3/dist-packages:${DLSTREAMER_DIR}/python:${PYTHONPATH}

# Build DLStreamer
RUN \
    if [ "${BUILD_ARG}" == "Debug" ]; then \
        C_FLAGS="-Og -g"; \
        CXX_FLAGS="-Og -g -Wno-error=deprecated-enum-enum-conversion"; \
    else \
        C_FLAGS=""; \
        CXX_FLAGS="-Wno-error=deprecated-enum-enum-conversion"; \
    fi && \
    cmake \
        -DCMAKE_BUILD_TYPE="${BUILD_ARG}" \
        -DCMAKE_C_FLAGS="${C_FLAGS}" \
        -DCMAKE_CXX_FLAGS="${CXX_FLAGS}" \
        -DENABLE_PAHO_INSTALLATION=ON \
        -DENABLE_RDKAFKA_INSTALLATION=ON \
        -DENABLE_VAAPI=ON \
        -DENABLE_SAMPLES=ON \
        -DENABLE_REALSENSE=ON \
        .. && \
    make -j "$(nproc)" && \
    usermod -a -G video dlstreamer && \
    chown -R dlstreamer:dlstreamer /home/dlstreamer

WORKDIR /home/dlstreamer
USER dlstreamer

# ==============================================================================
FROM dlstreamer-dev AS rpm-builder

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# hadolint ignore=DL3002
USER root
ENV USER=dlstreamer
ENV RPM_PKG_NAME=intel-dlstreamer-${DLSTREAMER_VERSION}

# hadolint ignore=DL3041
RUN \
    dnf install -y rpmdevtools patchelf && \
    dnf clean all

RUN \
    mkdir -p /${RPM_PKG_NAME}/opt/intel/ && \
    mkdir -p /${RPM_PKG_NAME}/opt/opencv/include && \
    mkdir -p /${RPM_PKG_NAME}/opt/rdkafka && \
    mkdir -p /${RPM_PKG_NAME}/opt/ffmpeg && \
    mkdir -p /${RPM_PKG_NAME}/opt/dlstreamer && \
    mkdir -p /${RPM_PKG_NAME}/opt/librealsense && \
    cp -r "${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}" /${RPM_PKG_NAME}/opt/intel/dlstreamer && \
    cp -r "${DLSTREAMER_DIR}/samples/" /${RPM_PKG_NAME}/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/python/" /${RPM_PKG_NAME}/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/scripts/" /${RPM_PKG_NAME}/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/include/" /${RPM_PKG_NAME}/opt/intel/dlstreamer/ && \
    cp "${DLSTREAMER_DIR}/README.md" /${RPM_PKG_NAME}/opt/intel/dlstreamer && \
    cp -rT "${GSTREAMER_DIR}" /${RPM_PKG_NAME}/opt/intel/dlstreamer/gstreamer && \
    cp -a /usr/local/lib64/libopencv* /${RPM_PKG_NAME}/opt/opencv/ && \
    cp -a /usr/local/lib/librdkafka* /${RPM_PKG_NAME}/opt/rdkafka/ && \
    cp -a /usr/local/lib64/librealsense* /${RPM_PKG_NAME}/opt/librealsense/ && \
    find /usr/local/lib -regextype grep -regex ".*libav.*so\.[0-9]*$" -exec cp {} /${RPM_PKG_NAME}/opt/ffmpeg \; && \
    find /usr/local/lib -regextype grep -regex ".*libswscale.*so\.[0-9]*$" -exec cp {} /${RPM_PKG_NAME}/opt/ffmpeg \; && \
    find /usr/local/lib -regextype grep -regex ".*libswresample.*so\.[0-9]*$" -exec cp {} /${RPM_PKG_NAME}/opt/ffmpeg \; && \
    cp -r /usr/local/include/opencv4/* /${RPM_PKG_NAME}/opt/opencv/include && \
    cp "${DLSTREAMER_DIR}"/LICENSE /${RPM_PKG_NAME}/ && \
    rpmdev-setuptree && \
    tar -czf ~/rpmbuild/SOURCES/${RPM_PKG_NAME}.tar.gz -C / ${RPM_PKG_NAME} && \
    cp "${DLSTREAMER_DIR}"/docker/fedora41/intel-dlstreamer.spec ~/rpmbuild/SPECS/ && \
    sed -i -e "s/DLSTREAMER_VERSION/${DLSTREAMER_VERSION}/g" ~/rpmbuild/SPECS/intel-dlstreamer.spec && \
    sed -i -e "s/CURRENT_DATE_TIME/$(date '+%a %b %d %Y')/g" ~/rpmbuild/SPECS/intel-dlstreamer.spec && \
    find /${RPM_PKG_NAME}/opt/intel/dlstreamer/bin -type f ! -name "liblibrarymock1.so" ! -name "liblibrarymock2.so" ! -name "draw_face_attributes" -exec rm -f {} + && \
    find /${RPM_PKG_NAME}/opt/intel/dlstreamer/bin -type d -empty -delete && \
    find /${RPM_PKG_NAME}/opt/intel -name "*.a" -delete && \
    rpmbuild -bb ~/rpmbuild/SPECS/intel-dlstreamer.spec

RUN mkdir /rpms && \
    cp ~/rpmbuild/RPMS/x86_64/${RPM_PKG_NAME}* "/rpms/${RPM_PKG_NAME}.${DLSTREAMER_BUILD_NUMBER}-1.fc41.x86_64.rpm"

# ==============================================================================
ARG DOCKER_REGISTRY
FROM ${DOCKER_REGISTRY}fedora:41 AS dlstreamer

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN \
    dnf install -y \
    "https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
    "https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm" && \
    dnf clean all

RUN \
    printf "[OpenVINO]\n\
name=Intel(R) Distribution of OpenVINO\n\
baseurl=https://yum.repos.intel.com/openvino\n\
enabled=1\n\
gpgcheck=1\n\
repo_gpgcheck=1\n\
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB\n" >/tmp/openvino.repo && \
    mv /tmp/openvino.repo /etc/yum.repos.d

RUN mkdir -p /rpms

COPY --from=rpm-builder /rpms/*.rpm /rpms/

# Download and install DLS rpm package
RUN \
    dnf install -y /rpms/*.rpm && \
    dnf clean all && \
    useradd -ms /bin/bash dlstreamer && \
    chown -R dlstreamer: /opt && \
    chmod -R u+rw /opt

ENV LIBVA_DRIVER_NAME=iHD
ENV GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
ENV LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/usr/lib:/opt/intel/dlstreamer/lib:/opt/opencv:/opt/rdkafka:/opt/ffmpeg:/opt/librealsense:/usr/local/lib
ENV LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
ENV GST_VA_ALL_DRIVERS=1
ENV MODEL_PROC_PATH=/opt/intel/dlstreamer/samples/gstreamer/model_proc
ENV PATH=/python3venv/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
ENV PYTHONPATH=/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/home/dlstreamer/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:
ENV TERM=xterm
ENV GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib64/girepository-1.0

RUN \
    usermod -a -G video dlstreamer && \
    ln -s /opt/intel/dlstreamer /home/dlstreamer/dlstreamer

WORKDIR /home/dlstreamer
USER dlstreamer

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD [ "bash", "-c", "pgrep bash > /dev/null || exit 1" ]

CMD ["/bin/bash"]
