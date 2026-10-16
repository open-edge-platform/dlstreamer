# gvaframeselect

Selects up to `frame-budget` evenly spaced frames from each `sub-stream-frames` block.
The selection restarts when the input buffer has `GST_BUFFER_FLAG_DISCONT`; `gvaeventrecorder`
marks the first forwarded frame of each event this way. Input and output video caps are unchanged.

| Property | Type | Range | Default | Description |
|---|---|---|---|---|
| `sub-stream-frames` | Unsigned Integer | [1, 4294967295] | 900 | Length $N$, in input frames, of each selection block. The pattern restarts every $N$ frames and on `DISCONT`. |
| `frame-budget` | Unsigned Integer | [1, 4294967295] | 8 | Maximum number $k$ of frames forwarded per full block, selected evenly across the block. For $k=1$, the first frame is selected. |
| `output-dir` | String | — | (empty) | If set, each forwarded frame is also saved to this directory as `frame_<stream frame number>.jpg`. The directory is created if needed. |

For block length $N$ and budget $k$, the selected position is
$\operatorname{round}(i \times (N - 1) / (k - 1))$. If $k=1$, frame index zero is selected;
if $k \geq N$, every frame is forwarded. A short final block forwards only selected positions
that occur before end-of-stream; its selection is not recalculated for the shorter length.

The stream frame number counts every input frame from `STREAM_START`, including dropped frames.
It is reset on a new `STREAM_START` event, but not on `DISCONT`. Each forwarded frame carries
this number in its buffer offset (`GST_BUFFER_OFFSET`), which GStreamer defines as the frame number
for video. If `output-dir` is set, an
unmodified copy of the frame is saved as `frame_<number>.jpg`, with the number zero-padded to
8 digits. Saving requires frames in system memory.

```sh
... ! gvaeventrecorder ! gvaframeselect sub-stream-frames=900 frame-budget=8 ! ...
```

```sh
... ! gvaframeselect sub-stream-frames=900 frame-budget=8 output-dir=./selected_frames ! ...
```

For a runnable command-line example, see the [gvaframeselect sample](../../../samples/gstreamer/gst_launch/gvaframeselect/README.md).