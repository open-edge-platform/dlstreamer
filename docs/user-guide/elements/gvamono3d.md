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
