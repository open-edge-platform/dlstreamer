# gvagenai

Performs inference with Vision Language Models using OpenVINO™ GenAI.
Accepts video and text prompts as input, and outputs text description.
It can be used to generate text summarizations from video.

[Prerequisites steps](https://docs.openedgeplatform.intel.com/dev/edge-ai-libraries/dl-streamer/dev_guide/advanced_install/advanced_install_guide_compilation.html#step-6-install-openvino-genai-only-for-ubuntu-optional)

## Configuration

### Generation Config

The `generation-config` property accepts config parameters in
the `KEY=VALUE,KEY=VALUE` format. For detailed information about these
parameters, refer to the [OpenVINO™ GenAI GenerationConfig
documentation](https://docs.openvino.ai/2025/api/genai_api/_autosummary/openvino_genai.GenerationConfig.html).

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
| rng_seed                       | Integer                                              |
| assistant_confidence_threshold | Float                                                |
| num_assistant_tokens           | Integer                                              |
| max_ngram_size                 | Integer                                              |
| apply_chat_template            | Boolean                                              |

Example:

```text
generation-config="max_new_tokens=100,temperature=0.7,do_sample=true"
```

### Scheduler Config

The `scheduler-config` property accepts config parameters in the
`KEY=VALUE,KEY=VALUE` format. For detailed information about these
parameters, refer to the [OpenVINO™ GenAI SchedulerConfig
documentation](https://docs.openvino.ai/2025/api/genai_api/_autosummary/openvino_genai.SchedulerConfig.html).

Available `scheduler-config` keys:

Scheduler Config Parameters

| Key                               | Format                                   |
|-----------------------------------|------------------------------------------|
| max_num_batched_tokens            | Integer                                  |
| num_kv_blocks                     | Integer                                  |
| cache_size                        | Integer                                  |
| dynamic_split_fuse                | Boolean                                  |
| use_cache_eviction                | Boolean                                  |
| max_num_seqs                      | Integer                                  |
| enable_prefix_caching             | Boolean                                  |
| cache_eviction_start_size         | Integer                                  |
| cache_eviction_recent_size        | Integer                                  |
| cache_eviction_max_cache_size     | Integer                                  |
| cache_eviction_aggregation_mode   | String, AggregationMode: SUM or NORM_SUM |
| cache_eviction_apply_rotation     | Boolean                                  |
| cache_eviction_snapkv_window_size | Integer                                  |

Example:

```bash
scheduler-config="max_num_batched_tokens=256,cache_size=10,use_cache_eviction=true"
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
  model-cache-path    : Path for caching compiled models (GPU only)
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
```
