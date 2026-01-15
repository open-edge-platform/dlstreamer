# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# Build flow:
#                ubuntu:24.04
#                     |
#                     |
#                     V
#                  builder -----------------------------------------------
#                 /       \                         |                    |
#                /         \                        |                    |
#               V           V                       |                    |
#      gstreamer-builder   opencv-builder           V                    V
#               |              |                kafka-builder    realsense-builder
#               |              |                    |                    |
#    (copy libs) \            / (copy libs)         |                    |
#                 \          /                      |                    |
#                  V        V       (copy libs)     |                    |
#                dlstreamer-dev <-------------------|--------------------|
#                      |
#                      |
#                      V
#                  deb-builder
#                      |
#                      | (copy debs)
#                      V
#                  dlstreamer
# ==============================================================================
ARG DOCKER_REGISTRY
FROM ${DOCKER_REGISTRY}ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_ARG=Release

LABEL description="This is the development image of Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework"
LABEL vendor="Intel Corporation"

ARG GST_VERSION=1.26.6
ARG OPENVINO_VERSION=2025.4.0
ARG REALSENSE_VERSION=v2.57.5

ARG DLSTREAMER_VERSION=2025.2.0
ARG DLSTREAMER_BUILD_NUMBER

ENV DLSTREAMER_DIR=/home/dlstreamer/dlstreamer
ENV GSTREAMER_DIR=/opt/intel/dlstreamer/gstreamer
ENV LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
ENV LIBVA_DRIVER_NAME=iHD
ENV GST_VA_ALL_DRIVERS=1

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN userdel -r ubuntu

