# g3drender

Renders a LiDAR point cloud — with optional camera streams and cross-modal detection metadata — into a BGR video frame.

## Overview

The `g3drender` element is a `GstBaseTransform` that accepts either a raw LiDAR stream or a `GstAnalyticsBatchMeta` container (produced by `gvastreammux`, followed by `g3dobjectfuser` or `g3dbevfusion`) and produces a single `video/x-raw(BGR)` frame per input buffer. It supports three rendering modes selected by the `view-mode` property.

**Rendering modes:**

- **`bev` (default)** — Bird's-eye view. The point cloud is projected top-down onto the XY plane. A metric grid and a coordinate-axis indicator are overlaid. 3D detection boxes are drawn as projected footprint polygons.
- **`perspective`** — Synthetic 3D camera view. Camera position is controlled by `cam-distance`, `cam-elevation`, and `cam-azimuth`. Each LiDAR point is coloured by height. 3D detection boxes are drawn as full wire-frame cuboids.
- **`cam-proj`** — LiDAR projected onto a real camera image. LiDAR points and 3D boxes are projected using KITTI-style calibration matrices (`P2 × R0 × Tr`) received from `g3dobjectfuser` or `g3dbevfusion` via the `g3d/calibration` sticky event.

In `bev` and `perspective` modes, each camera tile also shows the 3D boxes projected into that camera, when the `g3d/calibration` event carries a calibration for the camera's `gvastreammux` pad. Box edges are clipped at 0.5 m in front of the camera, so a box that is partly behind it keeps its visible part.

## Properties

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `width` | int | [1, 32767] | 800 | Output frame width in pixels. Automatically doubled to `height × 2` when a batch stream is detected and `width == height`. |
| `height` | int | [1, 32767] | 800 | Output frame height in pixels. |
| `view-mode` | enum | 0 / 1 / 2 | 0 | Rendering mode: `0=bev`, `1=perspective`, `2=cam-proj`. |
| `point-radius` | int | [1, 20] | 2 | Rendered radius of each LiDAR point in pixels. `radius=1` uses a fast single-pixel path. |
| `point-stride` | int | [1, 100] | 16 | Point cloud decimation factor. One point in N is rendered; higher values reduce density and improve throughput. The chosen point shifts by one each time, so LiDARs that interleave their laser rings in the point order (such as nuScenes) keep all rings. |
| `zoom` | float | [0.1, 20.0] | 1.0 | BEV viewport zoom. `2.0` halves the visible range (zooms in); `0.5` doubles it. Only effective in `bev` mode. |
| `cam-distance` | float | [1.0, 500.0] | 35.0 | Synthetic camera distance from the scene origin in meters. Only effective in `perspective` mode. |
| `cam-elevation` | float | [5.0, 89.0] | 30.0 | Synthetic camera elevation angle in degrees (`5°` = near eye-level, `89°` = near top-down). Only effective in `perspective` mode. |
| `cam-azimuth` | float | [-360.0, 360.0] | 180.0 | Synthetic camera horizontal azimuth in degrees. Only effective in `perspective` mode. |
| `cam-fov` | float | [10.0, 150.0] | 60.0 | Synthetic camera vertical field of view in degrees. Only effective in `perspective` mode. |
| `cam-proj-index` | int | [0, 255] | 0 | `gvastreammux` pad index of the camera to use as the projection background in `cam-proj` mode. The first camera in the batch is used when no camera has this index. |
| `cam-bg-grayscale` | bool | true / false | true | Convert the camera background to grayscale in `cam-proj` mode. |
| `cam-bg-dim` | float | [0.0, 1.0] | 0.65 | Brightness multiplier for the camera background in `cam-proj` mode (`0.0=black`, `1.0=original`). |
| `forward-axis` | enum | `x` / `y` | `x` | LiDAR axis that points in the vehicle's forward direction: `x` (KITTI) or `y` (nuScenes). `bev` mode draws it pointing up, and `perspective` mode measures `cam-azimuth` from it, so `cam-azimuth=180` looks forward from behind the vehicle either way. |

## Pipeline Examples

### LiDAR-only BEV render

```bash
# BEV with 2× zoom; render every 16th LiDAR point with a radius of 2 pixels
gst-launch-1.0 -v \
    multifilesrc location="<lidar_path>/lidar.bin" ! \
    g3dlidarparse ! \
    g3dinference ! \
    g3drender view-mode=bev zoom=2.0 point-stride=16 point-radius=2 ! \
    videoconvert ! \
    autovideosink
```

### Camera + LiDAR fusion with perspective render

