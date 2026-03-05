%define debug_package %{nil}
Name:           intel-dlstreamer
Version:        2025.2.0
Release:        1%{?dist}
Summary:        Intel Deep Learning Streamer framework

License:        Apache License 2.0
Source0:        %{name}-%{version}.tar.gz
URL:            https://github.com/open-edge-platform/edge-ai-libraries/tree/release-2025.2.0/libraries/dl-streamer
Packager:       DL Streamer Team <dlstreamer@intel.com>
ExclusiveArch:  x86_64
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root

# Turns off all auto-dependency generation
AutoReq: no

# Build dependencies
BuildRequires:  cmake gcc gcc-c++ make
BuildRequires:  libva-devel libva-intel-media-driver
BuildRequires:  python3-devel python3-pip
BuildRequires:  pkgconfig patchelf
BuildRequires:  opencv-devel >= 4.12.0
BuildRequires:  gstreamer-devel >= 1.26.1
BuildRequires:  paho-mqtt-c-devel >= 1.3.4
BuildRequires:  librdkafka-devel

# Runtime dependencies
Requires:       paho-mqtt-c-devel >= 1.3.4
Requires:       ffmpeg >= 6.1.1
Requires:       gstreamer >= 1.26.1
Requires:       opencv >= 4.12.0
Requires:       libva2 libva-intel-media-driver
Requires:       python3 python3-pip python3-gobject
Requires:       glib2-devel
Requires:       libjpeg-turbo libpng libdrm
Requires:       wayland-devel libX11 libXext
Requires:       mesa-libGL mesa-libGLU
Requires:       libgudev cairo cairo-gobject
Requires:       gobject-introspection
Requires:       libXv libX11 libXext
Requires:       libva-utils libusb1

%description
Intel Deep Learning Streamer (DL Streamer) is a streaming media analytics 
framework based on GStreamer for creating complex media analytics pipelines 
optimized for Intel hardware including Intel CPUs, Intel integrated GPUs, 
and Intel discrete GPUs.

%package devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}


%description devel
Development files and headers for Intel DL Streamer.

%package samples
Summary:        Sample applications and scripts for %{name}
Requires:       %{name} = %{version}-%{release}

%description samples
Sample applications, scripts, and models for Intel DL Streamer.

%prep
%setup -q -n intel-dlstreamer-%{version}

%build
cur_dir=`pwd`
mkdir build
cd build

# Set up PKG_CONFIG_PATH for packages
export PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig/:/opt/intel/dlstreamer/gstreamer/lib/pkgconfig:/usr/lib/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

source /opt/intel/openvino_2026/setupvars.sh
# Configure with optimized dependencies
cmake -DCMAKE_INSTALL_PREFIX=/opt/intel/dlstreamer \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_PAHO_INSTALLATION=ON \
      -DENABLE_RDKAFKA_INSTALLATION=ON \
      -DENABLE_VAAPI=ON \
      -DENABLE_SAMPLES=ON \
      -DCMAKE_CXX_FLAGS="-Wno-error" \
      -DCMAKE_C_FLAGS="-Wno-error" \
      ..

make -j "$(nproc)"

%install
rm -rf %{buildroot}
cd build
mkdir -p %{buildroot}/opt/intel/dlstreamer
make install DESTDIR=%{buildroot}

# Explicitly copying the .so and .a files
cp -r intel64/Release/* %{buildroot}/opt/intel/dlstreamer
rm -rf %{buildroot}/opt/intel/dlstreamer/lib/gst-video-analytics
rm -rf %{buildroot}/opt/intel/dlstreamer/lib/gstreamer-1.0

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

# Create environment setup script
mkdir -p %{buildroot}/etc/profile.d
cat > %{buildroot}/opt/intel/dlstreamer/setupvars.sh << 'EOF'
# Intel DL Streamer environment setup
export LIBVA_DRIVER_NAME=iHD
export GST_PLUGIN_PATH="/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0"
export LD_LIBRARY_PATH="/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/usr/lib:/usr/local/lib:$LD_LIBRARY_PATH"
export LIBVA_DRIVERS_PATH="/usr/lib/dri"
export GST_VA_ALL_DRIVERS="1"
export PATH="/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/bin:$HOME/.local/bin:$HOME/python3venv/bin:$PATH"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/opt/intel/dlstreamer/lib/pkgconfig:/opt/intel/dlstreamer/gstreamer/lib/pkgconfig:$PKG_CONFIG_PATH"
export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
EOF

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README.md
%license LICENSE
/opt/intel/dlstreamer/*
/opt/intel/dlstreamer/setupvars.sh

%files devel
%defattr(-,root,root,-)
/opt/intel/dlstreamer/include/*  
/opt/intel/dlstreamer/lib/*.a  
/opt/intel/dlstreamer/lib/pkgconfig/*

%changelog
* Wed Dec 02 2025 DL Streamer Team <dlstreamer@intel.com> - 2025.2.0-1
- Update DL Streamer version
* Thu Aug 07 2025 DL Streamer Team <dlstreamer@intel.com> - 2025.1.2-1
- Split into modular package architecture
- Use Intel optimized dependencies
- Added development and samples subpackages
