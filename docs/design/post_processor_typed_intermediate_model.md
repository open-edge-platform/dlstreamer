# Design plan: typed intermediate model for the inference post-processor

Status: **Planned** (not started).
Owner: DL Streamer post-processing.
Prerequisite work that must land first:
1. Semantic + instance **segmentation → GstAnalytics** migration is merged.
2. **Full `GstAnalyticsTensorMtd` support** (generic raw tensors, round-trippable) is in place.

This document is a durable, self-contained plan. It captures the motivation,
the exact current data flow, the shape of the proposed typed intermediate
model, a phased migration, and every code location that must change. Come back
to this file to execute the plan.

---

## 1. Motivation / the problem

Today the metadata flow inside `gvadetect` / `gvaclassify` / `gvainference`
post-processing is:

```
model output blobs
   → converter        (produces GstStructure tensors)
   → GstStructure     ← intermediate carrier (TensorsTable)
   → coordinates restorer (mutates GstStructure in place)
   → meta attacher    (GstStructure → GstAnalytics*Mtd  via Tensor::convert_to_meta)
   → GstAnalyticsRelationMeta on the buffer
   → consumers        (GstAnalytics*Mtd → GstStructure via Tensor::convert_to_tensor)
```

We serialize **three times**: to `GstStructure`, then to `GstAnalytics`, then
back to `GstStructure` on read. The `GstStructure` intermediate is:

- untyped (string-keyed map), error-prone, and slow (repeated copies);
- the reason `Tensor::convert_to_meta` / `Tensor::convert_to_tensor` exist and
  have to encode/decode semantic tags, formats, dims, etc.;
- a maintenance burden (every new metadata kind needs bespoke field plumbing).

**Goal:** eliminate the `GstStructure` intermediate and the
`convert_to_meta` / `convert_to_tensor` pair from the hot path, replacing them
with a **typed C++ intermediate model** that is serialized to `GstAnalytics`
exactly once (at the attacher) and read directly from `GstAnalytics` by
consumers.

> Key insight discovered during analysis: the pain is the **`GstStructure`
> carrier**, not the two-stage (convert → attach) structure. The stages are
> *forced* by (a) needing frame/buffer context to place absolute coordinates,
> (b) NMS needing to run before anything is written to immutable analytics meta.
> So we keep the logical stages but change the carrier type and serialize once.

---

## 2. Exact current data flow (with code anchors)

### 2.1 Types

- `TensorsTable` = `std::vector<std::vector<std::vector<GstStructure *>>>`
  → indexed `[frame][detection][detection_tensor + secondary tensors]`.
  Defined in `src/monolithic/gst/inference_elements/common/post_processor/post_proc_common.h`
  (`DETECTION_TENSOR_ID = 0`).
- Detection converters already use a **plain C++ struct** `DetectedObject`
  (double `x/y/w/h/r/confidence`, `label_id`, `label`, and
  `std::vector<GstStructure *> tensors` for secondary results). Defined in
  `converters/to_roi/blob_to_roi_converter.h`.

### 2.2 Orchestration

`ConverterFacade::convert()` in
`converters/.../converter_facade.cpp`:

```cpp
tensors_batch = blob_to_meta->convert(...);                 // stage 1
if (frames.need_coordinate_restore() && coordinates_restorer)
    coordinates_restorer->restore(tensors_batch, frames);   // stage 2
meta_attacher->attach(tensors_batch, frames, *blob_to_meta);// stage 3
```

### 2.3 Stage 1 — converters (blob → GstStructure)

- ROI/detection path: `BlobToROIConverter` subclasses build `DetectedObject`s,
  then `storeObjects()` runs **NMS on the structs** (`runNms`, works on
  `DetectedObject` with `candidates.erase(...)`), drops invalid detections,
  validates keypoints, and finally `toTensorsTable()` → `DetectedObject::toTensor()`
  creates the `GstStructure` ("detection" with `x_min/x_max/y_min/y_max/...`).
  File: `converters/to_roi/blob_to_roi_converter.cpp` and `.h`.