```bash
# Perspective mode: camera at 35 m distance, 30° elevation, 180° azimuth (directly behind), 1600×800 output
gst-launch-1.0 -v \
    rtspsrc ! decodebin ! videoconvert ! gvadetect ! mux.sink_0 \
    multifilesrc location="<lidar_path>/lidar.bin" ! g3dlidarparse ! g3dinference ! mux.sink_1 \
    gvastreammux name=mux ! \
    g3dobjectfuser ! \
    g3drender view-mode=perspective width=1600 height=800 \
              cam-azimuth=180 cam-elevation=30 cam-distance=35 ! \
    videoconvert ! \
    autovideosink
```

### Camera + LiDAR fusion with cam-proj render

`cam-proj` mode requires a camera stream and calibration matrices from `g3dobjectfuser`. The first camera stream is used by default; set `cam-proj-index` to select a different one.

```bash
# cam-proj mode: use the first camera stream as background; render every 4th LiDAR point with a radius of 2 pixels
gst-launch-1.0 -v \
    rtspsrc ! decodebin ! videoconvert ! gvadetect ! mux.sink_0 \
    multifilesrc location="<lidar_path>/lidar.bin" ! g3dlidarparse ! g3dinference ! mux.sink_1 \
    gvastreammux name=mux ! \
    g3dobjectfuser ! \
    g3drender view-mode=cam-proj cam-proj-index=0 point-stride=4 point-radius=2 ! \
    videoconvert ! \
    autovideosink
```

### Multi-camera + LiDAR fusion with perspective render

```bash
# Perspective mode: camera at 35 m distance, 30° elevation, 180° azimuth (directly behind), 1600×800 output
gst-launch-1.0 -v \
    rtspsrc ! decodebin ! videoconvert ! gvadetect ! mux.sink_0 \
    rtspsrc ! decodebin ! videoconvert ! gvadetect ! mux.sink_1 \
    multifilesrc location="<lidar_path>/lidar.bin" ! g3dlidarparse ! g3dinference ! mux.sink_2 \
    gvastreammux name=mux ! \
    g3dobjectfuser ! \
    g3drender view-mode=perspective width=1600 height=800 \
              cam-azimuth=180 cam-elevation=30 cam-distance=35 ! \
    videoconvert ! \
    autovideosink
```

## Input / Output

| | Capability |
|---|---|
| **Sink** | `application/x-lidar` — raw LiDAR-only stream |
| **Sink** | `multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)` — batch container with one LiDAR sub-stream and zero or more camera sub-streams |
| **Src** | `video/x-raw, format=BGR` |

The element always allocates a new output buffer; the input buffer is not modified.

**Canvas layout:**

```
# LiDAR only
┌──────────────────────────┐
│       LiDAR panel        │
│     (height × height)    │
└──────────────────────────┘

# LiDAR + 1 camera
┌──────────────────────────┬──────────────────────────┐
│       camera panel       │       LiDAR panel        │
│      (remaining width)   │     (height × height)    │
└──────────────────────────┴──────────────────────────┘

# LiDAR + 2 cameras (1 col × 2 rows)
┌──────────────────────────┬──────────────────────────┐
│         camera 0         │       LiDAR panel        │
│──────────────────────────│     (height × height)    │
│         camera 1         │                          │
└──────────────────────────┴──────────────────────────┘

# LiDAR + 3 cameras (1 col × 3 rows)
┌──────────────────────────┬──────────────────────────┐
│         camera 0         │       LiDAR panel        │
│──────────────────────────│     (height × height)    │
│         camera 1         │                          │
│──────────────────────────│                          │
│         camera 2         │                          │
└──────────────────────────┴──────────────────────────┘

# LiDAR + 4 cameras (2 cols × 2 rows)
┌─────────────┬────────────┬──────────────────────────┐
│   camera 0  │  camera 1  │       LiDAR panel        │
│─────────────┼────────────│     (height × height)    │
│   camera 2  │  camera 3  │                          │
└─────────────┴────────────┴──────────────────────────┘

# cam-proj mode (full canvas, any number of cameras)
┌──────────────────────────────────────────────────────┐
│         camera background + LiDAR projection         │
│                   (width × height)                   │
└──────────────────────────────────────────────────────┘
```

The LiDAR panel is always a `height × height` square anchored to the right edge. Each camera frame is letterboxed into its grid cell. The layout switches from 1 column to 2 columns when 4 or more camera streams are present.

## Notes

- **BEV range and zoom**: LiDAR has no native image resolution. In `bev` mode the internal render range defaults to ±50 m on each axis, mapped to the output canvas. `zoom=2.0` halves the visible range (25 m), zooming in; `zoom=0.5` doubles it (100 m), zooming out. `zoom` has no effect in `perspective` or `cam-proj` mode.

