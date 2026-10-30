# Build: docker build -f docker/fedora/fedora-rawhide-system-gst.Dockerfile -t dlstreamer-fedora-rawhide-system-gst .
# Run from the repository root.

# ==============================================================================

FROM fedora:rawhide

SHELL ["/bin/bash", "-xo", "pipefail", "-c"]

RUN dnf install -y --setopt=install_weak_deps=False rpm-build rpmdevtools dnf-plugins-core && \
    dnf clean all && \
    rpmdev-setuptree

COPY docker/fedora/dlstreamer-system-gst.spec /root/rpmbuild/SPECS/dlstreamer.spec
COPY docker/fedora/patches/ /root/rpmbuild/SOURCES/

RUN spectool -g -R /root/rpmbuild/SPECS/dlstreamer.spec && \
    dnf builddep -y /root/rpmbuild/SPECS/dlstreamer.spec && \
    dnf clean all

RUN rpmbuild -ba /root/rpmbuild/SPECS/dlstreamer.spec
