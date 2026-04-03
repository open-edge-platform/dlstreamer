# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

ARG BASE_IMAGE
# hadolint ignore=DL3006
FROM $BASE_IMAGE

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

ARG DEBIAN_FRONTEND=noninteractive

USER root

RUN \
    apt-get update && \
    apt-get install -y -q --no-install-recommends gcc=\* cmake=\* python3-full=\* python-gi-dev=\* python3-dev=\* python3-pip=\* \
    libglib2.0-dev=\* libcairo2-dev=\* libopencv-objdetect-dev=\* libopencv-photo-dev=\* libopencv-stitching-dev=\* libopencv-video-dev=\* \
    libopencv-calib3d-dev=\* libopencv-core-dev=\* libopencv-dnn-dev=\* libgirepository1.0-dev=\* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN \
    mkdir /python3venv && \
    chown -R dlstreamer: /python3venv && \
    chmod -R u+w /python3venv

RUN \
    usermod -a -G video dlstreamer && \
    ln -s /opt/intel/dlstreamer /home/dlstreamer/dlstreamer

WORKDIR /home/dlstreamer
USER dlstreamer

COPY ./requirements.txt /home/dlstreamer/requirements.txt

RUN \
    python3 -m venv /python3venv && \
    /python3venv/bin/pip3 install --no-cache-dir --upgrade pip && \
    /python3venv/bin/pip3 install --no-cache-dir --no-dependencies -r /home/dlstreamer/requirements.txt

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD [ "bash", "-c", "pgrep bash > /dev/null || exit 1" ]

CMD ["/bin/bash"]
