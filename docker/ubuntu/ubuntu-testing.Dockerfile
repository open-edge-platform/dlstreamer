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
    . /etc/os-release && \
    case "$VERSION_ID" in \
        22.04) UV_PYTHON_PREFERENCE=managed uv venv --python 3.11 /python3venv ;; \
        24.04) UV_PYTHON_PREFERENCE=only-system uv venv --python python3 /python3venv ;; \
        26.04) UV_PYTHON_PREFERENCE=managed uv venv --python 3.13 /python3venv ;; \
        *) echo "Unsupported Ubuntu base version: $VERSION_ID" >&2; exit 1 ;; \
    esac && \
    PATH=/python3venv/bin:$PATH uv pip install --python /python3venv/bin/python --no-cache-dir --no-deps pycairo==1.26.0 && \
    PATH=/python3venv/bin:$PATH uv pip install --python /python3venv/bin/python --no-cache-dir --no-deps -r /home/dlstreamer/requirements.txt

ENV PATH=/python3venv/bin:$PATH

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD [ "bash", "-c", "pgrep bash > /dev/null || exit 1" ]

CMD ["/bin/bash"]