- Tensor path (`gvaclassify`, keypoints, segmentation second stage):
  `BlobToTensorConverter` subclasses build `GstStructure` tensors directly via
  `createTensor()`. File: `converters/to_tensor/blob_to_tensor_converter.{h,cpp}`
  and the individual converters (`semantic_segmentation.cpp`, keypoints_*.cpp, etc.).

**Important:** for detections, `GstStructure` only appears at the very end
(`toTensor`). NMS and box validity run on `DetectedObject`. Only **secondary**
tensors (keypoints, instance masks) are already `GstStructure` at this point.

### 2.4 Stage 2 — coordinates restorer (mutates GstStructure)

File: `converters/.../coordinates_restorer.{h,cpp}`.

- `ROICoordinatesRestorer::restore`: reads `x_min/x_max/y_min/y_max` from the
  detection `GstStructure`, applies preprocessing transform
  (`frame.image_transform_info`: padding/crop/resize via
  `restoreActualCoordinates`), converts ROI-relative → full-frame
  (`updateCoordinatesToFullFrame`, which **already reads the parent
  `GstAnalyticsODMtd`** from the buffer), clips, computes absolute pixels,
  and writes back `x_abs/y_abs/w_abs/h_abs` into the `GstStructure`.
- `KeypointsCoordinatesRestorer::restore`: reads the keypoint `GstStructure`
  float array, restores per-point coordinates, writes back via
  `copy_buffer_to_structure`.
- Needs: `ModelImageInputInfo input_info`, `FrameWrapper` (transform info,
  ROI, width/height, `meta_mutex`).

### 2.5 Stage 3 — meta attacher (GstStructure → GstAnalytics)

File: `converters/.../meta_attacher.{h,cpp}`. Variants chosen by
`ConverterType` × `AttachType`:

- `ROIToFrameAttacher` (TO_ROI): reads `x_abs/label/confidence/rotation`,
  makes buffer writable, takes `meta_mutex`, adds
  `GstAnalyticsRelationMeta`, optionally builds a **class-descriptor** ClsMtd
  (tagged `"class_descriptor"`) from `blob_to_meta.getLabels()`, adds
  `GstAnalyticsODMtd` (`add_oriented_od_mtd`), sets OD semantic tag =
  model name, then for each secondary tensor calls
  `GVA::Tensor::convert_to_meta(...)` and wires `CONTAIN` / `IS_PART_OF`
  relations. It **also dual-writes legacy**:
  `gst_buffer_add_video_region_of_interest_meta` + `add_param` and (in tensor
  attachers) `GstGVATensorMeta`.
- `TensorToFrameAttacher` (RAW/TO_TENSOR, TO_FRAME): frame-level; calls
  `convert_to_meta` then adds legacy `GstGVATensorMeta` (`tensor->data =
  tensor_data[0]`).
- `TensorToROIAttacher` (TO_ROI second stage, e.g. classify over roi-list):
  finds the existing `GstAnalyticsODMtd` for the frame ROI, builds/【reuses】
  class descriptor, `convert_to_meta` for each tensor, wires relations, and
  dual-writes ROI params + pushes into `frame.roi_classifications`.
- `FrameToExistingROIsTensorAttacher`, `TensorToFrameAttacherForMicro`:
  variants of the above for existing ROIs / micro elements.

### 2.6 Read-back — consumers (GstAnalytics → GstStructure)

`GVA::Tensor::convert_to_tensor(mtd, ...)` reconstructs a `GstStructure` and is
called from:

- `include/dlstreamer/gst/videoanalytics/region_of_interest.h` (`get_tensors`);
- `include/dlstreamer/gst/videoanalytics/video_frame.h` (frame-level + ROI);
- `python/gstgva/tensor.py` (`convert_to_tensor`) and
  `python/gstgva/region_of_interest.py`.

Consumers of the reconstructed `GstStructure`: `gvametaconvert`
(`jsonconverter.cpp`), `gvawatermark`, `gvametaaggregate`, custom converters
(`custom_to_roi.cpp`, `custom_to_tensor.cpp`), Python callbacks.

---

## 3. Hard constraints (why we cannot "just emit analytics in the converter")

