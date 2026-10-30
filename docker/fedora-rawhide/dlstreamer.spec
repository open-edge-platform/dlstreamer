# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

%global commit 83845c90c651f7e91140396e95f8f275e5f520d9
%global so_ver 26.3.0

# libgvametapublish.so is a plugin that is also linked by the kafka and mqtt plugins (through $ORIGIN rpath).
# It is internal to the package and lives in the plugin directory, so it must not be provided or required.
%global __provides_exclude ^libgvametapublish(kafka|mqtt)?\\.so
%global __requires_exclude ^libgvametapublish\\.so

Name:           dlstreamer
Version:        2026.3.0
Release:        1%{?dist}
Summary:        Intel Deep Learning Streamer GStreamer plugins for video analytics
# Most of the source code is MIT. The exceptions, from the vendored GStreamer GstAnalytics keypoint and group metadata
# (src/gst/metadata/gstanalytics{group,keypoint}mtd.c and include/dlstreamer/gst/metadata/gstanalytics{group,keypoint}mtd.h)
# are LGPL-2.0-or-later. They are built into libdlstreamer_gst_meta, which is shipped in the main package, and the
# headers are shipped in the devel package.
License:        MIT AND LGPL-2.0-or-later
URL:            https://github.com/open-edge-platform/dlstreamer
Source0:        %{url}/archive/%{commit}/%{name}-%{commit}.tar.gz
# Vendors keypoint and group metadata that system GStreamer 1.28 does not provide
Patch0:         0001-Keypoints-meta-patch.patch
# Stores semantic tags in a standalone mtd instead of GStreamer's patched GstAnalyticsMtd
Patch1:         0002-Semantic-tag-meta-patch.patch
# GIR and Python registration for the vendored mtd types, needed by gstgva to iterate the relation meta
Patch2:         0003-add-girs-for-new-metadata.patch
# Builds with the system fmt, spdlog is only used for its bundled fmt and is not part of the source tarball
Patch3:         0004-spdlog-removal-patch.patch
# Builds with the system nlohmann_json and json-schema-validator instead of downloading them during the build
Patch4:         0005-json-libs-patch.patch
# Overridable install paths and SOVERSION, so the package installs into FHS paths
Patch5:         0006-installation-paths-patch.patch
# kafka and mqtt plugins link libgvametapublish, which is installed next to them in the plugin directory
Patch6:         0007-metapublish-fix.patch
# The OpenVINO build dependency (libopenvino-devel) is only available on x86_64
ExclusiveArch:  x86_64

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  glib2-devel
BuildRequires:  python3-devel
BuildRequires:  gstreamer1-devel
BuildRequires:  gstreamer1-plugins-base-devel
BuildRequires:  gstreamer1-plugins-bad-free-devel
BuildRequires:  gobject-introspection-devel
BuildRequires:  libopenvino-devel
BuildRequires:  opencv-devel
BuildRequires:  libva-devel
BuildRequires:  libdrm-devel
BuildRequires:  ocl-icd-devel
BuildRequires:  paho-c-devel
BuildRequires:  librdkafka-devel
BuildRequires:  libuuid-devel
BuildRequires:  fmt-devel
BuildRequires:  json-devel
BuildRequires:  json-schema-validator-devel
# For %%check
BuildRequires:  python3-gobject
BuildRequires:  python3-gstreamer1
BuildRequires:  python3-numpy

Requires:       gstreamer1
Requires:       gstreamer1-plugins-base
Requires:       gstreamer1-plugins-good
Requires:       gstreamer1-plugins-bad-free
# libva.so.2 is linked and detected automatically, but libva-drm.so.2 is only loaded with dlopen() (see
# vaapi_utils.cpp), so RPM cannot detect it
Requires:       libva-drm.so.2()(64bit)
Recommends:     intel-opencl
Recommends:     libva-intel-media-driver

%description
Intel Deep Learning Streamer (DL Streamer) provides GStreamer elements for
video analytics: inference, metadata publishing, tracking and visualization.
GStreamer is provided by the system gstreamer1 packages.

%package devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}
# The installed headers include GStreamer, GStreamer base and GStreamer analytics headers
Requires:       gstreamer1-devel
Requires:       gstreamer1-plugins-base-devel
Requires:       gstreamer1-plugins-bad-free-devel

%description devel
Headers, pkg-config file and GIR files for developing applications with
%{name}.

%package -n python3-%{name}
Summary:        Python bindings for %{name}
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}
Requires:       python3-gobject
Requires:       python3-gstreamer1
# gstgva imports numpy at module level
Requires:       python3-numpy
# Python deps from python/pyproject.toml
Recommends:     python3-defusedxml
Recommends:     python3-urllib3

