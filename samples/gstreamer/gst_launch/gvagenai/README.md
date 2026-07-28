# Using VLM Models With gvagenai Element

This directory contains a script demonstrating how to use the `gvagenai` element with MiniCPM-V 2.6, Phi-4-multimodal-instruct or Gemma 3 for video summarization.

The `gvagenai` element integrates OpenVINO™ GenAI capabilities into video processing pipelines. It supports visual language models like MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 for video content description and analysis.

## How It Works

The script constructs a GStreamer pipeline that processes video input from various sources (file, URL, or camera) and applies the model for generating summarization of the video content.

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds GStreamer pipeline of the following elements:
- `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
- `decodebin3` for video decoding
- `vapostproc` or `videoscale` (optional) for downscaling frames when `--resolution` is given — smaller frames mean faster inference. VA-API scaling (`vapostproc`) is used when available; otherwise the CPU `videoscale` is used
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

After exporting the model, set the model path:
```bash
export GENAI_MODEL_PATH=/path/to/your/model
```

## Running the Sample

**Usage:**
```bash
./sample_gvagenai.sh [OPTIONS]
```

**Options:**
- `-S, --source FILE/URL/CAMERA`: Input source (file path, URL or web camera)
- `-D, --device DEVICE`: Inference device (CPU, GPU, NPU, or indexed GPU like GPU.0)
- `-P, --prompt TEXT`: Text prompt for the model
- `-F, --frame-rate RATE`: Frame sampling rate (fps)
- `-C, --chunk-size NUM`: Chunk size, or frames per inference call
- `-T, --max-tokens NUM`: Maximum new tokens to generate
- `-R, --resolution WxH`: Scale frames to `WxH` before inference (e.g. `320x240`). Smaller frames mean faster inference. Uses GPU `vapostproc` when available, else CPU `videoscale`. Default: source resolution (no scaling)
- `-V, --vision-mode image|video`: How frames are presented to the model. `video` requires a video-capable model (e.g. Qwen2/2.5/3-VL, LLaVA-NeXT-Video); image-only models such as MiniCPM-V use `image`. Default: `image`. See [Use Image or Video Tags in Prompt](https://openvinotoolkit.github.io/openvino.genai/docs/use-cases/image-processing/#use-image-or-video-tags-in-prompt) for model-specific tags and video support
- `-A, --pipeline-config KEY=VAL,...`: OpenVINO device/plugin properties passed to the pipeline, as `KEY=VALUE,KEY=VALUE`. Most useful for NPU tuning, where properties are nested per device, e.g. `NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512`
- `-B, --scheduler-config KEY=VAL,...`: Continuous-batching scheduler configuration, as `KEY=VALUE,KEY=VALUE` (e.g. `enable_prefix_caching=true,use_cache_eviction=true`). See the [gvagenai element documentation](../../../../docs/user-guide/elements/gvagenai.md) for the full list of keys
- `-O, --output FILE`: Output JSON file path. Default: `genai_output.json`
- `-M, --metrics`: Include performance metrics in JSON output
- `-H, --help`: Show help message

**Examples:**

1. **Basic usage with default settings**
   ```bash
   ./sample_gvagenai.sh
   ```

2. **Custom settings example**
   ```bash
   ./sample_gvagenai.sh --source /path/to/video.mp4 --device GPU --prompt "Describe what do you see in this video?" --chunk-size 10 --frame-rate 1 --max-tokens 100
   ```

3. **With performance metrics enabled**
   ```bash
   ./sample_gvagenai.sh --metrics --max-tokens 200
   ```

4. **Downscale frames for faster inference**
   ```bash
   ./sample_gvagenai.sh --resolution 320x240
   ```

5. **Video mode (video-capable models only, e.g. Qwen2.5-VL)**
   ```bash
   ./sample_gvagenai.sh --vision-mode video --chunk-size 16 --frame-rate 2
   ```

6. **NPU with required prompt/response sizing**
   ```bash
   ./sample_gvagenai.sh --device NPU --pipeline-config NPU.MAX_PROMPT_LEN=2048,NPU.MIN_RESPONSE_LEN=512
   ```

7. **Continuous batching with a custom output file**
   ```bash
   ./sample_gvagenai.sh --scheduler-config enable_prefix_caching=true --output results.json
   ```

**Output:**
- Results are saved to the file given by `--output` (default `genai_output.json`), one JSON object per inference.
- Each object contains the generated `result` text, a `confidence` score (when available), and the frame `timestamp`/`timestamp_seconds`:
  ```json
  {
    "result": "The video shows a person walking across a room.",
    "confidence": 0.87,
    "timestamp": 2000000000,
    "timestamp_seconds": 2.0
  }
  ```
- When `--metrics` is enabled, each object also includes a `metrics` block with load time, token counts, and latency/throughput statistics (in milliseconds unless noted):
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
- Ensure that the `GENAI_MODEL_PATH` environment variable is correctly set to the path of your model
- Verify the directory exists and contains the required model files

**Debug logging:**
- Enable detailed logs: `GST_DEBUG=gvagenai:5 ./sample_gvagenai.sh`
- When using `--metrics` flag, `GST_DEBUG=gvagenai:4` is automatically enabled (unless `GST_DEBUG` is already set)

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

**Video preprocessing not implemented:**
```
Video preprocessing isn't implemented for this model. Pass frames as independent images.
```
- **Cause:** `--vision-mode video` was used with a model that does not support video input (e.g. MiniCPM-V, Phi-4-multimodal, Gemma 3 are image-only)
- **Solution:** Use `--vision-mode image` (the default), or switch to a video-capable model such as Qwen2/2.5/3-VL or LLaVA-NeXT-Video

## See also
* [Samples overview](../../README.md)
