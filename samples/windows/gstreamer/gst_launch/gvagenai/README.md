# Using VLM Models With gvagenai Element (Windows)

This directory contains a PowerShell script demonstrating how to use the `gvagenai` element with MiniCPM-V 2.6, Phi-4-multimodal-instruct or Gemma 3 for video summarization on Windows.

The `gvagenai` element integrates OpenVINO™ GenAI capabilities into video processing pipelines. It supports visual language models like MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 for video content description and analysis.

## How It Works

The script constructs a GStreamer pipeline that processes video input from various sources (file, URL, or web camera) and applies the model for generating summarization of the video content.

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds a GStreamer pipeline of the following elements:
- `filesrc` or `urisourcebin` or `mfvideosrc` for input from file/URL/web-camera
- `decodebin3` for video decoding
- optional downscaling stage when `-Resolution` is given — smaller frames mean faster inference. On `GPU`/`NPU` this scales on the GPU with `d3d11upload ! d3d11convert`; on `CPU` it uses `videoscale`
- `gvagenai` for inferencing with the model to generate text descriptions. It accepts RGB/BGR/NV12/I420 directly and converts the frame internally
- `gvametapublish` for saving inference results to a JSON file
- `fakesink` for discarding output

## Model Preparation

Refer to [OpenVINO™ GenAI Model Preparation](https://openvinotoolkit.github.io/openvino.genai/docs/category/model-preparation) for more details on downloading pre-converted OpenVINO models or converting models to OpenVINO format.

> [!NOTE]
> To install `optimum-cli` and other required dependencies for model export, refer to the respective OpenVINO™ notebook tutorials linked in the table below.
> DL Streamer currently depends on OpenVINO™ GenAI 2026.2.0. For optimal compatibility, use the library versions specified in [export-requirements.txt](https://github.com/openvinotoolkit/openvino.genai/blob/releases/2026/2/samples/export-requirements.txt).

| Model | Export Command | Tutorial |
|-------|----------------|----------|
| **MiniCPM-V 2.6** | `optimum-cli export openvino --model openbmb/MiniCPM-V-2_6 --weight-format int4 MiniCPM-V-2_6` | [Visual-language assistant with MiniCPM-V2 and OpenVINO™](https://github.com/openvinotoolkit/openvino_notebooks/blob/latest/notebooks/minicpm-v-multimodal-chatbot/minicpm-v-multimodal-chatbot.ipynb) |
| **Phi-4-multimodal-instruct** | `optimum-cli export openvino --model microsoft/Phi-4-multimodal-instruct Phi-4-multimodal` | [Visual-language assistant with Phi-4-multimodal-instruct and OpenVINO™](https://github.com/openvinotoolkit/openvino_notebooks/blob/latest/notebooks/phi-4-multimodal/phi-4-multimodal.ipynb) |
| **Gemma 3** | `optimum-cli export openvino --model google/gemma-3-4b-it Gemma3` | [Visual-language assistant with Gemma 3 and OpenVINO™](https://github.com/openvinotoolkit/openvino_notebooks/blob/latest/notebooks/gemma3/gemma3.ipynb) |

After exporting the model, set the model path (PowerShell):
```powershell
$env:GENAI_MODEL_PATH = "C:\path\to\your\model"
```

## Running the Sample

**Usage:**
```powershell
.\sample_gvagenai.ps1 [OPTIONS]
```

The sample takes the following *optional* parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| `-Source FILE/URL/CAMERA` | Input source: a file path, an HTTP(S) URL, `camera` for the default webcam, or a webcam device index (e.g. `0`) | Pexels video URL |
| `-Device DEVICE` | Inference device (`CPU`, `GPU`, `NPU`, or indexed GPU like GPU.0) | `CPU` |
| `-Prompt TEXT` | Text prompt for the model | `Describe what you see in this video.` |
| `-FrameRate RATE` | Frame sampling rate (fps) | `1` |
| `-ChunkSize NUM` | Chunk size, or frames per inference call | `10` |
| `-MaxTokens NUM` | Maximum new tokens to generate | `100` |
| `-Resolution WxH` | Scale frames to `WxH` before inference (e.g. `320x240`). Smaller frames mean faster inference. GPU/NPU scale on the GPU (`d3d11upload ! d3d11convert`); CPU uses `videoscale` | source resolution (no scaling) |
| `-VisionMode image\|video` | How frames are presented to the model. `video` requires a video-capable model (e.g. Qwen2/2.5/3-VL, LLaVA-NeXT-Video); image-only models such as MiniCPM-V use `image`. See [Use Image or Video Tags in Prompt](https://openvinotoolkit.github.io/openvino.genai/docs/use-cases/image-processing/#use-image-or-video-tags-in-prompt) | `image` |
| `-PipelineConfig KEY=VAL,...` | OpenVINO device/plugin properties passed to the pipeline, as `KEY=VALUE,KEY=VALUE`. Most useful for NPU tuning, where properties are nested per device, e.g. `NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512` | none |
| `-SchedulerConfig KEY=VAL,...` | Continuous-batching scheduler configuration, as `KEY=VALUE,KEY=VALUE` (e.g. `enable_prefix_caching=true,use_cache_eviction=true`). See the [gvagenai element documentation](../../../../../docs/user-guide/elements/gvagenai.md) for the full list of keys | none |
| `-Output FILE` | Output JSON file path | `genai_output.json` |
| `-Metrics` | Include performance metrics in JSON output | off |
| `-Help` | Show help message | |

**Examples:**

1. **Basic usage with default settings**
   ```powershell
   .\sample_gvagenai.ps1
   ```

2. **Custom settings example**
   ```powershell
   .\sample_gvagenai.ps1 -Source C:\videos\video.mp4 -Device GPU -Prompt "Describe what do you see in this video?" -ChunkSize 10 -FrameRate 1 -MaxTokens 100
   ```

3. **With performance metrics enabled**
   ```powershell
   .\sample_gvagenai.ps1 -Metrics -MaxTokens 200
   ```

4. **Downscale frames for faster inference**
   ```powershell
   .\sample_gvagenai.ps1 -Resolution 320x240
   ```

5. **Video mode (video-capable models only, e.g. Qwen2.5-VL)**
   ```powershell
   .\sample_gvagenai.ps1 -VisionMode video -ChunkSize 16 -FrameRate 2
   ```

6. **NPU with required prompt/response sizing**
   ```powershell
   .\sample_gvagenai.ps1 -Device NPU -PipelineConfig NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512
   ```

7. **Continuous batching with a custom output file**
   ```powershell
   .\sample_gvagenai.ps1 -SchedulerConfig enable_prefix_caching=true -Output results.json
   ```

**Output:**
- Results are saved to the file given by `-Output` (default `genai_output.json`), one JSON object per inference.
- Each object contains the generated `result` text, a `confidence` score (when available), and the frame `timestamp`/`timestamp_seconds`:
  ```json
  {
    "result": "The video shows a person walking across a room.",
    "confidence": 0.87,
    "timestamp": 2000000000,
    "timestamp_seconds": 2.0
  }
  ```
- When `-Metrics` is enabled, each object also includes a `metrics` block with load time, token counts, and latency/throughput statistics (in milliseconds unless noted):
  ```json
  {
    "result": "...",
    "metrics": {
      "load_time": 1000.0,
      "num_generated_tokens": 100,
      "num_input_tokens": 512,
      "ttft_mean": 210.3,
      "tpot_mean": 35.1,
      "throughput_mean": 28.5,
      "generate_duration_mean": 3600.0,
      "chat_template_duration_mean": 0.4
    }
  }
  ```
  Additional fields include `*_std` counterparts and `inference_duration`, `tokenization_duration`, `detokenization_duration`, `prepare_embeddings_duration`, and `ipot` statistics.

## Troubleshooting

### General Issues

**Model validation:**
- Use [LLM Bench tool](https://github.com/openvinotoolkit/openvino.genai/tree/master/tools/llm_bench) to verify the model works with OpenVINO™ GenAI runtime independently

**Model path not set:**
- Ensure that the `GENAI_MODEL_PATH` environment variable is correctly set to the path of your model: `$env:GENAI_MODEL_PATH = "C:\path\to\model"`
- Verify the directory exists and contains the required model files

**Debug logging:**
- Enable detailed logs: `$env:GST_DEBUG = "gvagenai:5"` before running the script
- When using `-Metrics`, `GST_DEBUG=gvagenai:4` is automatically enabled (unless `GST_DEBUG` is already set)

### Common Error Messages

**Chat template error:**
```
Chat template wasn't found. This may indicate that the model wasn't trained for chat scenario.
Please add 'chat_template' to tokenizer_config.json to use the model in chat scenario.
```
- **Cause:** The model is outdated and doesn't contain a chat template
- **Solution:** Re-export the model with the required `export-requirements.txt`

**Tokenizer error:**
```
Either openvino_tokenizer.xml was not provided or it was not loaded correctly.
Tokenizer::encode is not available
```
- **Cause:** The tokenizer file is missing or corrupted
- **Solution:**
  1. Install sentencepiece: `pip install sentencepiece`
  2. Re-export the model with the required `export-requirements.txt`

**VLM pipeline on NPU prompt length:**
```
VLM pipeline on NPU may only process input embeddings up to 1024 tokens. <N> is passed.
Set the "MAX_PROMPT_LEN" config option to increase the limit.
```
- **Cause:** The input (prompt + image/video tokens) exceeds the NPU static-pipeline default of 1024 tokens
- **Solution:** Raise the limit with `-PipelineConfig NPU.MAX_PROMPT_LEN=<N>,NPU.MIN_RESPONSE_LEN=<M>` (values must be integers). Note that memory and compile time grow with `MAX_PROMPT_LEN`; reducing `-Resolution` and `-FrameRate` also lowers the token count

**Video preprocessing not implemented:**
```
Video preprocessing isn't implemented for this model. Pass frames as independent images.
```
- **Cause:** `-VisionMode video` was used with a model that does not support video input (e.g. MiniCPM-V, Phi-4-multimodal, Gemma 3 are image-only)
- **Solution:** Use `-VisionMode image` (the default), or switch to a video-capable model such as Qwen2/2.5/3-VL or LLaVA-NeXT-Video

## See also
* [Windows Samples overview](../../../README.md)
* [Linux gvagenai Sample](../../../../gstreamer/gst_launch/gvagenai/README.md)
