# Requirements Questionnaire

Detailed questionnaire for collecting pipeline requirements from the user.
Read this reference when the user's prompt is missing key information
(see [Step 0 checklist](../SKILL.md) for what to look for).

Present all questions **interactively in a single round**.
If an interactive question tool is available (e.g. `vscode_askQuestions` in VS Code Copilot),
use it to present all questions at once. Otherwise, list the questions inline in chat
and ask the user to confirm or override before proceeding.

If the user's prompt matches a [Fast Path](../SKILL.md#fast-path-pattern-table-match) row,
pre-fill these questions with the inferred values and mark them as `recommended`.
The user can confirm or override before proceeding.

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

> Add more `Model N` + `Model N Task` pairs only if the use case clearly requires 3+ models.
>
> The task selection maps directly to DL Streamer elements:
> | Task | Element |
> |------|---------|
> | Object detection | `gvadetect` |
> | Classification / OCR / Pose estimation / Segmentation | `gvaclassify` |
> | VLM / generative AI | `gvagenai` |
>
> The agent auto-infers the source ecosystem (HuggingFace / Ultralytics / direct URL)
> from the model URL or name — no need to ask the user separately.

## Section 3 — Target Environment

| Header | Question | Options |
|--------|----------|---------|
| `Intel Platform` | What Intel hardware will this run on? | `Intel Core Ultra 3 (Panther Lake) — CPU + Xe3 GPU + NPU`, `Intel Core Ultra 2 (Lunar Lake / Arrow Lake) — CPU + GPU + NPU`, `Intel Core Ultra 1 (Meteor Lake) — CPU + GPU + NPU`, `Intel Core (older, no NPU) — CPU + GPU`, `Intel Xeon (server) — CPU only`, `Intel Arc discrete GPU`, `Not sure / detect at runtime` |
| `Available Accelerators` | Which accelerators are available? (select all that apply) | `GPU (/dev/dri/renderD128)`, `NPU (/dev/accel/accel0)`, `CPU only` (multiSelect) |

> The agent uses these answers to apply **Rule 6 — Device Assignment Strategy** from
> [Pipeline Construction Reference](./pipeline-construction.md#rule-6--device-assignment-strategy-for-intel-core-ultra)
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
| `Application Type` | Python application or Gstreamer command line? | `Python application` (recommended), `Gstreamer command line` |


