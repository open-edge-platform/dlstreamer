# Packaging

## Memory interop library

The memory interop library (sub-component #1) is available via
APT/Debian installation `sudo apt install intel-dlstreamer-cpp` and
installed into folder `/opt/intel/dlstreamer/include/`, see
page
[Memory Interop and C++ abstract interfaces](./cpp_interfaces.md) for details.

## C++ elements and GStreamer elements

All C++ and GStreamer elements (sub-components #2 and #3) grouped by
library/framework they based on and compiled into separate .so shared
libraries and packaged into separate Debian packages with corresponding
dependencies on other packages.

Packages typically install two .so files named
`libgstdlstreamer_XYZ.so` and
`libdlstreamer_XYZ.so` where XYZ is name of framework the
elements based on. The first .so file contains all C++ elements, the
second is tiny .so file with GStreamer wrappers and registered as
GStreamer plug-in.

The following Debian packages are currently distributed in scope of
Deep Learning Streamer Architecture 2.0.

| Debian package | Dependencies |
|---|---|
| intel-dlstreamer-cpp | libc6-dev |
| intel-dlstreamer-dpcpp | intel-oneapi-compiler-dpcpp-cpp-runtime, level-zero, intel-level-zero-gpu |
| intel-dlstreamer-ffmpeg | libavcodec-dev, libavformat-dev, libavutil-dev, libswscale-dev |
| intel-dlstreamer-opencl | ocl-icd-libopencl1, intel-opencl-icd |
| intel-dlstreamer-opencv | libopencv-imgproc-dev |
| intel-dlstreamer-vaapi | intel-media-va-driver-non-free, libva-drm2, libva2 |
| intel-dlstreamer-openvino: | &nbsp; |

## Packages content

Files installed by 2.0 packages:

```text
intel-dlstreamer-ffmpeg
`-- opt
    `-- intel
        `-- dlstreamer
            |-- include
            |   `-- dlstreamer
            |       `-- ffmpeg
            |           `-- elements
            |               `-- ffmpeg_multi_source.h
            |-- lib
            |   `-- libdlstreamer_ffmpeg.so
```

```text
intel-dlstreamer-opencl
`-- opt
    `-- intel
        `-- dlstreamer
            |-- include
            |   `-- dlstreamer
            |       `-- opencl
            |           `-- elements
            |               |-- opencl_tensor_normalize.h
            |               `-- vaapi_to_opencl.h
            |-- lib
            |   |-- gstreamer-1.0
            |   |   `-- libgstdlstreamer_opencl.so
            |   `-- libdlstreamer_opencl.so
```

```text
intel-dlstreamer-opencv
`-- opt
    `-- intel
        `-- dlstreamer
            |-- include
            |   `-- dlstreamer
            |       `-- opencv
            |           `-- elements
            |               |-- opencv_find_contours.h
            |               |-- opencv_barcode_detector.h
            |               |-- opencv_object_association.h
            |               |-- opencv_remove_background.h
            |               |-- opencv_tensor_normalize.h
            |               |-- opencv_cropscale.h
            |               |-- opencv_warp_affine.h
            |               `-- opencv_meta_overlay.h
            |-- lib
            |   |-- gstreamer-1.0
            |   |   |-- libgstdlstreamer_opencv.so
            |   |   `-- libgvatrack.so
            |   `-- libdlstreamer_opencv.so
```

```text
intel-dlstreamer-openvino
`-- opt
    `-- intel
        `-- dlstreamer
            |-- include
            |   `-- dlstreamer
            |       `-- openvino
            |           `-- elements
            |               `-- openvino_tensor_inference.h
            |-- lib
            |   |-- gstreamer-1.0
            |   |   `-- libgstdlstreamer_openvino.so
            |   `-- libdlstreamer_openvino.so
```

```text
intel-dlstreamer-vaapi
`-- opt
    `-- intel
        `-- dlstreamer
            |-- include
            |   `-- dlstreamer
            |       `-- vaapi
            |           `-- elements
            |               |-- vaapi_sync.h
            |               `-- vaapi_batch_proc.h
            |-- lib
            |   |-- gstreamer-1.0
            |   |   `-- libgstdlstreamer_vaapi.so
            |   `-- libdlstreamer_vaapi.so
```

```text
intel-dlstreamer-dpcpp
`-- opt
    `-- intel
        `-- dlstreamer
            |-- lib
            |   |-- gstreamer-1.0
            |   |   `-- libgstdlstreamer_sycl.so
            |   `-- libdlstreamer_gst_sycl.so
```