- **Auto-double width**: When the input is a batch stream and `width == height`, `g3drender` automatically doubles `width` to `height × 2` during caps negotiation so the default square canvas fits the side-by-side camera + LiDAR layout. Set `width` to a value other than `height` to suppress this behaviour.

- **cam-proj requires calibration**: `cam-proj` mode depends on the `g3d/calibration` sticky event emitted by `g3dobjectfuser` or `g3dbevfusion`. Without a calibration for the selected camera the element falls back to `bev`.

- **3D box convention**: boxes are drawn as `GstAnalytics3DODMtd` defines them: `length` along the box's own x axis, `width` along its y axis, rotated by `yaw` counter-clockwise about +z.

- **Camera labels**: each camera tile, and the `cam-proj` image, is labelled with the camera's `name` from the `g3d/calibration` event (`g3dbevfusion` sends nuScenes names such as `CAM_FRONT`), or `Cam <pad>` without one.

- **Box colours**: a 3D box that `g3dobjectfuser` associated with a camera detection is white. Otherwise, when the `g3d/calibration` event lists `class-names` (as `g3dbevfusion` does), boxes are coloured per class; if not, they are green.

- **Point intensity**: points are coloured by intensity, normalised to 0..1. Intensities above 1.5 in a frame are taken to be on a 0..255 scale.

- **cam-proj projects one camera at a time**: When multiple camera streams are present, only the stream selected by `cam-proj-index` (default `0`) is used as the projection background. Other camera streams are ignored in this mode.

## Element Details (gst-inspect-1.0)

```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      application/x-lidar
      multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: BGR (gchararray)
                  width: [ 1, 32767 ] (GstIntRange)
                 height: [ 1, 32767 ] (GstIntRange)
              framerate: [ 0/1, 120/1 ] (GstFractionRange)

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  cam-azimuth         : Camera horizontal angle in degrees (perspective mode)
                        flags: readable, writable
                        Float. Range:            -360 -             360 Default:             180
  cam-distance        : Camera distance from origin in meters (perspective mode)
                        flags: readable, writable
                        Float. Range:               1 -             500 Default:              35
  cam-elevation       : Camera elevation angle in degrees: 0=horizon 90=top-down (perspective mode)
                        flags: readable, writable
                        Float. Range:               5 -              89 Default:              30
  cam-fov             : Field of view in degrees (perspective mode)
                        flags: readable, writable
                        Float. Range:              10 -             150 Default:              60
  forward-axis        : Sensor axis that points in the vehicle's forward direction: x (KITTI) or y (nuScenes). bev mode draws it pointing up, and perspective mode measures cam-azimuth from it
                        flags: readable, writable
                        Enum "GstG3DRenderForwardAxis" Default: 0, "x"
                           (0): x                - Sensor +x points forward (e.g. KITTI)
                           (1): y                - Sensor +y points forward (e.g. nuScenes)
  cam-proj-index      : gvastreammux pad index of the camera to use for LiDAR projection in cam-proj mode; the first camera in the batch is used when no camera has this index
                        flags: readable, writable
                        Integer. Range: 0 - 255 Default: 0
  cam-bg-dim          : Brightness multiplier for the camera background in cam-proj mode (0.0=black, 1.0=original)
                        flags: readable, writable
                        Float. Range:               0 -               1 Default:            0.65
  cam-bg-grayscale    : Convert the camera background to grayscale in cam-proj mode
                        flags: readable, writable
                        Boolean. Default: true
  height              : Output image height in pixels
                        flags: readable, writable
                        Integer. Range: 1 - 32767 Default: 800
  point-radius        : Radius of each rendered point in pixels
                        flags: readable, writable
                        Integer. Range: 1 - 20 Default: 2
  point-stride        : Render every Nth point (1 = all points, 16 = every 16th point, etc.)
                        flags: readable, writable
                        Integer. Range: 1 - 100 Default: 16
  view-mode           : Rendering mode: bev (Bird's Eye View), perspective (3D perspective), or cam-proj (project LiDAR onto camera image using calibration)
                        flags: readable, writable
                        Enum "GstG3DRenderViewMode" Default: 0, "bev"
                           (0): bev              - Bird's Eye View
                           (1): perspective      - Perspective 3D
                           (2): cam-proj         - Camera Projection
  width               : Output image width in pixels
                        flags: readable, writable
                        Integer. Range: 1 - 32767 Default: 800
  zoom                : BEV zoom factor: 1.0=default (50m range), 2.0=zoomed in (25m range), 0.5=zoomed out (100m range)
                        flags: readable, writable
                        Float. Range:             0.1 -              20 Default:               1
```
