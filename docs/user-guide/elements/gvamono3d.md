# gvamono3d

Performs monocular 3D object detection (e.g. MonoDETR). Consumes a single
camera image together with the camera calibration and produces, for each
detected object, a 2D bounding box annotated with a full 3D cuboid
(location, dimensions and orientation).

`gvamono3d` is an inference element built on the same engine as `gvadetect`,
so it inherits batching, VA pre-processing, multi-stream model sharing and the
standard inference properties. The model is driven directly from its `rt_info`
(`model_type=mono3d`). Detections are attached
as `GstAnalyticsCamera3DODMtd` metadata, serialized to JSON by `gvametaconvert`
and rendered by `gvawatermark3d`.

The camera calibration (KITTI `P2` and the original image size) is required for
the 3D lift and is provided through the `calibration-file` property. Both a
KITTI `.txt` calibration (line starting with `P2:`) and a JSON file with an
`intrinsic_matrix` (3x3) or `projection_matrix` (3x4) and optional `image_size`
are supported.

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
  model               : Path to inference model network file (OpenVINO™ IR .xml).
                        flags: readable, writable
                        String. Default: null
  calibration-file    : Path to camera calibration: KITTI .txt (P2 row) or JSON with "intrinsic_matrix" (3x3) / "projection_matrix" (3x4) and optional "image_size".
                        flags: readable, writable
                        String. Default: null
  device              : Target device for inference. Please see OpenVINO™ Toolkit documentation for list of supported devices.
                        flags: readable, writable
                        String. Default: "CPU"
  threshold           : Only detections with confidence above the threshold are attached to the frame.
                        flags: readable, writable
                        Float. Range: 0 - 1 Default: 0.5
```

In addition to the properties above, `gvamono3d` supports the common inference
element properties (`batch-size`, `nireq`, `ie-config`, `model-instance-id`,
`pre-process-backend`, `inference-interval`, `reshape`, …); see
[gvadetect](./gvadetect.md) for their descriptions.

## Calibration file formats

Set `calibration-file` to either a KITTI calibration text file or a JSON file.
The projection matrix is stored in row-major order and maps homogeneous 3D
camera coordinates `[X, Y, Z, 1]` to image coordinates.

### KITTI text

The `P2:` row must start at the beginning of a line and contain the 12 values
of the 3x4 projection matrix in row-major order. Other rows are ignored.

```text
P2: 7.215377e+02 0.000000e+00 6.095593e+02 4.485728e+01 0.000000e+00 7.215377e+02 1.728540e+02 2.163791e-01 0.000000e+00 0.000000e+00 1.000000e+00 2.745884e-03
```

Equivalently, the values after `P2:` are ordered as follows:

```text
p00 p01 p02 p03 p10 p11 p12 p13 p20 p21 p22 p23
```

KITTI text files do not specify the image dimensions, so `gvamono3d` uses the
negotiated input frame width and height.

### JSON

A JSON file can provide the full 3x4 projection matrix as nested arrays:

```json
{
  "projection_matrix": [
    [721.5377, 0.0, 609.5593, 44.85728],
    [0.0, 721.5377, 172.854, 0.2163791],
    [0.0, 0.0, 1.0, 0.002745884]
  ],
  "image_size": [1242, 375]
}
```

Alternatively, provide a 3x3 intrinsic matrix. `gvamono3d` converts it to a
projection matrix by appending a zero column (`P2 = [K | 0]`):

```json
{
  "intrinsic_matrix": [
    [721.5377, 0.0, 609.5593],
    [0.0, 721.5377, 172.854],
    [0.0, 0.0, 1.0]
  ],
  "image_size": [1242, 375]
}
```

`image_size` is optional and is ordered as `[width, height]`. It must describe
the frame dimensions to which the calibration applies, before the inference
element's internal resize. If omitted, the negotiated input frame dimensions
are used. If both matrix fields are present, `projection_matrix` takes
precedence. File names ending in `.json` (case-insensitive) are parsed as JSON;
all other file names are parsed as KITTI text.

On GPU the execution mode defaults to `ACCURACY` automatically (MonoDETR's
backbone overflows FP16 under the default performance mode); this can be
overridden with `ie-config=EXECUTION_MODE_HINT=…`.

## Examples

```sh
# CPU
gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! \
  gvamono3d model=monodetr.xml device=CPU calibration-file=000000.txt threshold=0.2 ! \
  gvawatermark3d calibration-file=000000.txt ! videoconvert ! autovideosink

# GPU (execution mode defaults to ACCURACY)
gst-launch-1.0 filesrc location=input.mp4 ! decodebin3 ! \
  gvamono3d model=monodetr.xml device=GPU calibration-file=000000.txt threshold=0.2 ! \
  gvametaconvert format=json ! gvametapublish method=file file-path=out.jsonl ! fakesink
```
