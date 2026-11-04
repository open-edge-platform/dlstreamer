# gvaframeselect

Selects up to `frame-budget` evenly spaced frames from each `sub-stream-frames` block.
The selection restarts when the input buffer has `GST_BUFFER_FLAG_DISCONT`; `gvaeventrecorder`
marks the first forwarded frame of each event this way. Selected buffers are cached until the
frame budget is collected, then forwarded in input order with one shared ROI. Input and output
video caps are unchanged.

| Property | Type | Range | Default | Description |
| --- | --- | --- | --- | --- |
| `sub-stream-frames` | Unsigned Integer | [1, 4294967295] | 900 | Length $N$, in input frames, of each selection block. The pattern restarts every $N$ frames and on `DISCONT`. |
| `frame-budget` | Unsigned Integer | [1, 4294967295] | 8 | Maximum number $k$ of frames forwarded per full block, selected evenly across the block. For $k=1$, the first frame is selected. |
| `target-classes` | String | — | `person,vehicle` | Comma-separated detection labels included when calculating the shared ROI across the selected frames. |
| `confidence-threshold` | Double | [0.0, 1.0] | 0.5 | Minimum confidence of a target detection used in the shared ROI calculation. Set this to the same value as `gvaeventrecorder`; the value is not propagated automatically. |
| `roi-padding` | Unsigned Integer | [0, 4294967295] | 0 | Pixels added on each side of the shared ROI, clipped to the frame boundaries. |
| `output-dir` | String | — | (empty) | If set, each forwarded frame is also saved to this directory as `frame_XXXXXXXX.jpg`, where `XXXXXXXX` is the zero-padded stream frame number. The directory is created if needed. |

## Shared ROI

For each selection block, the element caches the selected frames until it has collected
`frame-budget` frames. It computes the union of the bounding boxes for all objects whose labels
match `target-classes` and whose confidence meets `confidence-threshold`, across all classes and
cached frames. The configured `roi-padding` is then added on every side, with the result clipped
to the image bounds. The same marked ROI metadata is attached to every cached frame, including
PRE and POST event frames without an accepted target detection, so `gvaroiscale` crops the same
area throughout the selection block. Original analytics detections are preserved. If none of
the cached frames has an accepted target detection, every frame gets a full-frame ROI instead.

This buffering adds latency: a full block is not forwarded until its selected-frame budget is
collected. A partial block is flushed at `event=stop` from `gvaeventrecorder`, at a block boundary,
or on EOS. `gvaroiscale` recognizes the marked shared ROI and uses it as one crop instead of using
the original per-detection boxes.

`confidence-threshold` is configured independently on `gvaeventrecorder` and `gvaframeselect`.
Set both explicitly to the same value so the event trigger and ROI calculation use a consistent
confidence cutoff.

For block length $N$ and budget $k$, the selected position is
$\operatorname{round}(i \times (N - 1) / (k - 1))$. If $k=1$, frame index zero is selected;
if $k \geq N$, every frame is forwarded. A short final block forwards only selected positions
that occur before end-of-stream; its selection is not recalculated for the shorter length.

The stream frame number counts every input frame from `STREAM_START`, including unselected frames.
It is reset on a new `STREAM_START` event, but not on `DISCONT`. Each forwarded frame carries
this number in its buffer offset (`GST_BUFFER_OFFSET`), which GStreamer defines as the frame number
for video. If `output-dir` is set, an
unmodified copy of the frame is saved as `frame_<number>.jpg`, with the number zero-padded to
8 digits. Saving requires frames in system memory.

```sh
... ! gvaeventrecorder confidence-threshold=0.6 ! \
  gvaframeselect sub-stream-frames=900 frame-budget=8 target-classes=person,vehicle \
    confidence-threshold=0.6 roi-padding=16 ! gvaroiscale ...
```

```sh
... ! gvaframeselect sub-stream-frames=900 frame-budget=8 output-dir=./selected_frames ! ...
```

For a runnable command-line example, see the [gvaframeselect sample](../../../samples/gstreamer/gst_launch/gvaframeselect/README.md).

## Element Details (gst-inspect-1.0)

```text
Pad Templates:
	SINK template: 'sink'
		Availability: Always
		Capabilities:
			video/x-raw
								 format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:DMABuf)
								 format: { (string)DMA_DRM } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:VASurface)
								 format: { (string)NV12 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:VAMemory)
								 format: { (string)NV12 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)

	SRC template: 'src'
		Availability: Always
		Capabilities:
			video/x-raw
								 format: { (string)BGRx, (string)BGRA, (string)BGR, (string)NV12, (string)I420 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:DMABuf)
								 format: { (string)DMA_DRM } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:VASurface)
								 format: { (string)NV12 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)
			video/x-raw(memory:VAMemory)
								 format: { (string)NV12 } (GstValueList)
									width: [ 1, 2147483647 ] (GstIntRange)
								 height: [ 1, 2147483647 ] (GstIntRange)
							framerate: [ 0/1, 2147483647/1 ] (GstFractionRange)

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
	SINK: 'sink'
		Pad Template: 'sink'
	SRC: 'src'
		Pad Template: 'src'

Element Properties:

	target-classes      : Comma-separated detection labels used when calculating the shared ROI
										flags: readable, writable
										String. Default: "person,vehicle"
	confidence-threshold: Minimum detection confidence used when calculating the shared ROI; set it to the same value as gvaeventrecorder
										flags: readable, writable
										Double. Range: 0 - 1 Default: 0.5
	roi-padding         : Pixels added on each side of the shared ROI
										flags: readable, writable
										Unsigned Integer. Range: 0 - 4294967295 Default: 0
	frame-budget        : Maximum k frames forwarded per full block; selected positions are evenly spaced, with the first frame used when k is 1
												flags: readable, writable
												Unsigned Integer. Range: 1 - 4294967295 Default: 8
	name                : The name of the object
												flags: readable, writable
												String. Default: "gvaframeselect0"
	output-dir          : If set, each forwarded frame is also saved to this directory as frame_<stream frame number>.jpg; frame numbers count all input frames from stream start
												flags: readable, writable
												String. Default: null
	parent              : The parent of the object
												flags: readable, writable
												Object of type "GstObject"
	qos                 : Handle Quality-of-Service events
												flags: readable, writable
												Boolean. Default: false
	sub-stream-frames   : Length N, in input frames, of each selection block; the pattern restarts every N frames and on DISCONT
												flags: readable, writable
												Unsigned Integer. Range: 1 - 4294967295 Default: 900
```