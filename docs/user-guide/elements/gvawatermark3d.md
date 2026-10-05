# gvawatermark3d

Overlays 3D object detection results on video frames. The element renders the
camera-space cuboids stored in `GstAnalyticsCamera3DODMtd`, including the
metadata produced by [`gvamono3d`](./gvamono3d.md).

Each object is drawn as a wire-frame cuboid with a heading marker. A label near
the associated 2D box shows the object class, estimated depth, and length. The
input metadata is copied to the output buffer.

`gvawatermark3d` is a bin that converts supported system-memory, VA-memory, and
DMA-buffer video to BGR for the OpenCV renderer, then converts the rendered
frame to the format requested downstream.

```text
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
      video/x-raw(memory:DMABuf)
                format: { (string)DMA_DRM }
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 }
      video/x-raw(memory:DMABuf)
                format: { (string)DMA_DRM }
      video/x-raw(memory:VASurface)
                format: { (string)NV12 }
      video/x-raw(memory:VAMemory)
                format: { (string)NV12 }

Element Properties:
  calibration-file    : Camera calibration for monocular 3D boxes: KITTI .txt
                        (P2 row) or JSON (intrinsic_matrix 3x3 /
                        projection_matrix 3x4).
                        flags: readable, writable
                        String. Default: null
  intrinsics-file     : Path to JSON file with camera intrinsics.
                        flags: readable, writable
                        String. Default: null
```

On Windows, the element also accepts and produces D3D11 memory in NV12 and
BGRA formats. VA-memory and DMA-buffer formats are available only in VA-enabled
Linux builds.

## Calibration

Set `calibration-file` to the same camera calibration used by `gvamono3d` so
the 3D corners are projected onto the image correctly. The property accepts:

- A KITTI text file with a `P2:` row containing a row-major 3x4 projection
  matrix.
- A JSON file with a 3x4 `projection_matrix` or a 3x3 `intrinsic_matrix`.
  When both fields are present, `projection_matrix` takes precedence. A 3x3
  matrix is extended to `P2 = [K | 0]`.

See [gvamono3d calibration file formats](./gvamono3d.md#calibration-file-formats)
for complete examples.

The legacy `intrinsics-file` property accepts a JSON file containing only a
3x3 `intrinsic_matrix`. It is used to project legacy ROI metadata whose
`detection` structure contains `translation`, quaternion `rotation`, and
`dimension` values in `extra_params_json`.

If `calibration-file` is not set, monocular 3D metadata is projected using the
matrix from `intrinsics-file`, extended with a zero fourth column. If neither
property is set, a generic intrinsic matrix is used. A matching
`calibration-file` is therefore recommended for accurate placement.

## Examples

Render MonoDETR results using a KITTI calibration file:

```sh
gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! \
  gvamono3d model=monodetr.xml calibration-file=000000.txt threshold=0.2 ! \
  gvawatermark3d calibration-file=000000.txt ! videoconvert ! autovideosink
```

The same pipeline can use a JSON calibration file:

```sh
gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! \
  gvamono3d model=monodetr.xml calibration-file=calibration.json ! \
  gvawatermark3d calibration-file=calibration.json ! videoconvert ! autovideosink
```