# Requirements Questionnaire

Collect missing pipeline requirements from the user.
Present all questions **interactively in a single round** using `vscode_askQuestions`
or inline in chat.

If the user's prompt matches a [Fast Path](../SKILL.md#fast-path-pattern-table-match),
pre-fill with inferred values and mark as `recommended`.

---

## Section 1 — Input

| Header | Question | Options |
|--------|----------|---------|
| `Input Type` | What type of video input? | `Local file path`, `HTTP URL`, `RTSP stream URI` |
| `Input Value` | Provide the path or URL (e.g. `/path/to/video.mp4`, `https://...`, `rtsp://...`) | Free text |

## Section 2 — AI Models

| Header | Question | Options |
|--------|----------|---------|
| `Model 1` | First model URL/name (e.g. `yolo11n`, `PaddlePaddle/PP-OCRv5_server_rec`) | Free text |
| `Model 1 Task` | What does this model do? | `Object detection`, `Classification`, `OCR / text recognition`, `VLM / generative AI`, `Segmentation`, `Pose estimation`, `Other` |
| `Model 2 (optional)` | Second model URL/name. Leave empty if not needed. | Free text |
| `Model 2 Task` | What does the second model do? (skip if no Model 2) | Same options as Model 1 Task |

> Task maps to DL Streamer elements: detection → `gvadetect`, classification/OCR → `gvaclassify`, VLM → `gvagenai`.

## Section 3 — Target Environment

| Header | Question | Options |
|--------|----------|---------|
| `Intel Platform` | What Intel hardware will this run on? | `Intel Core Ultra 3 (Panther Lake) — CPU + Xe3 GPU + NPU`, `Intel Core Ultra 2 (Lunar Lake / Arrow Lake) — CPU + GPU + NPU`, `Intel Core Ultra 1 (Meteor Lake) — CPU + GPU + NPU`, `Intel Core (older, no NPU) — CPU + GPU`, `Intel Xeon (server) — CPU only`, `Intel Arc discrete GPU`, `Not sure / detect at runtime` |
| `Available Accelerators` | Which accelerators are available? (select all that apply) | `GPU (/dev/dri/renderD128)`, `NPU (/dev/accel/accel0)`, `CPU only` (multiSelect) |

> The agent uses these answers to apply the [Element & Device Selection](./pipeline-construction.md#element--device-selection) guidance
> when setting `device=` and `batch-size=` on inference elements.
> For advanced tuning (multi-GPU selection, pre-process backends, MULTI: device),
> refer to the docs:
> - [Performance Guide](../../../../docs/user-guide/dev_guide/performance_guide.md) — batch-size, multi-stream, memory types
> - [GPU Device Selection](../../../../docs/user-guide/dev_guide/gpu_device_selection.md) — multi-GPU systems
> - [Optimizer](../../../../docs/user-guide/dev_guide/optimizer.md) — auto-tuning tool

## Section 4 — Output

| Header | Question | Options |
|--------|----------|---------|
| `Output Format` | What outputs do you need? | `Annotated video (.mp4)`, `JSON metadata (.jsonl)`, `JPEG snapshots`, `Display window`, `All of the above` (multiSelect) |

## Section 5 — Application Type

| Header | Question | Options |
|--------|----------|---------|
| `Application Type` | Python application or GStreamer command line? | `Python application` (recommended), `GStreamer command line` |


