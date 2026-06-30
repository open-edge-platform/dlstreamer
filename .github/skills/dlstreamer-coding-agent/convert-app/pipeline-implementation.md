# Pipeline Implementation — Verification, Patterns, Pitfalls

Used by **step 5 (Implement)** of [`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

This reference collects all in-code checks and patterns the agent must apply
while writing the converted C++ application.

## 1. Pipeline element availability verification (after EVERY modification)

Every time the pipeline is modified — whether during initial implementation,
debugging, fixing negotiation errors, adding/removing sink paths, or any other
change — re-verify that **every** GStreamer element referenced in the pipeline
actually exists in the current runtime. Applies to **both** styles:

- **String-based** (`gst_parse_launch`): extract every element name from the
  pipeline string.
- **Programmatic** (`gst_element_factory_make` / `gst_element_factory_create`):
  check every factory name passed to `make`/`create` calls.

```bash
# For every element in the pipeline, verify it exists:
for elem in vapostproc gvawatermark videoconvert gvafpscounter autovideosink vah264enc; do
    gst-inspect-1.0 "$elem" >/dev/null 2>&1 && echo "OK: $elem" || echo "MISSING: $elem"
done
```

For programmatic pipelines, additionally grep the C++ source:

```bash
grep -oP 'gst_element_factory_make\s*\(\s*"([^"]+)"' *.cpp | \
    sed 's/.*"\(.*\)"/\1/' | sort -u | \
    while read elem; do
        gst-inspect-1.0 "$elem" >/dev/null 2>&1 && echo "OK: $elem" || echo "MISSING: $elem"
    done
```

A pipeline that compiles in C++ but references a non-existent element fails
only at runtime with an opaque `Failed to create pipeline: no element "..."`
or a NULL return from `gst_element_factory_make`. Utility elements like
`vapostproc`, `vah264enc`, `x264enc`, encoders, and format converters are
common sources. If anything is missing, find an alternative
(`gst-inspect-1.0 --list` or `gst-inspect-1.0 | grep <keyword>`) and update
the pipeline before proceeding. **Never assume an element exists based on
documentation alone — always confirm against the live registry.**

## 2. Metadata flow verification between pipeline stages

Every time the pipeline is modified, verify that GVA inference metadata
propagates correctly from the first inference element to the visualization /
output element. Metadata can silently stop flowing when:

- A `videoconvert` or `capsfilter` is inserted at a position that triggers a
  buffer copy without metadata preservation, or an element between inference
  and watermark fails to forward `GstVideoRegionOfInterestMeta`.
- An element that operates on ROI metadata (e.g. `gvainference` with
  `object-class=license-plate`) receives ROIs with wrong labels due to a
  labels file mismatch.
- A probe is attached at a point in the pipeline where upstream inference
  metadata has not yet been attached to the buffer.

### Verification procedure (after every pipeline change)

1. **Stage-by-stage detection count audit** — run with `--sink fake` and verify
   each stage of the cascade reports the expected number of detections. For
   an N-stage cascade, all N counts must be non-zero. The wrapper / probe
   code MUST print one log line per stage (named after the stage's role)
   that can be grep'd from stdout, e.g.:

   ```bash
   ./run.sh --sink fake 2>&1 | grep -E "<stage1-tag>|<stage2-tag>|<stageN-tag>"
   ```

   If any downstream stage shows zero detections while the upstream shows
   non-zero, metadata is not flowing between those stages. Common causes:
   wrong `object-class` filter, labels file mismatch, missing ROI metadata.

2. **Visual output spot-check** — run with `--sink file`, extract a frame
   known to contain detections, and verify that **every expected overlay type**
   is present (bounding boxes, labels, classification text, tracking IDs). If
   bboxes appear but classification/OCR text is missing, the most likely cause
   is `videoconvert` dropping `GstVideoRegionOfInterestMeta.params` — see §6
   (two-probe architecture). If no overlays appear at all, `gvawatermark` is
   likely in transparent mode — see §5.

3. **Semantic sanity check** — verify detection labels match expected
   semantics by comparing each detection's bounding-box dimensions against
   the size of the object class it is supposed to represent. If the pipeline
   emits an ROI whose bbox dimensions clearly do not fit the semantic class
   (e.g. an ROI labelled as a small object but covering most of the frame),
   the labels file indexing is wrong (§4). Run the model on a single frame
   via OpenVINO Python API to confirm `class_id` values align with the
   labels file.

4. **Emitted-label spot dump (MANDATORY before relying on `object-class=` or
   `roi.label()` string compares)** — OpenVINO IR models can ship with an
   embedded `<rt_info><model_info><labels value="..." /></model_info></rt_info>`
   block. `gvadetect` may use that block as the source of class names and
   **the external `labels-file` may be ignored or merged** depending on the
   model_type and the DL Streamer version. The effective ROI label is
   therefore not predictable from the `labels-file` alone.

   Before wiring `object-class=` on a downstream element OR comparing
   `roi.label()` in a probe, the agent MUST dump the **actual emitted label**
   for the first frames containing a detection:

   ```bash
   gst-launch-1.0 -q filesrc location=<input> ! decodebin ! videoconvert ! \
       gvadetect model=<xml> labels-file=<txt> threshold=0.1 ! \
       gvametaconvert format=json ! gvametapublish file-format=json-lines \
           file-path=/tmp/lab.jsonl ! fakesink
   python3 -c "import json,collections; c=collections.Counter(); \
       [c.update([o['detection'].get('label','?')]) for l in open('/tmp/lab.jsonl') \
        if l.startswith('{') for o in json.loads(l).get('objects',[])]; \
       print(c.most_common())"
   ```

   The label strings that appear in the printed `Counter` are exactly the
   strings that downstream `gvadetect object-class=` / `gvaclassify
   object-class=` filters compare against, and the strings the probe will see
   in `roi.label()`. If they do not match the expected vocabulary, adjust the
   `labels-file`, change `object-class=` (and probe filters) to the
   actually-emitted strings, or insert a labels-rewriting probe. Never assume
   the `labels-file` remap took effect without this dump.

This verification is especially critical for cascade architectures — a single
misalignment can cause the entire downstream chain to produce zero results
while the pipeline reports no errors.

## 3. Model–inference-mode compatibility validation

When mapping a secondary detector that runs on ROIs in the source pipeline
(e.g. DeepStream SGIE with `operate-on-gie-id`), do NOT blindly translate it
to `inference-region=roi-list` in DL Streamer. Many models are trained on
full-frame images and suffer catastrophic accuracy loss when applied to
cropped/letterboxed ROIs produced by `roi-list` — in practice this can range
from a small recall drop to a near-total collapse (i.e. the same model and
clip yielding orders of magnitude more detections in full-frame mode than in
roi-list mode).

Validate for **every** `gvadetect` / `gvainference` element that is a
candidate for `inference-region=roi-list`:

1. **Run a quick A/B test** with `gst-launch-1.0`. Bare `fakesink` does not
   serialise GVA metadata to stdout, so always pipe through
   `gvametaconvert ! gvametapublish` (writing JSON-Lines to disk) and count
   detections from the output file — never `grep -c` on stderr/stdout from
   `fakesink`, which always reports 0.
   - **A** — `inference-region=full-frame` with `threshold=0.3`
   - **B** — `inference-region=roi-list` with `threshold=0.01` (intentionally
     low to catch even weak detections)

   ```bash
   # Variant A (full-frame). Re-run with `inference-region=roi-list object-class=<cls>`
   # and a separate output path (e.g. /tmp/ab_roi.jsonl) for variant B.
   gst-launch-1.0 -q ... ! gvadetect model=<model> inference-region=full-frame threshold=0.3 ! \
       gvametaconvert format=json ! \
       gvametapublish file-format=json-lines file-path=/tmp/ab_full.jsonl ! fakesink
   # Count detections in either output file (one JSON object per frame; sum object array lengths)
   python3 -c "import json,sys; print(sum(len(json.loads(l).get('objects',[])) for l in open(sys.argv[1]) if l.startswith('{')))" /tmp/ab_full.jsonl
   ```

2. **Decision rule**:
   - If `roi-list` count is **≥ 50%** of `full-frame` count AND max confidence
     is **≥ 0.3** → use `roi-list` (preserves cascade semantics, avoids false
     positives on background).
   - Otherwise → use `full-frame` and document the decision with A/B numbers
     in the README under `Conversion Notes → Inference mode selection`.

3. **Metadata impact** — switching from `roi-list` to `full-frame` changes the
   metadata topology: detections become top-level `ODMtd` entries (no
   `parent_id` / no `CONTAIN` relation to an upstream detector's ROI). The
   probe callback MUST be adjusted accordingly — iterate all ODs and match by
   the label string the model actually emits (see §2, step 4 of the
   verification procedure, for how to discover it) instead of walking
   `CONTAIN` relations from a parent object.

4. **Document** the A/B test results and chosen mode in the README. Include
   detection counts, max confidence, and rationale.

## 4. Label file indexing for SSD DetectionOutput models

SSD-style models with a `DetectionOutput` layer use `class_id=0` for the
background class. The labels file consumed by `gvadetect` maps line number N
(0-indexed) to `class_id=N`. If the labels file starts with the first real
object class instead of a placeholder for background at index 0, **all class
IDs shift by one** — every detection ends up under the label one row above
its true class, and the last class disappears entirely. The pipeline still
compiles, runs and reports detections, but every label is wrong (semantic
nonsense — e.g. ROIs whose bbox dimensions clearly do not match the assigned
class).

Verify label-to-class-id alignment for every detection model:

1. Run the model on a single representative frame using OpenVINO Python API
   (`ov.Core().compile_model`) and inspect the raw `class_id` values in the
   `DetectionOutput` tensor.
2. Confirm the labels file entry at that index matches the expected semantic
   class.
3. If the model uses `class_id=0` for background (standard for SSD,
   SSD-MobileNet, and similar), the labels file MUST have `background` (or an
   empty line) as its first entry.
4. Record the verified mapping in the README's model substitution table
   (e.g. "class_id 0=background, 1=vehicle, 2=license-plate → labels line
   0='background', line 1='vehicle', line 2='license-plate'").

## 5. `gvawatermark` rendering — transparent path pitfall

`gvawatermark` negotiates `ANY` memory type when upstream buffers are not
explicitly constrained to system memory. This causes it to select the
transparent rendering path (path=3), which bypasses all drawing. The pipeline
runs without errors and processes all frames, but the video is visually
identical to the input.

Debug log shows: `"Transparent path linked (identity bypassed)"`.

**Fix**: insert `capsfilter caps=video/x-raw,format=BGRx` immediately before
`gvawatermark` to force system-memory caps negotiation. The capsfilter MUST
have `name=pre_watermark` so the label probe (§6) can attach to its src pad:

```
videoconvert n-threads=4 ! capsfilter caps=video/x-raw,format=BGRx name=pre_watermark ! gvawatermark ! videoconvert n-threads=4
```

This adds a CPU copy step; expect a measurable FPS reduction (e.g. 170 → 55
FPS) when GPU inference is used. Verify that `gvawatermark` actually renders
overlays by comparing a frame from the output video against the original input
(pixel diff > 0 in bbox regions).

## 6. Probe callbacks must write decoded results to ROI metadata

> **Before writing any manual decoder in a probe**: verify that `gvaclassify`
> does not already post-process this model. DL Streamer's classification
> element ships built-in post-processors for several model families
> (e.g. CTC text-recognition heads, ImageNet-style softmax classifiers,
> regression heads, custom JSON-described outputs). When the embedded
> `rt_info.model_info.model_type` (or a matching `model-proc` file) selects
> such a post-processor, the decoded text/label is **already** present on the
> classification tensor's `label` field and the raw `tensor.data<float>()`
> blob may be **empty** — the raw output is consumed and discarded by the
> post-processor. A probe that ignores `tensor.label()` and tries to run
> its own decoder on the empty blob silently produces zero results.
>
> Verification step before writing a manual decoder:
>
> ```cpp
> // dump first 3 classification tensors raw
> for (auto &t : roi.tensors()) {
>     if (t.is_detection()) continue;
>     const GstStructure *s = t.gst_structure();
>     g_print("[T] %s\n", gst_structure_to_string(s));
> }
> ```
>
> If the dump already contains a populated `label=(string)…` field, **do not
> write a manual decoder** — read `tensor.label()` directly. Only when the
> dump shows `label=""` AND a non-empty `data` blob should the probe perform
> the decode itself.

If a pad probe decodes inference results (e.g. OCR text from an LPR model,
classification labels), printing to console via `g_print()` is **not
sufficient** for visual output. `gvawatermark` renders only metadata attached
to GVA `RegionOfInterest` objects — specifically, it reads `tensor.label()`
from every non-detection tensor in each ROI. If the probe does not write the
decoded result back to the ROI, `gvawatermark` will draw the bounding box but
display no text.

### CRITICAL: `videoconvert` drops `GstVideoRegionOfInterestMeta.params`

In GStreamer 1.24+, `videoconvert` copies `GstVideoRegionOfInterestMeta` to the
output buffer (so bounding box coordinates survive), but it does **NOT** copy
the `params` field (`GList<GstStructure>`) which stores all tensor data
(detection results, classification labels, custom tensors added via
`roi.add_tensor()`). `GstAnalyticsRelationMeta` (the new analytics API) **does**
survive `videoconvert` fully. Net effect: bounding boxes drawn by
`gvawatermark` appear (bbox comes from analytics meta), but **every** text
label — custom decoded text and even detection-confidence labels — is missing
because `tensor.label()` returns empty.

### Two-probe architecture (required when `videoconvert` sits between inference and `gvawatermark`)

1. **Decode probe** — attached to the inference element's **src pad** (before
   `videoconvert`). At this point, `params` still contain raw tensor data.
   Decode the inference output (e.g. LPR int32 indices → plate text string)
   and store in a **global thread-safe store** keyed by `roi.region_id()`
   (stable across the copy — comes from `GstAnalyticsODMtd.id`).

2. **Label probe** — attached to the **capsfilter src pad** (after
   `videoconvert`, immediately before `gvawatermark`). Iterate all ROIs on the
   buffer, look up stored text by `region_id()`, and re-attach the decoded
   result as a fresh `GstStructure` via `roi.add_tensor()`. Since this runs
   just before `gvawatermark`, the tensor is present when `preparePrimsForRoi`
   iterates `roi.tensors()`.

```cpp
/* --- Example pattern: a 2-stage detect + decode-in-probe cascade.
 * Identifiers below (`lp_text_*`, `decode_output`, `"license-plate"`,
 * `int32_t` indices) are illustrative for an OCR-style text-decoding probe
 * \u2014 substitute the data type and the target ROI label with whatever the
 * concrete pipeline actually emits.
 */
#include <map>
#include <mutex>

/* Global store: region_id \u2192 decoded text (populated by decode probe, consumed by label probe) */
static std::mutex                  lp_text_mutex;
static std::map<int, std::string>  lp_text_store;

/* Probe 1: on inference element src pad (BEFORE videoconvert) */
static GstPadProbeReturn decode_probe(GstPad *pad, GstPadProbeInfo *info, gpointer) {
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GVA::VideoFrame vf(buf, caps);
    gst_caps_unref(caps);
    for (auto &roi : vf.regions()) {
        if (roi.label() == "<target-roi-label>") {        // e.g. "license-plate"
            for (auto tensor : roi.tensors()) {
                std::string decoded = decode_output(tensor.data<int32_t>());
                {
                    std::lock_guard<std::mutex> lock(lp_text_mutex);
                    lp_text_store[roi.region_id()] = decoded;
                }
            }
        }
    }
    return GST_PAD_PROBE_OK;
}

/* Probe 2: on capsfilter src pad (AFTER videoconvert, BEFORE gvawatermark) */
static GstPadProbeReturn label_probe(GstPad *pad, GstPadProbeInfo *info, gpointer) {
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GVA::VideoFrame vf(buf, caps);
    gst_caps_unref(caps);
    std::lock_guard<std::mutex> lock(lp_text_mutex);
    for (auto &roi : vf.regions()) {
        auto it = lp_text_store.find(roi.region_id());
        if (it != lp_text_store.end()) {
            GstStructure *s = gst_structure_new(
                "classification_result",
                "label", G_TYPE_STRING, it->second.c_str(),
                "confidence", G_TYPE_DOUBLE, 1.0,
                nullptr);
            roi.add_tensor(GVA::Tensor(s));
        }
    }
    return GST_PAD_PROBE_OK;
}
```

### GstStructure naming rules for `gvawatermark` text rendering

- The structure name MUST NOT be `"detection"` —
  `gvawatermarkimpl::preparePrimsForRoi` skips tensors where
  `tensor.is_detection()` returns true (name equals `"detection"`). Use
  `"classification_result"` or any other name.
- The structure MUST have a `"label"` field of type `G_TYPE_STRING` — this is
  what `tensor.label()` reads.
- Optionally include `"confidence"` (`G_TYPE_DOUBLE`).

The capsfilter before `gvawatermark` MUST have a `name=` property (e.g.
`name=pre_watermark`) so the C++ code can find it via `gst_bin_get_by_name()`
and attach the label probe to its src pad.

## 7. Model path resolution — absolute canonical paths required

DL Streamer inference elements (`gvadetect`, `gvainference`, `gvaclassify`)
reject relative paths and may silently fail on symlinks when resolving the
model file. The converted C++ code MUST resolve every model path to an
absolute canonical path via POSIX `realpath()` **before** embedding it in the
pipeline string or passing it to `g_object_set()`:

```cpp
auto resolve_path = [](const gchar *path) -> std::string {
    char *abs = realpath(path, nullptr);
    if (!abs) {
        g_printerr("ERROR: Cannot resolve path: %s (%s)\n", path, strerror(errno));
        return "";
    }
    std::string result(abs);
    free(abs);
    return result;
};
std::string model_abs = resolve_path(user_supplied_model_path);
```

Apply this to **all** file paths passed to inference elements: `model=`,
`model-proc=`, `labels-file=`. Failure to do so causes opaque
`Failed to set pipeline to PLAYING` or `Model file not found` errors that are
hard to diagnose because the working directory at runtime may differ from the
build directory.

## 8. Runtime hardware encoder detection

The converted application MUST NOT hardcode a specific GStreamer encoder
element name (e.g. `vah264enc`, `x264enc`). Encoder availability varies across
DL Streamer installations, driver versions, and hardware:

- `vah264enc` requires the VA-API plugin (often missing).
- `qsvh264enc` requires Intel Media SDK/QSV.
- Software fallbacks (`openh264enc`, `x264enc`) may not be installed.

The C++ code MUST probe the GStreamer element registry at runtime and select
the first available encoder from a priority-ordered list:

```cpp
static std::string find_hw_encoder() {
    const char *candidates[] = {"vah264enc", "vah264lpenc", "qsvh264enc", nullptr};
    for (int i = 0; candidates[i]; i++) {
        GstElementFactory *f = gst_element_factory_find(candidates[i]);
        if (f) { gst_object_unref(f); return candidates[i]; }
    }
    return "openh264enc"; // software fallback
}
```

The chosen encoder MUST be logged at startup so the user can see which path
was taken. Document the encoder priority list and fallback logic in the README
under `Conversion Notes`.

## 9. Source traceability comments (mandatory)

Every logically distinct section of the converted C++ code MUST include a
comment that traces it back to the corresponding code in the reference
application. Format:

```
/* --- Ref: <source_file>:<function_or_line_range> — <brief description> --- */
```

Examples:

- `/* --- Ref: deepstream_lpr_app.c:create_pipeline() L120-L145 — build primary detector (PGIE) --- */`
- `/* --- Ref: deepstream_lpr_app.c:osd_sink_pad_buffer_probe() L55-L90 — iterate batch meta, extract plate text --- */`
- `/* --- Ref: N/A — DL Streamer-specific: vapostproc for GPU→CPU transfer --- */` (for code with no reference counterpart)

This applies to: pipeline construction, inference element setup,
probe/callback functions, CLI option parsing, bus message handling, and any
helper functions. The goal is that a reader can open the converted code and
the reference code side-by-side and immediately see which parts correspond to
each other.

## 10. Pipeline construction — preserve all stages (no merging)

The 1-to-1 element-mapping rule is defined in
[`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md) Step 4 and is
non-negotiable: every source-app inference stage maps to a separate DL
Streamer inference element. Element availability and property names are
verified per §1; deny-list checks are driven from
[`deprecation-discovery.md`](./deprecation-discovery.md).
