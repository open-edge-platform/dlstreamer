# GstAnalytics-based classification history – plan

Scope: `gvaclassify` with `reclassify-interval > 1` (restoring classification results for frames
on which an object was not classified).

Starting point: branch `gst-analytics-classification-history`, commits
`7dacc317` (history based on the output frame counter) and `51baf23f` (per-instance probe).

## Goal

The source of the history should be GstAnalytics metadata (`GstAnalyticsRelationMeta`) instead of
legacy `GstStructure` params attached to `GstVideoRegionOfInterestMeta`. Legacy remains only as a
transitional output path for consumers which still need it (`gvametaconvert`).

Frame synchronization logic (output frame counter, updating the history right before
`gst_pad_push`, `frame_of_last_result`) stays unchanged – only **what** is stored in the history
and **how** it is restored changes.

## Summary of decisions

| Topic | Decision |
|---|---|
| History source | GstAnalytics metadata of this element, identified by Mtd ids passed from `TensorToROIAttacher` (section "Problem 1") |
| Storage | Per-object storage: an empty `GstBuffer` with `GstAnalyticsRelationMeta`, copying done with `copy_one_gst_analytics_mtd` (section 2) |
| Copy helpers | Moved from `gvametaaggregate` to `common/gva_analytics_utils.{h,c}`, separate commit (section 2a) |
| Stored types | `ClsMtd` and `TensorMtd` (raw tensors restored the same way as classification results) |
| Skipped types | Keypoints (groups, single keypoints), segmentation – including instance segmentation passed as `TensorMtd` (section 6) |
| Class descriptors | Out of scope, to be removed eventually (section 5) |
| `frames_ago` | Dropped – results restored from history are not marked in GstAnalytics; legacy – to be confirmed (section "Decisions", A) |
| Legacy | Separate, transitional path until `gvametaconvert` is migrated (section 3) |
| Frame synchronization | Unchanged compared to commits `7dacc317` / `51baf23f` |

---

## How it works now

### Data flow

```
TransformFrameIp (input thread)
  └─ IsROIClassificationNeeded()            -> decision: classify the object or not
  └─ SubmitImages() -> InferenceFrame{buffer, roi, roi_classifications = {}}

InferenceCompletionCallback (inference thread)
  └─ post_proc->process()
       └─ TensorToROIAttacher::attach()
            ├─ convert_to_meta()            -> GstAnalytics Mtd (ClsMtd / TensorMtd / keypoints group)
            │                                  + CONTAIN / IS_PART_OF relations with OD
            │                                  + RELATE_TO to class_descriptor
            │                                  !!! id of the created Mtd is lost
            ├─ add_param(roi_meta, s)        -> legacy GstStructure on ROI
            └─ roi_classifications.push_back(s)  -> the same GstStructure pointer
  └─ PushOutput()
       └─ UpdateClassificationHistory()
            └─ ClassificationHistory::UpdateROIParams(buffer, roi, roi_classifications)
                 ├─ gst_structure_copy(s)                  -> history source = legacy
                 └─ FindClassDescriptorLabels(od, s)       -> heuristic (see below)
       └─ PushBufferToSrcPad() -> probe -> ClassificationHistory::FillROIParams()
            ├─ AttachLegacyResult()   -> GstStructure copy + frames_ago on ROI
            └─ AttachResult()         -> GstAnalytics recreated from GstStructure via convert_to_meta()
```

Code references:

- [`TensorToROIAttacher::attach`](../src/monolithic/gst/inference_elements/common/post_processor/meta_attacher.cpp)
  – creates Mtd (`GstAnalyticsMtd tensor_mtd;` as a local variable) and fills
  `roi_classifications` with pointers to legacy `GstStructure`.
- [`InferenceFrame::roi_classifications`](../src/monolithic/gst/inference_elements/base/processor_types.h)
  – `std::vector<GstStructure *>`, the only channel through which results reach the history.
- [`UpdateClassificationHistory` / `PushOutput`](../src/monolithic/gst/inference_elements/base/inference_impl.cpp)
  – history update right before the frame is pushed.
- [`ClassificationHistory::UpdateROIParams`, `FillROIParams`, `FindClassDescriptorLabels`](../src/monolithic/gst/inference_elements/gvaclassify/classification_history.cpp).