RUN \
    apt-get update && \
    apt-get install -y -q --no-install-recommends libtbb12=\* curl=\* gpg=\* ca-certificates=\* && \
    rm -rf /var/lib/apt/lists/*

# Intel GPU client drivers and prerequisites installation
RUN \
    apt-get update && \
    apt-get install -y --no-install-recommends software-properties-common=\* && \
    add-apt-repository -y ppa:kobuk-team/intel-graphics && \
    apt-get update && \
    echo "Snapshot: 20251125T030400Z" >> /etc/apt/sources.list.d/kobuk-team-ubuntu-intel-graphics-noble.sources && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    intel-metrics-discovery=\* intel-gsc=\* libvpl2=\* \
    libze-intel-gpu1=25.40.35563.7-1~24.04~ppa1 libze1=1.24.3-1~24.04~ppa1 intel-opencl-icd=25.40.35563.7-1~24.04~ppa1 clinfo=3.0.23.01.25-1build1 \
    intel-media-va-driver-non-free=25.4.2-1~24.04~ppa1 libmfx-gen1=25.4.0-0ubuntu1~24.04~ppa1 libvpl-tools=1.4.0-0ubuntu1~24.04~ppa1 libva-glx2=2.22.0-1ubuntu1~24.04~ppa1 va-driver-all=2.22.0-1ubuntu1~24.04~ppa1 vainfo=2.22.0-0ubuntu1~24.04~ppa1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Intel NPU drivers and prerequisites installation
WORKDIR /tmp/npu_deps

RUN curl -LO https://github.com/intel/linux-npu-driver/releases/download/v1.28.0/linux-npu-driver-v1.28.0.20251218-20347000698-ubuntu2404.tar.gz && \
    tar -xf linux-npu-driver-v1.28.0.20251218-20347000698-ubuntu2404.tar.gz && \
    dpkg -i ./*.deb && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/npu_deps

WORKDIR /

RUN \
    apt-get update && \
    apt-get install -y -q --no-install-recommends xz-utils=\* python3-pip=\* python3-gi=\* gcc-multilib=\* libglib2.0-dev=\* \
    flex=\* bison=\* autoconf=\* automake=\* libtool=\* libogg-dev=\* make=\* g++=\* libva-dev=\* yasm=\* libglx-dev=\* libdrm-dev=\* \
    python-gi-dev=\* python3-dev=\* unzip=\* libgflags-dev=\* libcurl4-openssl-dev=\* \
    libgirepository1.0-dev=\* libx265-dev=\* libx264-dev=\* libde265-dev=\* gudev-1.0=\* libusb-1.0=\* nasm=\* python3-venv=\* \
    libcairo2-dev=\* libxt-dev=\* libgirepository1.0-dev=\* libgles2-mesa-dev=\* wayland-protocols=\* \
    libssh2-1-dev=\* cmake=\* git=\* valgrind=\* numactl=\* libvpx-dev=\* libopus-dev=\* libsrtp2-dev=\* libxv-dev=\* \
    linux-libc-dev=\* libpmix2t64=\* libhwloc15=\* libhwloc-plugins=\* libxcb1-dev=\* libx11-xcb-dev=\* \
    ffmpeg=\* libpaho-mqtt-dev=\* libpostproc-dev=\* libavfilter-dev=\* libavdevice-dev=\* \
    libswscale-dev=\* libswresample-dev=\* libavutil-dev=\* libavformat-dev=\* libavcodec-dev=\* libxml2-dev=\* libsoup-3.0-0=\* \
    ocl-icd-opencl-dev=\* &&  \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN \
    useradd -ms /bin/bash dlstreamer && \
    mkdir /python3venv && \
    chown dlstreamer: /python3venv && \
    chmod u+w /python3venv

USER dlstreamer

RUN \
    python3 -m venv /python3venv && \
    /python3venv/bin/pip3 install --no-cache-dir --upgrade pip==25.3 && \
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
FROM builder AS opencv-builder

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# Build OpenCV
WORKDIR /

RUN \
    curl -sSL -o opencv.zip https://github.com/opencv/opencv/archive/4.12.0.zip && \
    curl -sSL -o opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.12.0.zip && \
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
RUN cp -a /usr/local/lib/libopencv* ./

# ==============================================================================

FROM opencv-builder AS gstreamer-builder

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# Copy GStreamer patches
RUN mkdir -p /tmp/patches
COPY dependencies/patches/ /tmp/patches/

# Build GStreamer
WORKDIR /home/dlstreamer

RUN \
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git

    ENV PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig/:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

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
    curl -sSL --insecure https://sh.rustup.rs | sh -s -- -y --default-toolchain 1.86.0 && \
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

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# Build librdkafka
RUN curl -sSL https://github.com/edenhill/librdkafka/archive/v2.12.1.tar.gz | tar -xz
WORKDIR /librdkafka-2.12.1
RUN ./configure &&\
    make && make install

WORKDIR /copy_libs
RUN cp -a /usr/local/lib/librdkafka* ./

# ==============================================================================

FROM builder AS realsense-builder
SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

# Build librealsense
WORKDIR /home/dlstreamer

RUN apt-get update && apt-get install -y --no-install-recommends libssl-dev=\* libusb-1.0-0-dev=\* libudev-dev=\* pkg-config=\* libgtk-3-dev=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

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
RUN cp -a /usr/local/lib/librealsense* ./

# ==============================================================================

FROM builder AS dlstreamer-dev

# DL Streamer development image and build proccess

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

COPY --from=gstreamer-builder ${GSTREAMER_DIR} ${GSTREAMER_DIR}
COPY --from=opencv-builder /usr/local/include/opencv4 /usr/local/include/opencv4
COPY --from=opencv-builder /copy_libs/ /usr/local/lib/
COPY --from=opencv-builder /usr/local/lib/cmake/opencv4 /usr/local/lib/cmake/opencv4
COPY --from=kafka-builder /usr/local/include/librdkafka /usr/local/include/librdkafka
COPY --from=kafka-builder /copy_libs/ /usr/local/lib/
COPY --from=realsense-builder /copy_libs/ /usr/local/lib/
COPY --from=realsense-builder /usr/local/include/librealsense2 /usr/local/include/librealsense2

# Build DL Streamer
WORKDIR /home/dlstreamer

RUN apt-get update && apt-get install --no-install-recommends -y gnupg=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*
RUN \
    echo "deb https://apt.repos.intel.com/openvino/2025 ubuntu24 main" | tee /etc/apt/sources.list.d/intel-openvino-2025.list && \
    curl -sSL -O https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB && \
    apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB && \
    apt-get update && apt-get install --no-install-recommends -y "openvino-${OPENVINO_VERSION}"=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# OpenVINO Gen AI
ARG OPENVINO_GENAI_VER=openvino_genai_ubuntu24_${OPENVINO_VERSION}.0_x86_64
ARG OPENVINO_GENAI_PKG=https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/2025.4/linux/${OPENVINO_GENAI_VER}.tar.gz

RUN curl -L ${OPENVINO_GENAI_PKG} | tar -xz && \
    mv ${OPENVINO_GENAI_VER} /opt/intel/openvino_genai

WORKDIR "$DLSTREAMER_DIR"

COPY . "${DLSTREAMER_DIR}"

RUN mkdir build

WORKDIR $DLSTREAMER_DIR/build


# DL Streamer environment variables
ENV LIBDIR=${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}/lib
ENV BINDIR=${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}/bin
ENV PATH=${GSTREAMER_DIR}/bin:${BINDIR}:${PATH}
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:${LIBDIR}/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:${GSTREAMER_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH}
ENV LIBRARY_PATH=${GSTREAMER_DIR}/lib:${LIBDIR}:/usr/lib:/usr/local/lib:${LIBRARY_PATH}
ENV LD_LIBRARY_PATH=${GSTREAMER_DIR}/lib:${LIBDIR}:/usr/lib:/usr/local/lib:${LD_LIBRARY_PATH}
ENV LIB_PATH=$LIBDIR
ENV GST_PLUGIN_PATH=${LIBDIR}:${GSTREAMER_DIR}/lib/gstreamer-1.0:/usr/lib/x86_64-linux-gnu/gstreamer-1.0:${GST_PLUGIN_PATH}
ENV LC_NUMERIC=C
ENV C_INCLUDE_PATH=/usr/local/include:${DLSTREAMER_DIR}/include:${DLSTREAMER_DIR}/include/dlstreamer/gst/metadata:${C_INCLUDE_PATH}
ENV CPLUS_INCLUDE_PATH=/usr/local/include:${DLSTREAMER_DIR}/include:${DLSTREAMER_DIR}/include/dlstreamer/gst/metadata:${CPLUS_INCLUDE_PATH}
ENV GST_PLUGIN_SCANNER=${GSTREAMER_DIR}/bin/gstreamer-1.0/gst-plugin-scanner
ENV GI_TYPELIB_PATH=${GSTREAMER_DIR}/lib/girepository-1.0
ENV PYTHONPATH=${GSTREAMER_DIR}/lib/python3/dist-packages:${DLSTREAMER_DIR}/python:${PYTHONPATH}

# Build DLStreamer
# hadolint ignore=SC1091
RUN \
    source /opt/intel/openvino_genai/setupvars.sh && \
    cmake \
       -DCMAKE_BUILD_TYPE="${BUILD_ARG}" \
       -DENABLE_PAHO_INSTALLATION=ON \
       -DENABLE_RDKAFKA_INSTALLATION=ON \
       -DENABLE_VAAPI=ON \
       -DENABLE_SAMPLES=ON \
       -DENABLE_REALSENSE=ON \
       -DENABLE_GENAI=ON \
       .. && \
       make -j "$(nproc)" && \
       usermod -a -G video dlstreamer && \
       chown -R dlstreamer:dlstreamer /home/dlstreamer

WORKDIR /home/dlstreamer
USER dlstreamer

# ==============================================================================
FROM dlstreamer-dev AS deb-builder
# Building deb package for DL Streamer

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

WORKDIR /
# hadolint ignore=DL3002
USER root
ENV USER=dlstreamer

RUN apt-get update && \
    apt-get install -y --no-install-recommends devscripts=\* dh-make=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN \
    mkdir -p /deb-pkg/usr/lib/ && \
    mkdir -p /deb-pkg/opt/intel/ && \
    mkdir -p /deb-pkg/opt/opencv/include && \
    mkdir -p /deb-pkg/opt/rdkafka && \
    mkdir -p /deb-pkg/opt/librealsense/ && \
    find /opt/intel/openvino_genai -regex '.*\/lib.*\(genai\|token\).*$' -exec cp -a {} /deb-pkg/usr/lib/ \; && \
    cp -r "${DLSTREAMER_DIR}/build/intel64/${BUILD_ARG}" /deb-pkg/opt/intel/dlstreamer && \
    cp -r "${DLSTREAMER_DIR}/samples/" /deb-pkg/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/python/" /deb-pkg/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/scripts/" /deb-pkg/opt/intel/dlstreamer/ && \
    cp -r "${DLSTREAMER_DIR}/include/" /deb-pkg/opt/intel/dlstreamer/ && \
    cp "${DLSTREAMER_DIR}/README.md" /deb-pkg/opt/intel/dlstreamer && \
    cp -rT "${GSTREAMER_DIR}" /deb-pkg/opt/intel/dlstreamer/gstreamer && \
    cp -a /usr/local/lib/libopencv*.so* /deb-pkg/opt/opencv/ && \
    cp -r /usr/local/include/opencv4/* /deb-pkg/opt/opencv/include && \
    cp -a /usr/local/lib/librdkafka*.so* /deb-pkg/opt/rdkafka/ && \
    cp -a /usr/local/lib/librealsense* /deb-pkg/opt/librealsense/ && \
    rm -rf /deb-pkg/opt/intel/dlstreamer/archived && \
    rm -rf /deb-pkg/opt/intel/dlstreamer/docker && \
    rm -rf /deb-pkg/opt/intel/dlstreamer/docs && \
    rm -rf /deb-pkg/opt/intel/dlstreamer/infrastructure && \
    rm -rf /deb-pkg/opt/intel/dlstreamer/tests && \
    find /deb-pkg/opt/intel/dlstreamer/bin -type f ! -name "liblibrarymock1.so" ! -name "liblibrarymock2.so" ! -name "draw_face_attributes" -exec rm -f {} + && \
    find /deb-pkg/opt/intel/dlstreamer/bin -type d -empty -delete && \
    find /deb-pkg/opt/intel -name "*.a" -delete

COPY docker/ubuntu/debian /deb-pkg/debian

RUN \
    sed -i -e "s/DLSTREAMER_VERSION/${DLSTREAMER_VERSION}/g" /deb-pkg/debian/changelog && \
    sed -i -e "s/CURRENT_DATE_TIME/$(date -R)/g" /deb-pkg/debian/changelog && \
    sed -i -e "s/DLSTREAMER_VERSION/${DLSTREAMER_VERSION}/g" /deb-pkg/debian/control

WORKDIR /deb-pkg

RUN \
    debuild -z1 -us -uc && \
    mv "/intel-dlstreamer_${DLSTREAMER_VERSION}_amd64.deb" "/intel-dlstreamer_${DLSTREAMER_VERSION}.${DLSTREAMER_BUILD_NUMBER}_amd64.deb"

# ==============================================================================
ARG DOCKER_REGISTRY
FROM ${DOCKER_REGISTRY}ubuntu:24.04 AS dlstreamer
ARG DLSTREAMER_VERSION
ARG DLSTREAMER_BUILD_NUMBER
# Build final image for dlstreamer - using .deb packages for installation

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN userdel -r ubuntu

# install prerequisites - gcc and cmake are needed to run .cpp samples
RUN \
    apt-get update && \
    apt-get install -y -q --no-install-recommends curl=\* gpg=\* ca-certificates=\* libtbb12=\* git=\* python3-venv=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# As clean ubuntu image is used, we need to install GPU and NPU on this image as well
# Intel GPU client drivers and prerequisites installation
RUN \
    apt-get update && \
    apt-get install -y --no-install-recommends software-properties-common=\* && \
    add-apt-repository -y ppa:kobuk-team/intel-graphics && \
    apt-get update && \
    echo "Snapshot: 20251125T030400Z" >> /etc/apt/sources.list.d/kobuk-team-ubuntu-intel-graphics-noble.sources && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    intel-metrics-discovery=\* intel-gsc=\* libvpl2=\* \
    libze-intel-gpu1=25.40.35563.7-1~24.04~ppa1 libze1=1.24.3-1~24.04~ppa1 intel-opencl-icd=25.40.35563.7-1~24.04~ppa1 clinfo=3.0.23.01.25-1build1 \
    intel-media-va-driver-non-free=25.4.2-1~24.04~ppa1 libmfx-gen1=25.4.0-0ubuntu1~24.04~ppa1 libvpl-tools=1.4.0-0ubuntu1~24.04~ppa1 libva-glx2=2.22.0-1ubuntu1~24.04~ppa1 va-driver-all=2.22.0-1ubuntu1~24.04~ppa1 vainfo=2.22.0-0ubuntu1~24.04~ppa1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Intel NPU drivers and prerequisites installation
WORKDIR /tmp/npu_deps

RUN curl -LO https://github.com/intel/linux-npu-driver/releases/download/v1.28.0/linux-npu-driver-v1.28.0.20251218-20347000698-ubuntu2404.tar.gz && \
    tar -xf linux-npu-driver-v1.28.0.20251218-20347000698-ubuntu2404.tar.gz && \
    dpkg -i ./*.deb && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/npu_deps

WORKDIR /

RUN curl -fsSL https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | \
    gpg --dearmor -o /usr/share/keyrings/intel-sw-products.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/intel-sw-products.gpg] https://apt.repos.intel.com/openvino/2025 ubuntu24 main" \
    > /etc/apt/sources.list.d/intel-openvino-2025.list

RUN mkdir -p /debs
COPY --from=deb-builder /*.deb /debs/

ARG DEBIAN_FRONTEND=noninteractive

RUN \
    apt-get update && \
    apt-get install -y -q --no-install-recommends ./debs/*.deb && \
    apt-get clean -y && \
    rm -rf /var/lib/apt/lists/* && \
    rm -f /*.deb && \
    useradd -ms /bin/bash dlstreamer && \
    chown -R dlstreamer: /opt && \
    chmod -R u+rw /opt

# DL Streamer environment variables
ENV LIBVA_DRIVER_NAME=iHD
ENV GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
ENV LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/opt/opencv:/opt/rdkafka:/opt/librealsense:/usr/local/lib/gstreamer-1.0:/usr/local/lib
ENV LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
ENV GST_VA_ALL_DRIVERS=1
ENV MODEL_PROC_PATH=/opt/intel/dlstreamer/samples/gstreamer/model_proc
ENV PATH=/python3venv/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
ENV PYTHONPATH=/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/home/dlstreamer/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:
ENV TERM=xterm
ENV GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0

RUN \
    usermod -a -G video dlstreamer && \
    ln -s /opt/intel/dlstreamer /home/dlstreamer/dlstreamer

WORKDIR /home/dlstreamer
USER dlstreamer

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD [ "bash", "-c", "pgrep bash > /dev/null || exit 1" ]

CMD ["/bin/bash"]
