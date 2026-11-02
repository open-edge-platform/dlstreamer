# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Build: docker build -f docker/fedora-rawhide/fedora-rawhide.Dockerfile -t dlstreamer-fedora-rawhide .
# Run from the repository root.
#
# The image is built in two stages:
#   builder - builds the RPMs. Use "--target builder" to get an image with the RPMs in /root/rpmbuild/RPMS.
#   tester  - installs the RPMs into a clean Fedora and runs the smoke test. This is the default target.

# ==============================================================================

FROM fedora:rawhide AS builder

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

# ==============================================================================

FROM fedora:rawhide AS tester

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

COPY --from=builder /root/rpmbuild/RPMS/ /rpms/

# Installing into a clean system verifies the dependencies of the packages. Debug packages are not installed.
RUN dnf install -y /rpms/noarch/*.rpm /rpms/x86_64/dlstreamer-[0-9]*.rpm /rpms/x86_64/dlstreamer-devel-*.rpm && \
    dnf clean all

COPY docker/fedora-rawhide/smoke-test.sh /usr/local/bin/dlstreamer-smoke-test.sh

RUN dlstreamer-smoke-test.sh