### `TensorToROIAttacher` excerpt (current code)

```cpp
for (std::vector<GstStructure *> tensor_data : tensors_batch[i]) {
    GstAnalyticsMtd tensor_mtd;                       // local – the id is lost after the iteration
    GVA::Tensor gva_tensor(tensor_data[0]);
    ...
    if (gva_tensor.convert_to_meta(&tensor_mtd, od_meta.meta, od_x, od_y, od_w, od_h)) {
        set_relation(CONTAIN, od_meta.id, tensor_mtd.id);
        set_relation(IS_PART_OF, tensor_mtd.id, od_meta.id);
        if (gva_tensor.has_field("label_id") && ...)
            set_relation(RELATE_TO, tensor_mtd.id, cls_descriptor_mtd.id);
    }
}
...
for (std::vector<GstStructure *> tensor_data : tensors_batch[i]) {
    gst_video_region_of_interest_meta_add_param(roi_meta, tensor_data[0]);
    frames[i].roi_classifications->push_back(tensor_data[0]);   // legacy goes to the history
}
```

### How `class_descriptor` is found today (heuristic)

The history only receives `GstStructure`, so to preserve `label_id` in the restored `ClsMtd` it has
to "guess" which `ClsMtd` in the buffer corresponds to the structure:

```cpp
// FindClassDescriptorLabels(od_mtd, tensor)
label    = quark(tensor.label())
label_id = tensor.label_id
for each ClsMtd c in get_direct_related(od, CONTAIN, ClsMtd):
    if c.length == 1 && c.quark[0] == label:
        d = get_direct_related(c, RELATE_TO, ClsMtd)
        if d is "class_descriptor" && d.index_of(label) == label_id:
            return d.labels
```

That is: "the first `ClsMtd` of the object with the same label whose descriptor has this label at
the same index".

---

## Problem 1 – identifying results of this element in GstAnalytics

### What the problem is

For the history to read results from GstAnalytics, at update time (`PushOutput`, right before the
frame is pushed) it has to know **which Mtd related to the object were created by this
`gvaclassify` in this inference**.

An OD in `RelationMeta` may have many Mtd attached with a `CONTAIN` relation, coming from different
sources:

| Source | Example |
|---|---|
| detector | keypoints from a pose model (YOLO pose) attached to OD by `gvadetect` |
| an upstream `gvaclassify` | `ClsMtd` from another model (e.g. vehicle color before vehicle type) |
| an upstream `gvaclassify` – results from **its** history | restored `ClsMtd` look exactly like fresh ones |
| this `gvaclassify` | results of the current inference – **only these should go to the history** |
| descriptors | `ClsMtd` tagged `class_descriptor` (related with `RELATE_TO`, not `CONTAIN`) |

GstAnalytics does not store "which element created the Mtd". The only available properties are
the Mtd type, the semantic tag and relations.

With legacy the problem does not exist, because `TensorToROIAttacher` hands the history pointers to
**its own** `GstStructure` (`roi_classifications`) – identification comes "for free", but it only
covers legacy. The only place which already has to guess in GstAnalytics is the class descriptor
(`FindClassDescriptorLabels`).

### Why heuristics are unreliable

**Semantic tag heuristic** ("all `CONTAIN` Mtd of the object with a tag equal to `model_name`"):

- two elements with the same model on the same object (e.g. the same model in two branches,
  a shared `model-instance-id`, or an upstream `gvaclassify` with the same model and different
  parameters) produce identical tags – they cannot be distinguished,
- an upstream `gvaclassify` with history restores Mtd with the same tag – they would be taken as
  results of the current inference,
- the tag is empty if `model_name` is empty – there is nothing to search by,
- different Mtd types use different tag conventions (`model`, `model/format`,
  `model/instance_segmentation`), so the rule would have to know all of them.

**Current descriptor heuristic** (label + index in the descriptor):

- if an upstream element attached a `ClsMtd` with the same label to the object (e.g. "red" from
  two color models, "person" from two models) and its descriptor has this label at the same
  index, **someone else's** descriptor is returned – the restored `ClsMtd` gets related to the
  label list of another model, so a consumer reading the descriptor sees wrong labels,