1. **No buffer/frame context in converters.** `GstAnalyticsMtd` must live in a
   `GstAnalyticsRelationMeta` on a specific writable `GstBuffer`. Converters run
   on a **batch** of `OutputBlobs` decoupled from frames; batch→frame mapping,
   `make_writable`, and `meta_mutex` are done only in the attacher.
2. **NMS has no "remove mtd" API.** `GstAnalyticsRelationMeta` exposes no public
   way to delete an individual mtd. NMS must therefore run on a mutable
   candidate list **before** serialization. (Today it runs on `DetectedObject`.)
3. **Absolute coordinates need frame context.** `GstAnalyticsODMtd` stores
   integer absolute pixels. Converters only know model-input/ROI-relative
   normalized coords. The transform (preprocessing params + parent ROI) lives in
   the coordinates restorer for a reason.
4. **Legacy dual-write** (`GstVideoRegionOfInterestMeta` params +
   `GstGVATensorMeta`) must stay until all legacy consumers are migrated.

**Conclusion:** keep the pipeline *stages*, replace the *carrier*.

---

## 4. Proposed typed intermediate model

Replace `TensorsTable = vector<vector<vector<GstStructure*>>>` with a typed tree.
New header (proposed): `converters/.../inference_result.h` (namespace
`post_processing`). Names are indicative.

```cpp
namespace post_processing {

enum class Precision { UNSPECIFIED, FP32, FP16, I32, I64, U8, /* ... */ };

// Generic tensor payload. Two alternative representations are on the table
// (see the "TensorResult payload" note below); the code shows Alternative B.

// Owning raw byte buffer + element precision, with type-checked typed access.
// Storage stays raw (a single memcpy serializes to GstTensor), but reads go
// through as<T>(), which verifies T matches `precision` (throws/asserts on
// mismatch). This keeps the payload precision-agnostic yet type-safe on access.
class TensorData {
  public:
    Precision            precision = Precision::UNSPECIFIED;
    std::vector<uint8_t> bytes;            // raw payload (owning)

    template <typename T>
    std::span<const T> as() const;        // checks sizeof(T)/precision, else throws
    template <typename T>
    void set(std::span<const T> values);  // sets bytes + precision from T

    std::size_t byte_size() const { return bytes.size(); }
};

// A generic raw tensor payload (maps to GstAnalyticsTensorMtd).
struct TensorResult {
    std::string              semantic_tag; // sole provenance carrier
    std::string              tensor_id;    // optional GstAnalyticsTensorMtd id (GQuark source)
    std::vector<std::size_t> dims;         // row-major (matches GstTensor dims)
    TensorData               data;         // precision + bytes, typed access via as<T>()
};

// Classification result (maps to GstAnalyticsClsMtd).
struct ClassificationResult {
    std::string            semantic_tag;   // sole provenance carrier
    std::string            label;          // may be multi-token
    double                 confidence = 0.0;
    std::optional<int>     label_id;       // when a class descriptor exists
    // NOTE: no nested raw tensor here — GstAnalyticsClsMtd carries only
    // label + confidence (+ semantic tag / class-descriptor relation). If a
    // converter also emits the raw logits blob (e.g. gvainference, or explicit
    // raw-tensor attachment), it is a *sibling* TensorResult in the same
    // `results` vector, serialized as its own GstAnalyticsTensorMtd — never
    // nested inside the ClsMtd.
};

// Keypoints group (maps to GstAnalyticsGroupMtd + KeypointMtd + skeleton relations).
struct KeypointsResult {
    std::string            semantic_tag;   // e.g. "model/body-pose/coco-17"
    std::string            format;         // descriptor key, e.g. "body-pose/coco-17"
    uint32_t               point_count = 0;
    uint32_t               point_dim = 2;  // 2 or 3
    std::vector<float>     positions;      // normalized [x,y,(z)] * point_count
    std::vector<float>     confidences;    // per point (or shared)
    std::vector<std::pair<uint32_t,uint32_t>> skeleton; // edges
    // NOTE: `format` is kept (needed for descriptor / skeleton / point-name
    // lookup). The serializer composes the final tag by combining the tag with
    // `format` exactly as convert_to_meta does today ("<tag>/<format>").
    // NOTE: `skeleton` priority — a converter may define edges explicitly here,
    // in which case they take priority over the descriptor's skeleton. If
    // `skeleton` is empty, the serializer falls back to the skeleton from the
    // keypoint descriptor resolved via `format`. If there is no descriptor for
    // `format` either, the group is attached without any skeleton edges.
};

// Semantic segmentation (maps to GstAnalyticsSegmentationMtd).
struct SemanticSegmentationResult {
    std::string            semantic_tag;   // sole provenance carrier
    uint32_t               width = 0, height = 0;
    std::vector<int64_t>   class_map;      // H*W class ids
};

// Instance segmentation soft mask (maps to GstAnalyticsTensorMtd, tagged).
struct InstanceMaskResult {
    std::string            semantic_tag;   // full tag, incl. "/instance_segmentation" suffix
    uint32_t               width = 0, height = 0;
    std::vector<float>     mask;           // FP32 soft probabilities [W*H]
};

// A secondary result attached to a detection or frame/ROI.
using SecondaryResult = std::variant<
    ClassificationResult, KeypointsResult,
    SemanticSegmentationResult, InstanceMaskResult, TensorResult>;

// A detection (maps to GstAnalyticsODMtd). Coordinates evolve across stages:
//  - after converter:  normalized, model-input/ROI relative (norm.*)
//  - after coord restore: absolute pixels (abs.*) filled in
struct DetectionResult {
    // geometry
    double   nx = 0, ny = 0, nw = 0, nh = 0;    // normalized (converter output)
    double   rotation = 0.0;
    uint32_t ax = 0, ay = 0, aw = 0, ah = 0;    // absolute px (coord restorer)
    bool     has_absolute = false;

    // classification
    std::string  semantic_tag;                  // provenance for the OD mtd
    size_t       label_id = 0;
    std::string  label;
    double       confidence = 0.0;

    // attached results
    std::vector<SecondaryResult> results;
};

// Frame- or ROI-level results with no parent detection.
struct FrameResults {
    std::vector<DetectionResult>  detections;   // TO_ROI converters
    std::vector<SecondaryResult>  frame_level;  // TO_TENSOR / full-frame
};

// Replaces TensorsTable. Index = frame/ROI in the batch.
using InferenceResults = std::vector<FrameResults>;

} // namespace post_processing
```

