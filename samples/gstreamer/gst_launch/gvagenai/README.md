# Models with gvagenai Element

This directory contains a script demonstrating how to use the `gvagenai` element with MiniCPM-V 2.6, Phi-4-multimodal-instruct or Gemma 3 for video summarization.

The `gvagenai` element integrates OpenVINO™ GenAI capabilities into video processing pipelines. It supports visual language models like MiniCPM-V, Phi-4-multimodal-instruct or Gemma 3 for video content description and analysis.

## How It Works

The script constructs a GStreamer pipeline that processes video input from various sources (file, URL, or camera) and applies the model for generating summerization of the video content.

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds GStreamer pipeline of the following elements:
- `filesrc` or `urisourcebin` or `v4l2src` for input from file/URL/web-camera
- `decodebin3` for video decoding
- `videoconvert` for converting video frame into RGB color format
- `gvagenai` for inferencing with the model to generate text descriptions
- `gvametapublish` for saving inference results to a JSON file
- `fakesink` for discarding output

## Model Preparation

> [!NOTE]
> To install `optimum-cli` and other required dependencies for model export, refer to the respective OpenVINO™ notebook tutorials linked in the table below.
> DL Streamer currently depends on OpenVINO™ GenAI 2025.4.0. For optimal compatibility, use the library versions specified in [export-requirements.txt](https://github.com/openvinotoolkit/openvino.genai/blob/releases/2025/4/samples/export-requirements.txt).

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
- `-s, --source FILE/URL/CAMERA`: Input source (file path, URL or web camera)
- `-d, --device DEVICE`: Inference device (CPU, GPU, NPU)
- `-p, --prompt TEXT`: Text prompt for the model
- `-r, --frame-rate RATE`: Frame sampling rate (fps)
- `-c, --chunk-size NUM`: Chunk size, or frames per inference call
- `-t, --max-tokens NUM`: Maximum new tokens to generate
- `-m, --metrics`: Include performance metrics in JSON output
- `-h, --help`: Show help message

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

4. **Print more logs**
   ```bash
   GST_DEBUG=gvagenai:4 ./sample_gvagenai.sh
   ```

**Output:**
- Results are saved to `genai_output.json`
- Contains inference results with timestamps and metadata
- When `--metrics` is enabled, includes performance metrics such as inference time and throughput

## Troubleshooting

### General Issues

**Model validation:**
- Use [LLM Bench tool](https://github.com/openvinotoolkit/openvino.genai/tree/master/tools/llm_bench) to verify the model works with OpenVINO™ GenAI runtime independently

**Model path not set:**
- Ensure that the `GENAI_MODEL_PATH` environment variable is correctly set to the path of your model
- Verify the directory exists and contains the required model files

**Debug logging:**
- Enable detailed logs: `GST_DEBUG=gvagenai:5 ./sample_gvagenai.sh`
- When using `--metrics` flag, `GST_DEBUG=4` is automatically enabled

### Common Error Messages

**Chat template error:**
```
Chat template wasn't found. This may indicate that the model wasn't trained for chat scenario.
Please add 'chat_template' to tokenizer_config.json to use the model in chat scenario.
```
- **Cause:** The model is outdated and doesn't contain a chat template
- **Solution:** Re-export the model with the latest version of `optimum-intel` library

**Tokenizer error:**
```
Either openvino_tokenizer.xml was not provided or it was not loaded correctly.
Tokenizer::encode is not available
```
- **Cause:** The tokenizer file is missing or corrupted
- **Solution:** 
  1. Install sentencepiece: `pip install sentencepiece`
  2. Re-export the model with the latest version of `optimum-intel` library

## See also
* [Samples overview](../../README.md)
