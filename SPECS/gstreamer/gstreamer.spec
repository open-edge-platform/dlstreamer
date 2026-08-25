%define debug_package %{nil}
Name:           gstreamer
Version:        1.28.2
Release:        1%{?dist}
Summary:        Intel optimized GStreamer build with VAAPI support

License:        LGPL-2.0+
Source0:        gstreamer-%{version}.tar.gz
URL:            https://gstreamer.freedesktop.org/

Patch1:         0001-va-encoder-disable-usage-hint-because-of-iHD-bug.patch
Patch2:         0002-msdkdec-Apply-dynamic-allocation-for-VPL-2.9.patch
Patch3:         0003-msdkenc-Add-VPL-string-API-option.patch
Patch4:         0004-msdkdec-Do-not-use-aligned-value-to-set-allocation-c.patch
Patch5:         0005-va-compositor-Update-to-multiple-input-direct-write-.patch
Patch6:         0006-va-Add-scaling-and-composition-pipeline-flags.patch
Patch7:         0009-Optimize-VP9-preferred_output_delay-value.patch
Patch8:         0010-msdkav1enc-Add-intrabc-and-palette-for-scc-encode.patch
Patch9:         0012-va-Do-not-reuse-vadisplay-if-it-is-used-too-many-tim.patch
Patch10:        0013-h266parse-Only-support-byte-stream.patch
Patch11:        0014-va-Add-configurable-threshold-level-for-vadisplay.patch
Patch12:        0015-va-allocator-add-gst_va_buffer_prepare_for_import-he.patch
Patch13:        0016-vabaseenc-use-gst_va_buffer_prepare_for_import.patch
Patch14:        0017-vavpp-use-gst_va_buffer_prepare_for_import.patch
Patch15:        0018-vacompositor-use-gst_va_buffer_prepare_for_import.patch
Patch16:        0019-vabasetransform-Copy-timestamp-for-vapostproc-output.patch
Patch17:        0020-gst-analytics.patch
Packager:       DL Streamer Team <dlstreamer@intel.com>
ExclusiveArch:  x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root

# Turns off all auto-dependency generation
AutoReq: no
BuildRequires:  meson ninja-build gcc gcc-c++
BuildRequires:  python3 python3-pip
BuildRequires:  libva-devel libva-intel-media-driver
BuildRequires:  pkgconfig flex bison
BuildRequires:  gobject-introspection-devel

Requires:       glib2 gobject-introspection
Requires:       libva2 libva-intel-media-driver
Requires:       ffmpeg >= 6.1.1

%description
Intel optimized GStreamer build with VAAPI hardware acceleration support.
This version is specifically configured for use with Intel DL Streamer.

%package devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description devel
Development files and headers for Intel GStreamer.

%prep
%setup -q -n gstreamer-%{version}
%patch 1 -p1
%patch 2 -p1
%patch 3 -p1
%patch 4 -p1
%patch 5 -p1
%patch 6 -p1
%patch 7 -p1
%patch 8 -p1
%patch 9 -p1
%patch 10 -p1
%patch 11 -p1
%patch 12 -p1
%patch 13 -p1
%patch 14 -p1
%patch 15 -p1
%patch 16 -p1
%patch 17 -p1

%build
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH}"
export LDFLAGS=-lstdc++

meson setup -Dexamples=disabled \
            -Dtests=disabled \
            -Dgst-examples=disabled \
            --buildtype=release \
            --prefix=/opt/intel/dlstreamer/gstreamer \
            --libdir=lib/ \
            --libexecdir=bin/ \
            build/

ninja -C build

%install
rm -rf %{buildroot}
env DESTDIR=%{buildroot} meson install -C build/

# Remove RPATH for all binaries/libs
find %{buildroot} -type f \( -name "*.so*" -o -perm -111 \) | while read -r file; do
    if patchelf --print-rpath "$file" &>/dev/null; then
        rpath=$(patchelf --print-rpath "$file")
        if [ -n "$rpath" ]; then
            echo "Removing RPATH from $file"
            patchelf --remove-rpath "$file"
        fi
    fi
done

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc README.md
/opt/intel/dlstreamer/gstreamer/bin/*
/opt/intel/dlstreamer/gstreamer/share/*
/opt/intel/dlstreamer/gstreamer/lib/*
/opt/intel/dlstreamer/gstreamer/etc/*

%files devel
%defattr(-,root,root,-)
/opt/intel/dlstreamer/gstreamer/include/*
/opt/intel/dlstreamer/gstreamer/lib/pkgconfig/

%changelog
* Thu Jun 18 2026 Gstreamer build - 1.28.2-1
- Update gstremer verison
* Thu Dec 09 2025 Gstreamer build - 1.26.6-1
- Update gstremer verison
* Thu Aug 25 2025 Gstreamer build - 1.26.1-1
- Initial GStreamer build