Notes:
- **`semantic_tag` is the single provenance carrier.** There is intentionally no
  `model_name` and no `layer_name` field. The converter composes the final tag
  once (model name, plus format/`instance_segmentation` suffix where applicable)
  and everything downstream — serializer, relations, read path — uses only
  `semantic_tag`. This removes the model_name/format string juggling that
  `convert_to_meta` does today.
- **`TensorResult` payload — two alternatives (decide before Phase 1).** The
  generic tensor payload is the one place where "raw" is hard to avoid, because
  it mirrors `GstAnalyticsTensorMtd` / `GstTensor` (a byte buffer + a
  `GstTensorDataType`). Two representations are recorded:
  - **Alternative A — plain raw bytes.** Fields on `TensorResult` directly:
    `Precision precision;`, `std::vector<uint8_t> data;`. Simplest; serialization
    is a single `memcpy`; one field covers every precision. Downside: element
    access needs `reinterpret_cast`, losing the type-safety we are after.
  - **Alternative B — typed-accessor wrapper (`TensorData`, shown above).** Same
    raw storage (still one `memcpy` to `GstTensor`, still precision-agnostic),
    but reads go through `as<T>()` which validates `T` against `precision`. Keeps
    the payload raw at the boundary while making element access type-checked.
  - **Recommendation:** Alternative B — same serialization cost, but type-safe
    reads. Alternative A remains acceptable if we want to keep the struct a plain
    aggregate with zero helper code.
  Either way, kind-specific structs that are inherently single-typed
  (`SemanticSegmentationResult::class_map` = `int64`, `InstanceMaskResult::mask`
  = `float`) use plain typed vectors and do not need this abstraction.
  - **Alternative C — hold a `GstTensor` directly (considered, not recommended
    as the model type).** Since `TensorResult` mirrors `GstAnalyticsTensorMtd`
    1:1, it is tempting to store a `GstTensor` (it already carries `id`,
    `data_type`, `dims`, `dims_order`, and a refcounted `GstBuffer` payload),
    giving near zero-copy serialization and no re-modelling. Rejected as the
    model type because it defeats the core goal of this refactor:
    - it drags GObject / `GstBuffer` / manual refcounting back into a model whose
      whole point is to be GStreamer-free, plain value-semantics and unit-testable
      without `gst_init` — the same class of problem as `GstStructure`;
    - it forces `GstBuffer` allocation already in the converter (heavier than a
      `std::vector`, reintroduces lifetime/ref management);
    - it is awkward for the intermediate stages (NMS, coordinate restore, keypoint
      tweaks), which would need to map/unmap buffers to touch floats;
    - it is not universal anyway — only `TensorResult` maps to `TensorMtd`; the
      other kinds map to `ODMtd`/`ClsMtd`/`GroupMtd`/`SegmentationMtd`, so
      `GstTensor` could never be the model's common payload.
    **Preferred instead:** keep the model GStreamer-free (A/B) and build the
    `GstTensor` only in the serializer. If profiling later shows that copying
    large raw tensors (e.g. depth maps) hurts, `TensorData` may *optionally* hold
    a shared-buffer handle (a thin RAII wrapper over `GstBuffer` /
    `shared_ptr<bytes>`) for zero-copy — as an opt-in optimization, not the
    default.
