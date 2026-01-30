# gvamotiondetect

Performs lightweight motion detection on NV12 video frames and emits motion regions of interest (ROIs) as analytics metadata. Automatically uses a VA-API (GPU) accelerated path when VAMemory caps are negotiated; otherwise falls back to a system-memory (CPU) path. Designed for low-latency scene motion highlighting and downstream triggering without requiring a full inference model.

```bash
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw(memory:VAMemory)
                format: (string)NV12
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw
                format: (string)NV12
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw(memory:VAMemory)
                format: (string)NV12
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw
                format: (string)NV12
                  width: [ 1, 2147483647 ]
                height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  block-size           : Full-resolution block size (pixels) used to build the grid for per-block motion ratio.
                          flags: readable, writable
                          Integer. Default: 64 (range 16..512)
  motion-threshold     : Per-block changed pixel ratio (0..1) required to flag motion in that block before temporal confirmation.
                          flags: readable, writable
                          Double. Default: 0.05
  min-persistence      : Frames a tracked ROI must persist (age) before being eligible for emission.
                          flags: readable, writable
                          Integer. Default: 2 (range 1..30)
  max-miss             : Grace frames allowed after last successful match before a tracked ROI is purged.
                          flags: readable, writable
                          Integer. Default: 1 (range 0..30)
  iou-threshold        : Intersection-over-Union threshold (0..1) used to match raw motion rectangles to existing tracked ROIs.
                          flags: readable, writable
                          Double. Default: 0.3
  smooth-alpha         : Exponential moving average smoothing factor (0..1) for ROI coordinates; higher values follow motion faster.
                          flags: readable, writable
                          Double. Default: 0.5
  confirm-frames       : Consecutive frames required to confirm a motion block. 1 = immediate single-frame detection (most sensitive).
                          flags: readable, writable
                          Integer. Default: 1 (range 1..10)
  pixel-diff-threshold : Per-pixel absolute luma difference (1..255) applied before thresholding; lower values increase sensitivity.
                          flags: readable, writable
                          Integer. Default: 15
  min-rel-area         : Minimum relative frame area (0..0.25) a motion rectangle must cover to be considered (filters tiny noise boxes).
                          flags: readable, writable
                          Double. Default: 0.0005
  name                 : The name of the element instance.
                          flags: readable, writable
                          String. Default: "gvamotiondetect0"
  parent               : The parent object.
                          flags: readable, writable
                          Object of type "GstObject"

Relationship between confirm-frames and min-persistence:
  These two controls act at different stages and are not interchangeable:
  - confirm-frames works at the raw per-block motion stage. It requires N consecutive frames of activity in the same grid block before that block contributes to a raw motion rectangle. It filters out flicker/noise early.
  - min-persistence applies after rectangles are merged and tracked. A tracked ROI must be matched for N frames (its age) before it is emitted. It guards publication of unstable, short-lived motion regions.
  Effect: Raising confirm-frames reduces how many raw rectangles enter tracking (front-end suppression). Raising min-persistence delays emission of tracked ROIs (back-end stabilization). You can combine them: e.g., confirm-frames=2 with min-persistence=2 yields rectangles only after two agreeing frames and then requires two tracked frames for output (at least 2 total, possibly 3 if initial confirmation overlaps). Keeping confirm-frames=1 but min-persistence>1 allows immediate raw detection yet still waits for persistence before emission.

Metadata Output:
- Each emitted motion ROI is attached as a `GstVideoRegionOfInterestMeta` with label "motion".
- A `GstAnalyticsRelationMeta` aggregates object detection metadata entries (type quark "motion") for all ROIs on the frame.
- ROI coordinates are normalized internally for analytics structures and rounded to 3 decimal places to reduce payload size.

Algorithm Summary:
1. Acquire current frame luma plane (VA surface fast path or system-memory conversion) and downscale to a working size.
2. Build motion mask: absdiff(previous, current) -> GaussianBlur -> threshold (pixel-diff-threshold) -> morphology (open + dilate).
3. Grid scan: accumulate changed pixels per block, compute ratio; mark blocks exceeding motion-threshold.
4. Temporal confirmation: if confirm-frames > 1 require consecutive active frames per block before rectangle creation.
5. Merge overlapping rectangles, track over time with IoU matching and exponential smoothing (smooth-alpha).
6. Apply persistence (min-persistence) and miss grace (max-miss), then emit stable ROIs with associated analytics metadata.

Tuning Guidelines:
- Increase `pixel-diff-threshold` to reduce noise sensitivity (e.g., minor lighting flicker). Decrease for subtle motion.
- Increase `motion-threshold` to require more changed pixels in a block (filters small localized changes).
- Raise `confirm-frames` (e.g., 2-3) to suppress transient single-frame spikes; keep at 1 for maximal responsiveness.
- Adjust `min-persistence` if you want to delay ROI publication until sustained movement is observed.
- Lower `iou-threshold` if objects move rapidly and fail to match across frames; raise to avoid merging nearby independent motions.
- Set `smooth-alpha` closer to 1.0 for minimal smoothing (snappier boxes) or lower for steadier boxes.
 - Raise `min-rel-area` to suppress very small (potentially noisy) motion rectangles; lower it to allow detection of tiny/distant objects. Default 0.0005 ≈ 0.05% of frame area.

Performance Notes:
- VA-API path uses hardware surface mapping and optional hardware downscale (via `vaBlitSurface` when available) to minimize memory bandwidth.
- System-memory path performs software resize; consider reducing `block-size` to balance granularity vs CPU cost.
- Internal working resolution scales proportionally to input width when downscaling; large frames benefit more from confirmation and smoothing.

Limitations / Future Improvements:
- Only NV12 format is currently supported (both system memory and VAMemory).
- Block merging uses a simple O(n^2) combination; extremely dense motion may produce fewer, larger ROIs.
- No explicit per-ROI confidence beyond binary motion presence (confidence fixed to 1.0 in analytics metadata).
- Coordinate rounding (3 decimal places) applied for analytics metadata; raw ROI meta stores integer pixel coordinates.

```bash
Plugin Registration:
  Name: gvamotiondetect
  Classification: Filter/Video
  Description: Automatically uses VA surface path when VAMemory caps negotiated; otherwise system memory path
```