- it only works for single-class `ClsMtd` (`length == 1`).

Failure scenario for the current descriptor heuristic:

```
gvaclassify model=color_A ! gvaclassify model=color_B reclassify-interval=5
```

1. `color_A` attaches `ClsMtd{"red"}` + descriptor `["red","green","blue"]` to the object.
2. `color_B` attaches `ClsMtd{"red"}` + descriptor `["red","white","black"]`.
3. `FindClassDescriptorLabels` for the `color_B` result iterates `CONTAIN` of the object and first
   hits the `ClsMtd` from `color_A`: label "red", index 0 – it matches.
4. Descriptor `["red","green","blue"]` is stored in the `color_B` history.
5. On frames restored from history, the `color_B` result points to the `color_A` descriptor.

### Proposed solution

Do not guess – pass **ids of the Mtd created by the attacher** to the history:

1. [`InferenceFrame`](../src/monolithic/gst/inference_elements/base/processor_types.h) gets a
   field, e.g. `std::vector<guint> roi_result_mtd_ids` (next to `roi_classifications`, which
   stays for the legacy path).
2. `TensorToROIAttacher::attach` stores `tensor_mtd.id` in this vector after a successful
   `convert_to_meta()` (a few lines).
3. `ClassificationHistory::UpdateROIParams` gets each Mtd by id:
   `gst_analytics_relation_meta_get_mtd(meta, id, GST_ANALYTICS_MTD_TYPE_ANY, &mtd)`
   and copies it to the history storage (see section 2). Class descriptors are not handled
   (see section 5), so the `FindClassDescriptorLabels` heuristic goes away as well.

The ids are used only to **identify** the results at update time – they are not stored, because
they are meaningless once the frame is pushed downstream.

Why the ids are safe:

- the attacher and `PushOutput` operate on the same buffer (`InferenceFrame::buffer`),
- if the buffer gets copied on the way (e.g. when it is made writable), the copy of
  `GstAnalyticsRelationMeta` preserves Mtd ids,
- the ids belong to a specific `InferenceFrame`, so multiple elements sharing an inference
  instance (`model-instance-id`) do not mix.

---

## Other parts of the plan

### 2. What the history stores – `RelationMeta` storage + `copy_one_gst_analytics_mtd`

GStreamer has no API to copy a single Mtd into the `RelationMeta` of another buffer
(`mtd_meta_transform` is meant for transforming metadata when the whole buffer is copied).
DLStreamer, however, already has a helper doing exactly that:
[`copy_one_gst_analytics_mtd`](../src/monolithic/gst/elements/gvametaaggregate/metaaggregate.c)
(used by `gvametaaggregate`). Instead of writing a separate snapshot per Mtd type, the history
uses this helper:

- each history entry (an object with a given tracking id) has its own "storage" – an empty
  `GstBuffer` with `GstAnalyticsRelationMeta` (`gst_buffer_new()` +
  `gst_buffer_add_analytics_relation_meta()`), held by a smart pointer with `gst_buffer_unref`
  (released together with the LRU entry),
- **update** (`PushOutput`, right before the frame is pushed): a new storage into which
  `copy_one_gst_analytics_mtd(storage_meta, &mtd, ...)` copies Mtd identified by ids
  (section "Problem 1"); the previous storage is replaced as a whole,
- **restore** (probe): for each Mtd from the storage
  `copy_one_gst_analytics_mtd(frame_meta, &stored_mtd, &new_mtd, 1.0, 1.0, NULL)` +
  `CONTAIN` / `IS_PART_OF` relations with the object's OD in the current frame,
- the storage is accessed only under `history_mutex`.

What the helper provides for free:

- copying `ClsMtd` (all classes + confidence levels) and `TensorMtd`,
- copying the semantic tag,
- `TensorMtd` – data is not copied, only a reference to the tensor data `GstBuffer` is taken
  (cheap),
- a single Mtd copying mechanism in the project instead of two.

What the helper does not do and why it does not matter:

- it does not copy relations (only `copy_all_gst_analytics_mtd` does) – the history sets
  relations with OD on its own anyway, and relations to class descriptors are not restored
  (section 5),