- `std::variant` keeps the model closed and exhaustive (compiler-checked
  serialization switch). A polymorphic base class is an alternative if we expect
  many out-of-tree kinds.
- `DetectedObject` (in `blob_to_roi_converter.h`) collapses into
  `DetectionResult` — its `tensors` vector becomes `results`.
- Ownership is value semantics (`std::vector<uint8_t>` etc.), removing manual
  `gst_structure_free` bookkeeping and the NMS free-loop.

---

## 5. Typed model → GstAnalytics mapping (single serialization point)

Serialization lives **only** in the attacher (or a new `AnalyticsSerializer`
helper it delegates to). One function per variant alternative:

| Typed struct | GstAnalytics call | Notes |
|---|---|---|
| `DetectionResult` | `gst_analytics_relation_meta_add_oriented_od_mtd` | uses `a*` absolute px + `rotation`; OD semantic tag = `semantic_tag` |
| `ClassificationResult` | `gst_analytics_relation_meta_add_one_cls_mtd` | tag = `semantic_tag`; wire `RELATE_TO` class-descriptor when `label_id` set |
| `KeypointsResult` | `gst_analytics_relation_meta_add_keypoints_group` | positions scaled to ref rect; skeleton → `RELATE_TO` edges; tag composed as `<semantic_tag>/<format>` (as today) |
| `SemanticSegmentationResult` | `gst_analytics_relation_meta_add_segmentation_mtd` | GRAY8/GRAY16_LE mask buffer + region ids; tag = `semantic_tag` |
| `InstanceMaskResult` | `gst_analytics_relation_meta_add_tensor_mtd_simple` | FP32 `[H,W]`; tag = `semantic_tag` (already ends with `/instance_segmentation`) |
| `TensorResult` | `gst_analytics_relation_meta_add_tensor_mtd_simple` | generic; precision→`GstTensorDataType`; tag = `semantic_tag`; mtd id from `tensor_id` |

Relations wired centrally: `OD ─CONTAIN→ result`, `result ─IS_PART_OF→ OD`;
frame-level results have no parent. This is exactly what
`Tensor::convert_to_meta` does today — the logic moves here, but reads typed
fields instead of `GstStructure` fields, so **no format/semantic-tag string
guessing is needed**.

This section supersedes `Tensor::convert_to_meta`. The bodies of the current
`convert_to_meta` branches (segmentation mask packing, keypoint scaling,
skeleton relation creation) are the reference implementation to port.

---

## 6. Stage-by-stage change plan

### 6.1 Converters (stage 1)
- Change every `convert()` to return `InferenceResults` (or `FrameResults`)
  instead of `TensorsTable`.
