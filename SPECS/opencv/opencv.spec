%define debug_package %{nil}
Name:           opencv
Version:        4.13.0
Release:        1%{?dist}
Summary:        OpenCV build

License:        Apache-2.0
Source0:        opencv-%{version}.tar.gz
URL:            https://opencv.org/
Packager:       DL Streamer Team <dlstreamer@intel.com>
ExclusiveArch:  x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root

BuildRequires:  cmake ninja-build gcc gcc-c++ make
BuildRequires:  libva-devel libva-intel-media-driver
BuildRequires:  pkgconfig

Requires:       libjpeg-turbo libpng libtiff
Requires:       libva2 libva-intel-media-driver

%description
This package provides a custom build of OpenCV with selected features and optimizations.

%package devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description devel
Development files and headers for Intel OpenCV.

%prep
%setup -q -n opencv-%{version}

%build
mkdir build
cd build
cmake -DBUILD_TESTS=OFF \
      -DBUILD_PERF_TESTS=OFF \
      -DBUILD_EXAMPLES=OFF \
      -DBUILD_opencv_apps=OFF \
      -GNinja ..
ninja -j "$(nproc)"

%install
rm -rf %{buildroot}
cd build
env DESTDIR=%{buildroot} ninja install
mkdir -p %{buildroot}/usr/local/lib/cmake/opencv4/
cp -r %{_builddir}/%{name}-%{version}/cmake/* %{buildroot}/usr/local/lib/cmake/opencv4/

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
%license LICENSE
/usr/local/bin/*
/usr/local/lib/*
/usr/local/lib64/*
/usr/local/share/*
/usr/local/include

%files devel
%defattr(-,root,root,-)
/usr/local/include  
/usr/local/lib/cmake/opencv4/*  
/usr/local/lib64/cmake/opencv4/*

%changelog
* Tue Mar 03 2026 OpenCV build - 4.13.0-1
- Update OpenCV version
* Wed Dec 02 2025 OpenCV build - 4.12.0-1
- Update OpenCV version
* Thu Aug 25 2025 OpenCV build - 4.10.0-1
- Initial OpenCV build
