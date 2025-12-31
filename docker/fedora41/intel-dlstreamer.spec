Name:           intel-dlstreamer
Version:        DLSTREAMER_VERSION
Release:        1%{?dist}
Summary:        Deep Learning Streamer

License:        Proprietary
Source0:        %{name}-%{version}.tar.gz
URL:            https://github.com/open-edge-platform/dlstreamer/tree/master
Packager:       DL Streamer Team <dlstreamer@intel.com>
ExclusiveArch: x86_64
AutoReqProv:    no
%define debug_package %{nil}
%define __os_install_post %{nil}

Requires: glib2-devel
Requires: libjpeg-turbo
Requires: libdrm
Requires: wayland-devel
Requires: libX11
Requires: libpng
Requires: libva
Requires: libcurl
Requires: libde265
Requires: libXext
Requires: mesa-libGL
Requires: mesa-libGLU
Requires: libgudev
Requires: paho-c
Requires: python3
Requires: python3-pip
Requires: python3-gobject
Requires: cairo
Requires: cairo-gobject
Requires: gobject-introspection
Requires: libvpx
Requires: opus
Requires: libsrtp
Requires: libXv
Requires: libva-utils
Requires: libogg
Requires: libusb1
Requires: x265-libs
Requires: x264-libs
Requires: openexr
Requires: tbb
Requires: libsoup3
Requires: intel-media-driver
Requires: openvino-2025.4.0

%description
This package contains Intel DL Streamer.

%prep
%setup -q

%build
# No build steps needed

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/opt
mkdir -p %{buildroot}/usr

cp -a opt/* %{buildroot}/opt/

find %{buildroot} -type f \( -name "*.so*" -o -perm -111 \) | while read -r file; do
    if patchelf --print-rpath "$file" &>/dev/null; then
        rpath=$(patchelf --print-rpath "$file")
        if [ -n "$rpath" ]; then
            echo "Removing RPATH from $file"
            patchelf --remove-rpath "$file"
        fi
    fi
done

%check
# No test suite

%files
%license LICENSE
/opt/intel/dlstreamer/
/opt/opencv/
/opt/rdkafka/
/opt/ffmpeg/
/opt/librealsense/

%changelog
* CURRENT_DATE_TIME DL Streamer Team <dlstreamer@intel.com> - DLSTREAMER_VERSION
- Initial release.