- ROI converters: `DetectedObject` → `DetectionResult`; `runNms` stays (now on
  `DetectionResult`); drop `toTensor()` / `toTensorsTable()` (no GstStructure).
- Tensor converters: build the appropriate typed `SecondaryResult` instead of
  `GstStructure` via `createTensor()`.
- Custom converters (`custom_to_roi.cpp`, `custom_to_tensor.cpp`) currently take
  a `GstStructure`/`GstTensorMeta` ABI across `dlopen`. **Keep the C ABI**: add
  a thin adapter that converts the custom lib's `GstStructure`/`GstTensor`
  output into the typed model at the boundary. (Do not break the plugin ABI.)

### 6.2 NMS
- Move `runNms` to operate on `std::vector<DetectionResult>`. Logic identical
  (sort by confidence, IoU suppress, `erase`). No mtd-removal problem because it
  still runs before serialization. Free-loop over `p_candidate->tensors`
  disappears (value semantics).

### 6.3 Coordinates restorer (stage 2)
- Operate on `InferenceResults` in place.
- `ROICoordinatesRestorer`: read `nx/ny/nw/nh`, apply transform + parent OD
  lookup (unchanged; already reads `GstAnalyticsODMtd`), write `ax/ay/aw/ah`,
  set `has_absolute = true`. Remove all `gst_structure_get/set`.
- `KeypointsCoordinatesRestorer`: mutate `KeypointsResult::positions` directly.
- Keep `restoreActualCoordinates`, `updateCoordinatesToFullFrame`,
  `clipNormalizedRect`, `getAbsoluteCoordinates` — only their I/O changes.

### 6.4 Meta attacher (stage 3)
- Rewrite the five attachers to consume `InferenceResults` and serialize via the
  §5 mapping. Relation wiring and class-descriptor creation stay.
- **Legacy dual-write:** keep for now, but source it from the typed model
  (build the legacy `GstStructure` / ROI params from typed fields at the very
  end). Gate behind a flag so it can be removed in the final phase.

### 6.5 Consumers / read path
- Add typed read helpers on `GVA::VideoFrame` / `GVA::RegionOfInterest` that
  iterate `GstAnalytics*Mtd` directly (no `convert_to_tensor`).
- Migrate `jsonconverter.cpp`, `gvawatermark`, `gvametaaggregate` to the typed
  readers.
- Python: expose typed accessors over GI; keep `convert_to_tensor` as a
  deprecated shim during transition.

---

## 7. Phased migration (each phase compiles + passes tests)

- **Phase 0 (prereq):** segmentation→analytics merged; full `GstAnalyticsTensorMtd`
  support (generic raw tensor round-trip) landed and tested.
- **Phase 1:** introduce `inference_result.h` typed model + an
  `AnalyticsSerializer` that maps typed → analytics (ported from
  `convert_to_meta`). Unit-test the serializer in isolation. No pipeline change
  yet.
- **Phase 2:** convert **one** vertical slice end-to-end (recommend
  **keypoints**, or **semantic segmentation** since it is self-contained and
  lossless): converter emits typed, coord restore typed, attacher serializes
  typed. Keep everything else on the old path via an adapter
  (`TensorsTable` ⇆ typed) so the two can coexist.
- **Phase 3:** migrate detection/ROI path (`DetectedObject` → `DetectionResult`,
  NMS move). This is the biggest change.
- **Phase 4:** migrate remaining tensor converters (classification, anomaly,
  custom via ABI adapter).
- **Phase 5:** migrate consumers to typed readers; make `convert_to_tensor` a
  thin deprecated shim.
- **Phase 6:** remove `convert_to_meta` / `convert_to_tensor` from the hot path;
  drop `TensorsTable`/`GstStructure` intermediate; retire legacy dual-write once
  no consumer needs `GstGVATensorMeta` / ROI params.

Provide `TensorsTable ⇆ InferenceResults` adapters during Phases 2–4 so slices
migrate independently.

---

## 8. Important code locations (change map)

