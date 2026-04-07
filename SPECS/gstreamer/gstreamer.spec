%define debug_package %{nil}
Name:           gstreamer
Version:        1.26.11
Release:        1%{?dist}
Summary:        Intel optimized GStreamer build with VAAPI support

License:        LGPL-2.0+
Source0:        gstreamer-%{version}.tar.gz
URL:            https://gstreamer.freedesktop.org/
Packager:       DL Streamer Team <dlstreamer@intel.com>
ExclusiveArch:  x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root

# Turns off all auto-dependency generation
AutoReq: no
BuildRequires:  meson ninja-build gcc gcc-c++
BuildRequires:  python3 python3-pip
BuildRequires:  libva-devel libva-intel-media-driver
BuildRequires:  pkgconfig flex bison

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

%build
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH}"
export LDFLAGS=-lstdc++

meson setup -Dexamples=disabled \
            -Dtests=disabled \
            -Dvaapi=enabled \
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
* Thu Dec 09 2025 Gstreamer build - 1.26.6-1
- Update gstremer verison
* Thu Aug 25 2025 Gstreamer build - 1.26.1-1
- Initial GStreamer build