- it only scales, without translation – irrelevant for `ClsMtd` and `TensorMtd`, which have no
  coordinates; it would only matter for keypoints and segmentation, which are out of scope
  (section 6),
- when copying a group it preserves membership (`gst_analytics_group_mtd_add_member`), but not
  relations between members (e.g. keypoint skeleton) – a TODO has been added in the code, and
  groups do not go to the history anyway.

Type filter applied when updating the history (before copying to the storage):

- `ClsMtd` – stored,
- `TensorMtd` – stored, **unless** it is an instance segmentation mask: the semantic tag contains
  `instance_segmentation` (`model_name/instance_segmentation`, the same rule used by
  `Tensor::convert_to_tensor()` in `tensor.h`),
- other types (keypoints group, keypoint, `SegmentationMtd`, …) – skipped.

Skipping is reported once with `GST_INFO` (e.g. "keypoints/segmentation results are not restored
from classification history"), to make it clear why these results are missing on frames restored
from history.

### 2a. Moving the helpers to a separate file

`copy_one_gst_analytics_mtd` and `copy_all_gst_analytics_mtd` currently live in the `elements`
library (`gvametaaggregate`), which `inference_elements` does not link. Both libraries, however,
link the [`common`](../src/monolithic/gst/common/) library, which already uses GstAnalytics
(`get_od_id` / `set_od_id` in `gva_utils.cpp`).

Proposal:

- a new file `src/monolithic/gst/common/gva_analytics_utils.{h,c}` (C, header wrapped in
  `G_BEGIN_DECLS` / `G_END_DECLS`, so it is usable from C++ as well),
- move `copy_one_gst_analytics_mtd` and `copy_all_gst_analytics_mtd` there without changing
  their behavior,
- remove their declarations from
  [`metaaggregate.h`](../src/monolithic/gst/elements/gvametaaggregate/metaaggregate.h) and include
  the new header in `metaaggregate.c`,
- `common/CMakeLists.txt` collects sources with a glob (`*.c`, `*.cpp`), so the new file gets
  into the build without CMake changes; to check whether `common` has GstAnalytics include/link
  directly or only through `gstvideoanalyticsmeta` (today it works through the latter),
- a separate commit (pure refactor) before the history change; verified with the existing tests
  `tests/unit_tests/check/components/metaaggregate_copy` (they test exactly these copy functions)
  and `tests/unit_tests/check/pipelines/metaaggregate_test`; the `metaaggregate_copy` test declares
  the functions itself (`extern "C"`) and links the `elements` library – to be changed to include
  the new header and link `common`,
- the functions are moved together with the comments describing how they work (already added in
  `metaaggregate.c`), including the TODO in `copy_one_gst_analytics_mtd` about restoring
  relations between group members – a topic for the future, not part of this change.

### 3. Legacy

Legacy cannot currently be generated from GstAnalytics without losing information:

- `ClsMtd` does not store the attribute name (structure name: "age", "gender") nor `layer_name`,
- the semantic tag is just `model_name` – shared by all outputs of the model,
- `gvametaconvert` uses the structure name as the JSON key – restoring via
  `Tensor::convert_to_tensor()` would produce the key "classification" without `model.name`.

Therefore:

- **now** – a separate, clearly marked legacy path: a copy of `GstStructure` from
  `roi_classifications` used only to restore legacy params; the GstAnalytics part does not use
  it; the legacy path does not filter types (same behavior as before, in legacy keypoint
  positions are relative to the ROI),
- **later (separate change)** – store the attribute name in GstAnalytics (e.g. semantic tag
  `model_name/attribute`, similar to keypoints `model/format`) and migrate `jsonconverter`;
  after that the legacy path in the history can be removed.

### 4. Scope of changes

| File | Change |
|---|---|
| `inference_elements/base/processor_types.h` | Mtd ids field in `InferenceFrame` |
| `inference_elements/common/post_processor/meta_attacher.cpp` | `TensorToROIAttacher` stores ids of created Mtd |
| `common/gva_analytics_utils.{h,c}` (new) | moved `copy_one_gst_analytics_mtd` / `copy_all_gst_analytics_mtd` |
| `elements/gvametaaggregate/metaaggregate.{h,c}` | removal of moved functions, include of the new header |
| `inference_elements/gvaclassify/classification_history.{h,cpp}` | per-object `RelationMeta` storage, capture by id with `copy_one`, restore with `copy_one` + relations with OD, legacy as a separate block, removal of `FindClassDescriptorLabels` and `FindOrAddClassDescriptor` |
| `inference_elements/base/inference_impl.cpp` | passing ids to the history |
| `tests/.../classification_history_tests.cpp` | results built as Mtd (`ClsMtd`, `TensorMtd`), test that only Mtd of this element go to the history (an upstream `ClsMtd` on the same object is ignored), test that keypoints and instance segmentation masks are not restored |

Estimate: helpers refactor – moving code without changes; history – smaller than the per-type
snapshot variant (about 150–200 lines including tests).

### 5. Class descriptors – out of scope

Class descriptors are going to be removed eventually, so the history does not handle them:

- on update they are neither read nor copied to the storage (the helper copies only Mtd
  identified by ids anyway, and a descriptor is not a result of `convert_to_meta`),
- on restore no `RELATE_TO` relation to a descriptor is created,
- `FindClassDescriptorLabels`, `FindOrAddClassDescriptor`, `IsClassDescriptor` and the
  `ClassificationResult::class_descriptor` field are removed.

Accepted consequence: on frames restored from history `ClsMtd` has no relation to a descriptor,
so reading it via `Tensor::convert_to_tensor()` / `RegionOfInterest` does not provide `label_id`
(unchanged on classified frames). Legacy params restored from history still contain `label_id`,
so JSON from `gvametaconvert` does not change. The descriptor failure scenario from section
"Problem 1" becomes irrelevant; the problem of identifying the results themselves remains and is
solved by ids.

### 6. Keypoints and segmentation – out of scope

The history neither stores nor restores keypoints or segmentation, including instance
segmentation passed as `TensorMtd`. Reasons:

- the result is tied to the object position on the frame on which it was classified; a copy
  without recalculating coordinates (keypoints, mask location) stays in the old place when the
  object moves,
- without `frames_ago` a consumer has no way to tell that the result is outdated – it is better
  for it to be absent than silently wrong,
- `copy_one_gst_analytics_mtd` does not restore relations between group members (skeleton),
- it is not supported nor used today, and supporting it would require recalculating coordinates
  relative to the current bbox – the effort is not worth the benefit.

Thanks to that there is no need to extend `copy_one_gst_analytics_mtd` with coordinate
translation or with copying relations within groups. If needed in the future, the decision can be
changed without breaking behavior.

### 7. Order of work

1. Commit: move `copy_one_gst_analytics_mtd` / `copy_all_gst_analytics_mtd` to
   `common/gva_analytics_utils.{h,c}` (together with comments and the TODO), update
   `gvametaaggregate` and the `metaaggregate_copy` test; verify with `metaaggregate_*` tests.
2. Commit: pass Mtd ids from `TensorToROIAttacher`, history based on `RelationMeta` storage +
   `copy_one`, type filter, removal of class descriptor handling, separate legacy path; history
   unit tests and an end-to-end test (`gvadetect ! gvatrack ! gvaclassify reclassify-interval=N !
   gvametaconvert`) as for the previous commits.

---

## Decisions

### A. Marking results restored from history – decided

`frames_ago` is dropped. Results restored from history are not marked in GstAnalytics in any way
(no dedicated Mtd, no semantic tag changes).

To be confirmed: whether to remove `frames_ago` from the legacy path as well. Today the restored
legacy structure gets this field, but nothing in the repository reads it (`gvametaconvert` does
not output it – `convert_tensor` serializes only selected fields), so removing it does not change
pipeline output; it may only be used by external consumers (e.g. custom `gvapython` scripts).
Until confirmed: legacy unchanged.

### B. Scope of types – decided

`ClsMtd` + `TensorMtd` (excluding instance segmentation masks). Keypoints and segmentation out of
scope (section 6), class descriptors out of scope (section 5).

### C. Restoring raw tensors (`TensorMtd`) – decided

Raw tensors are stored and restored the same way as `ClsMtd` (consistent with legacy, which
copied all structures). Exception: `TensorMtd` carrying an instance segmentation mask (section 6).