| Concern | File |
|---|---|
| Intermediate type `TensorsTable`, `DETECTION_TENSOR_ID` | `src/monolithic/gst/inference_elements/common/post_processor/post_proc_common.h` |
| Orchestration (3 stages) | `.../post_processor/converter_facade.cpp` (`ConverterFacade::convert`) |
| Detection struct + NMS + toTensor | `.../post_processor/converters/to_roi/blob_to_roi_converter.{h,cpp}` |
| ROI converters (yolo, rtdetr, mask_rcnn, centerface, detection_output, boxes_*) | `.../post_processor/converters/to_roi/*.cpp` |
| Tensor converter base | `.../post_processor/converters/to_tensor/blob_to_tensor_converter.{h,cpp}` |
| Tensor converters (segmentation, keypoints_*, anomaly, cls) | `.../post_processor/converters/to_tensor/*.cpp` |
| Coordinates restorer | `.../post_processor/coordinates_restorer.{h,cpp}` |
| Meta attacher (5 variants) | `.../post_processor/meta_attacher.{h,cpp}` |
| Typed→analytics logic to port | `include/dlstreamer/gst/videoanalytics/tensor.h` (`convert_to_meta`) |
| Read-back to port | `include/dlstreamer/gst/videoanalytics/tensor.h` (`convert_to_tensor`), `.../region_of_interest.h`, `.../video_frame.h` |
| Custom converter ABI (keep) | `.../to_roi/custom_to_roi.cpp`, `.../to_tensor/custom_to_tensor.cpp` |
| Consumers | `.../gvametaconvert/jsonconverter.cpp`, `gvawatermark/*`, `gvametaaggregate/metaaggregate.c` |
| Python bindings | `python/gstgva/tensor.py`, `python/gstgva/region_of_interest.py` |

---

## 9. Testing strategy

- **Serializer unit tests** (Phase 1): typed struct → analytics mtd, asserting
  the same properties current `SegmentationConvertToMetaTest` /
  `SemanticTagTest` / keypoint tests assert. Reuse
  `tests/unit_tests/check/components/gstvideoanalyticsmeta/tensor_convert_test.cpp`
  fixtures as a reference.
- **Round-trip invariant:** for each kind, `typed → analytics → typed` must be
  lossless (mirrors the existing meta→tensor→meta round-trip idea). Segmentation
  is the cleanest lossless case; keypoints are lossy on positions (int pixel
  quantization) — assert with tolerance as today.
- **Golden pipeline outputs:** compare `gvametaconvert` JSON before/after per
  slice to guarantee no observable change for consumers.
- **NMS parity:** unit-test `runNms` on `DetectionResult` against the current
  `DetectedObject` behavior with identical inputs.
- **metaaggregate copy tests** already validate segmentation/tensor mtd copying;
  keep green.

---

## 10. Risks / open questions

- **Custom-converter ABI**: must remain `GstStructure`/`GstTensor`-based across
  `dlopen`. Adapter at the boundary is mandatory; do not change the plugin
  contract.
- **`std::variant` vs polymorphism**: variant is exhaustive and fast but closed;
  if third parties need new kinds in-tree only, that is fine. Decide before
  Phase 1.
- **Legacy consumers**: audit who still reads `GstGVATensorMeta` / ROI
  `GstStructure` params before Phase 6 (watermark, metaconvert, python apps,
  `gvametaaggregate`).
- **Coordinate stage placement**: absolute-coordinate computation still needs
  frame/transform context, so it stays a distinct pass. The typed model must
  carry both normalized and absolute geometry across the pass boundary.
- **Threading**: only the attacher touches the buffer/`meta_mutex`; keep it that
  way. Converters and coord restore stay buffer-free (except the existing parent
  OD lookup, which already locks).

---

## 11. Definition of done

- No `GstStructure` intermediate inside post-processing; `TensorsTable` removed.
- `Tensor::convert_to_meta` / `Tensor::convert_to_tensor` no longer on the hot
  path (kept only as deprecated shims, or deleted).
- Single serialization to `GstAnalytics` at the attacher.
- Consumers read `GstAnalytics` directly (typed readers).
- Legacy dual-write removed or gated off by default.
- All existing analytics/segmentation/keypoint/JSON tests green; new serializer
  + round-trip tests added.
