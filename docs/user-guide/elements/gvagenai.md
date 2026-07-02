# gvagenai

Performs inference with Vision Language Models using OpenVINO™ GenAI.
Accepts video and text prompts as input, and outputs text description.
It can be used to generate text summarizations from video.

[Prerequisites steps for Ubuntu](../dev_guide/advanced_install/advanced_install_guide_compilation.md#optional-step-6-install-openvino-genai-only-for-ubuntu)

## Configuration

### Generation Config

The `generation-config` property accepts config parameters in
the `KEY=VALUE,KEY=VALUE` format. For detailed information about these
parameters, refer to the [OpenVINO™ GenAI GenerationConfig
documentation](https://docs.openvino.ai/2026/api/genai_api/_autosummary/openvino_genai.GenerationConfig.html).

Available `generation-config` keys:

Generation Config Parameters

| Key                            | Format                                               |
|--------------------------------|------------------------------------------------------|
| max_new_tokens                 | Integer                                              |
| max_length                     | Integer                                              |
| ignore_eos                     | Boolean                                              |
| min_new_tokens                 | Integer                                              |
| eos_token_id                   | Integer                                              |
| stop_strings                   | String (semicolon-separated), example: STOP;END;DONE |
| include_stop_str_in_output     | Boolean                                              |
| stop_token_ids                 | Integer (semicolon-separated), example: 1;2;3        |
| repetition_penalty             | Float                                                |
| presence_penalty               | Float                                                |
| frequency_penalty              | Float                                                |
| num_beams                      | Integer                                              |
| num_beam_groups                | Integer                                              |
| diversity_penalty              | Float                                                |
| length_penalty                 | Float                                                |
| num_return_sequences           | Integer                                              |
| no_repeat_ngram_size           | Integer                                              |
| stop_criteria                  | String, StopCriteria: EARLY, HEURISTIC, or NEVER     |
| do_sample                      | Boolean                                              |
| temperature                    | Float                                                |
| top_p                          | Float                                                |
| top_k                          | Integer                                              |
| min_p                          | Float                                                |
| rng_seed                       | Integer                                              |
| pruning_ratio                  | Integer, 0-100 CDPruner                              |
| relevance_weight               | Float, CDPruner                                      |
| assistant_confidence_threshold | Float                                                |
| num_assistant_tokens           | Integer                                              |
| max_ngram_size                 | Integer                                              |
| apply_chat_template            | Boolean                                              |

`pruning_ratio` and `relevance_weight` configure CDPruner visual-token pruning and
apply only to Vision Language Models. `pruning_ratio=0` (default) disables pruning.

> **Note:** Structured output (`json_schema`, `regex`, `grammar`, `backend`), is not 
> currently supported. Those values contain special characters (commas, spaces and `=`)
> which cannot fit the `KEY=VALUE,KEY=VALUE` grammar.

Example:

```text
generation-config="max_new_tokens=100,temperature=0.7,do_sample=true"
```

### Scheduler Config

The `scheduler-config` property accepts config parameters in the
`KEY=VALUE,KEY=VALUE` format. For detailed information about these
parameters, refer to the [OpenVINO™ GenAI SchedulerConfig
documentation](https://docs.openvino.ai/2026/api/genai_api/_autosummary/openvino_genai.SchedulerConfig.html).

Available `scheduler-config` keys:

Scheduler Config Parameters

| Key                                            | Format                                                  |
|------------------------------------------------|---------------------------------------------------------|
| max_num_batched_tokens                         | Integer                                                 |
| num_kv_blocks                                  | Integer                                                 |
| cache_size                                     | Integer                                                 |
| num_linear_attention_blocks                    | Integer                                                 |
| cache_interval_multiplier                      | Integer                                                 |
| dynamic_split_fuse                             | Boolean                                                 |
| use_cache_eviction                             | Boolean                                                 |
| max_num_seqs                                   | Integer                                                 |
| enable_prefix_caching                          | Boolean                                                 |
| use_sparse_attention                           | Boolean                                                 |
| cache_eviction_start_size                      | Integer                                                 |
| cache_eviction_recent_size                     | Integer                                                 |
| cache_eviction_max_cache_size                  | Integer                                                 |
| cache_eviction_aggregation_mode                | String, AggregationMode: SUM, NORM_SUM, or ADAPTIVE_RKV |
| cache_eviction_apply_rotation                  | Boolean                                                 |
| cache_eviction_snapkv_window_size              | Integer (0 disables SnapKV aggregation)                 |
| cache_eviction_kvcrush_budget                  | Integer, KVCrush blocks; 0 disables                     |
| cache_eviction_kvcrush_rng_seed                | Integer                                                 |
| cache_eviction_kvcrush_anchor_point_mode       | String: RANDOM, ZEROS, ONES, MEAN, ALTERNATING          |
| cache_eviction_adaptive_rkv_attention_mass     | Float (used with ADAPTIVE_RKV aggregation mode)         |
| cache_eviction_adaptive_rkv_window_size        | Integer (used with ADAPTIVE_RKV aggregation mode)       |
| sparse_attention_mode                          | String: TRISHAPE or XATTENTION                          |
| sparse_attention_num_last_dense_tokens_in_prefill   | Integer                                            |
| sparse_attention_num_retained_start_tokens_in_cache | Integer (TRISHAPE mode)                            |
| sparse_attention_num_retained_recent_tokens_in_cache| Integer (TRISHAPE mode)                            |
| sparse_attention_xattention_threshold          | Float (XATTENTION mode)                                 |
| sparse_attention_xattention_block_size         | Integer (XATTENTION mode)                               |
| sparse_attention_xattention_stride             | Integer (XATTENTION mode)                               |

`cache_eviction_*` keys take effect only when `use_cache_eviction=true`, and
`sparse_attention_*` keys only when `use_sparse_attention=true`. The KVCrush
algorithm (`cache_eviction_kvcrush_*`) cannot be combined with the `ADAPTIVE_RKV`
aggregation mode.

Example:

```bash
scheduler-config="max_num_batched_tokens=256,cache_size=10,use_cache_eviction=true"
```

### Pipeline Config

The `pipeline-config` property accepts OpenVINO™ device/plugin properties in the
`KEY=VALUE,KEY=VALUE` format. These are passed to the pipeline at construction and
coerced to the expected type by the plugin. This is primarily useful for device
tuning, most notably NPU, where prompt/response sizing and compilation hints are set
through plugin properties. For details, refer to the [Inference with OpenVINO™ GenAI on
NPU guide](https://docs.openvino.ai/2026/openvino-workflow-generative/inference-with-genai/inference-with-genai-on-npu.html).

Common NPU keys: `MAX_PROMPT_LEN`, `MIN_RESPONSE_LEN`, `GENERATE_HINT`
(`FAST_COMPILE` or `BEST_PERF`), `PREFILL_HINT` (`DYNAMIC` or `STATIC`), `CACHE_MODE`.

Example:

```bash
pipeline-config="MAX_PROMPT_LEN=2048,MIN_RESPONSE_LEN=512,GENERATE_HINT=BEST_PERF"
```

> **Note:** Boolean values are case-insensitive and accept `true`/`false`, `1`/`0`,
> `yes`/`no`, or `on`/`off`. For example: `pipeline-config="PERF_COUNT=NO"`. The same
> accepted forms apply to booleans in `generation-config` and `scheduler-config`.

#### Per-device (nested) properties

A key of the form `DEVICE.PROPERTY` nests `PROPERTY` under that device's property block
(OpenVINO™ `DEVICE_PROPERTIES_<DEVICE>`), while un-dotted keys remain top-level. This is
required for VLM on NPU:

```bash
pipeline-config="NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512,GENERATE_HINT=BEST_PERF"
```

This sets `MAX_PROMPT_LEN` and `MIN_RESPONSE_LEN` on the `NPU` device block and
`GENERATE_HINT` at the top level.

### Vision Mode

The `vision-mode` property controls how the frames accumulated for one inference
(`chunk-size` frames) are presented to the model:

| Value   | Behavior                                                                       |
|---------|--------------------------------------------------------------------------------|
| `image` | (default) Frames are sent as independent images. Works with any VLM.           |
| `video` | Frames are sent as a single video clip. Requires a video-capable model.        |

In `video` mode the frames are stacked into one clip and tagged with the model's native
video tag, so the model receives temporal context (frame ordering and, when available,
playback rate) rather than a set of unrelated stills. This is the correct mode for tasks
like action recognition or "describe what happens over time".

Video mode requires a model that supports video input — for example **Qwen2-VL**,
**Qwen2.5-VL**, **Qwen3-VL** or **LLaVA-NeXT-Video**. Image-only models (e.g. Phi-3.5-vision,
MiniCPM-V) must use `image` mode. For more information, see [Use Image or Video Tags in Prompt](https://openvinotoolkit.github.io/openvino.genai/docs/use-cases/image-processing/#use-image-or-video-tags-in-prompt).

The clip's frame rate is derived automatically from the input stream and the `frame-rate`
sampling property. For a single frame (`chunk-size=1`), `image` mode is preferred.

Example:

```bash
vision-mode=video chunk-size=16 frame-rate=2
```

```bash
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: { (string)RGB, (string)RGBA, (string)RGBx, (string)BGR, (string)BGRA, (string)BGRx, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                 format: { (string)DMA_DRM }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                 format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:D3D11Memory)
                 format: { (string)NV12 }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: { (string)RGB, (string)RGBA, (string)RGBx, (string)BGR, (string)BGRA, (string)BGRx, (string)NV12, (string)I420 }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:DMABuf)
                 format: { (string)DMA_DRM }
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]
      video/x-raw(memory:VAMemory)
                 format: { (string)NV12 }
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
  chunk-size          : Number of frames in one inference
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 4294967295 Default: 1
  device              : Device to use (CPU, GPU, NPU, etc.)
                        flags: readable, writable
                        String. Default: "CPU"
  frame-rate          : Number of frames sampled per second for inference (0 = process all frames)
                        flags: readable, writable
                        Double. Range:               0 -   1.797693e+308 Default:               0
  generation-config   : Generation configuration as KEY=VALUE,KEY=VALUE format
                        flags: readable, writable
                        String. Default: null
  metrics             : Include performance metrics in JSON output
                        flags: readable, writable
                        Boolean. Default: false
  model-cache-path    : Path for caching compiled models (GPU/NPU only)
                        flags: readable, writable
                        String. Default: "ov_cache"
  model-path          : Path to the GenAI model
                        flags: readable, writable
                        String. Default: null
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "gvagenai0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  pipeline-config     : OpenVINO device/plugin properties passed to the pipeline at construction, as KEY=VALUE,KEY=VALUE format
                        flags: readable, writable
                        String. Default: null
  prompt              : Text prompt for the GenAI model
                        flags: readable, writable
                        String. Default: null
  prompt-path         : Path to text prompt file for the GenAI model
                        flags: readable, writable
                        String. Default: null
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  scheduler-config    : Scheduler configuration as KEY=VALUE,KEY=VALUE format
                        flags: readable, writable
                        String. Default: null
  vision-mode         : How accumulated frames are presented to the model: as independent images, or as one video clip. Video mode requires a video-capable model
                        flags: readable, writable
                        Enum "GstGvaGenAIVisionMode" Default: 0, "image"
                           (0): image            - Present accumulated frames as independent images
                           (1): video            - Present accumulated frames as one video clip
```