%description -n python3-%{name}
Python modules gstgva and dlstreamer for accessing %{name} metadata from
Python.

%package samples
Summary:        Samples for %{name}
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}
# Most of the Python samples use the gstgva and dlstreamer modules
Recommends:     python3-%{name} = %{version}-%{release}

%description samples
Sample pipelines and applications for %{name}.

%prep
%autosetup -p1 -n %{name}-%{commit}

# The tarball contains a prebuilt static libittnotify.a, it is removed and ITT is disabled (-DENABLE_ITT=OFF)
rm -rf thirdparty/itt/lib

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DDLSTREAMER_LIBRARIES_INSTALL_PATH=%{_lib} \
    -DDLSTREAMER_SHARED_LIBS_INSTALL_PATH=%{_lib} \
    -DDLSTREAMER_SOVERSION=%{so_ver} \
    -DDLSTREAMER_PYTHON_INSTALL_PATH=%{python3_sitelib} \
    -DDLSTREAMER_SAMPLES_INSTALL_PATH=%{_datadir}/%{name}/samples \
    -DENABLE_SAMPLES=ON \
    -DENABLE_TESTS=OFF \
    -DENABLE_PAHO_INSTALLATION=ON \
    -DENABLE_RDKAFKA_INSTALLATION=ON \
    -DENABLE_VAAPI=ON \
    -DENABLE_ITT=OFF \
    -DENABLE_SYSTEM_FMT=ON \
    -DENABLE_SYSTEM_JSON=ON \
    -DTREAT_WARNING_AS_ERROR=OFF
%cmake_build

%install
%cmake_install

# Standalone gvaattachroi plugin outside the GStreamer plugin path, not shipped
rm -rf %{buildroot}%{_prefix}/lib/gst-video-analytics

# Static libraries of internal components are installed next to the plugins, not shipped
rm -f %{buildroot}%{_libdir}/gstreamer-1.0/*.a

# Version control files from the sample directories
find %{buildroot}%{_datadir}/%{name}/samples -name .gitignore -delete

# Sample scripts have a shebang but are not executable in the sources
find %{buildroot}%{_datadir}/%{name}/samples -type f \( -name '*.sh' -o -name '*.py' \) ! -perm /111 \
    -exec grep -l '^#!' {} + | xargs -r chmod +x

%check
# Smoke test of the installed files: the plugins load, the typelibs and Python modules import.
# No GPU, network or models are needed.
export LD_LIBRARY_PATH=%{buildroot}%{_libdir}
export GST_PLUGIN_PATH=%{buildroot}%{_libdir}/gstreamer-1.0
export GST_REGISTRY=%{_builddir}/gst-registry.bin
export GI_TYPELIB_PATH=%{buildroot}%{_libdir}/girepository-1.0
export PYTHONPATH=%{buildroot}%{python3_sitelib}
for element in gvadetect gvaclassify gvainference gvametaconvert gvametaaggregate gvawatermark gvatrack gvametapublishkafka gvametapublishmqtt; do
    gst-inspect-1.0 --exists $element
done
%{python3} -c "
import gi
gi.require_version('DLStreamerMeta', '1.0')
gi.require_version('DLStreamerWatermarkMeta', '1.0')
from gi.repository import DLStreamerMeta, DLStreamerWatermarkMeta
import gstgva
"

%files
%license LICENSE
%doc README.md
%{_libdir}/libdlstreamer_gst.so.%{so_ver}
%{_libdir}/libdlstreamer_gst_meta.so.%{so_ver}
%{_libdir}/gstreamer-1.0/*.so
%{_libdir}/girepository-1.0/DLStreamerMeta-1.0.typelib
%{_libdir}/girepository-1.0/DLStreamerWatermarkMeta-1.0.typelib

%files devel
%{_includedir}/dlstreamer/
%{_libdir}/libdlstreamer_gst.so
%{_libdir}/libdlstreamer_gst_meta.so
%{_libdir}/pkgconfig/dl-streamer.pc
%{_datadir}/gir-1.0/DLStreamerMeta-1.0.gir
%{_datadir}/gir-1.0/DLStreamerWatermarkMeta-1.0.gir

%files -n python3-%{name}
%{python3_sitelib}/gstgva/
%{python3_sitelib}/dlstreamer/

%files samples
%{_datadir}/%{name}/

%changelog
* Tue Oct 27 2026 Packager <packager@example.com> - 2026.2.0-1
- Test package using Fedora system GStreamer
