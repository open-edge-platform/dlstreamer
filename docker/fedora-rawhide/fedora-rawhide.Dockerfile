# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Build: docker build -f docker/fedora-rawhide/fedora-rawhide.Dockerfile -t dlstreamer-fedora-rawhide .
# Run from the repository root.

# ==============================================================================

FROM fedora:rawhide

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN dnf install -y --setopt=install_weak_deps=False rpm-build rpmdevtools dnf-plugins-core && \
    dnf clean all && \
    rpmdev-setuptree

COPY docker/fedora-rawhide/dlstreamer.spec /root/rpmbuild/SPECS/
COPY docker/fedora-rawhide/patches/ /root/rpmbuild/SOURCES/

RUN spectool -g -R /root/rpmbuild/SPECS/dlstreamer.spec && \
    dnf builddep -y /root/rpmbuild/SPECS/dlstreamer.spec && \
    dnf clean all

RUN rpmbuild -ba /root/rpmbuild/SPECS/dlstreamer.spec
