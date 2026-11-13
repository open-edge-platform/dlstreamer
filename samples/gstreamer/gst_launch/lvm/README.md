# Image Embeddings Generation with ViT (gst-launch command line)

This sample demonstrates the use of Vision Transformer (ViT) in a pipeline constructed via `gst-launch-1.0` command-line utility. It allows the extraction of image embeddings (CLS tokens) for each frame using the Vision Transformer extracted from a CLIP model.

## How It Works

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds a GStreamer pipeline of the following elements:

* `filesrc`, `urisourcebin`, or `v4l2src` for input from file/URL/web-camera
* `decodebin3` for video decoding
* `videoconvert` for converting video frames into different color formats
* `videoscale` for scaling video frames
* `vapostproc` for post-processing (used in GPU pipeline)
* `gvainference` for running inference using the CLIP Vision Transformer model
* `gvametaconvert` for converting metadata to JSON format
* `gvametapublish` for publishing metadata to a file
* `gvafpscounter` for measuring FPS (used in FPS mode)
* `fakesink` for discarding the output

## Model

The sample currently supports these CLIP image encoders: [`openai/clip-vit-large-patch14`](https://huggingface.co/openai/clip-vit-large-patch14), [`openai/clip-vit-base-patch16`](https://huggingface.co/openai/clip-vit-base-patch16), and [`openai/clip-vit-base-patch32`](https://huggingface.co/openai/clip-vit-base-patch32). Prepare one of these examples with the general [`download_hf_models.py`](../../../../scripts/download_models/README.md#1-hugging-face-conversion) helper; see its README for supported models and setup. The helper converts the Hugging Face ID to the model directory name used by this sample, such as `openai_clip-vit-large-patch14`.

## Running

```sh
    export MODELS_PATH="$HOME"/models
    ./generate_frame_embeddings.sh [INPUT] [DEVICE] [OUTPUT] [MODEL] [PREPROCESSING_BACKEND]
```

The sample requires a video source; device, output, model, and preprocessing backend are optional:

1. [INPUT] to specify the input source.  
The input could be:
    * local video file
    * web camera device (e.g., `/dev/video0`)
    * RTSP camera (URL starting with `rtsp://`) or other streaming source (e.g., URL starting with `http://`)  
Use any local video or supported stream URL.

2. [DEVICE] to specify the device for inference.  
   You can choose either `CPU` or `GPU`.
3. [OUTPUT] to choose between file output mode and FPS throughput mode:
    * json - output to a JSON file (default)
    * fps - FPS only
4. [MODEL] to specify the model for inference:
    * `openai_clip-vit-large-patch14` (default)
    * `openai_clip-vit-base-patch16`
    * `openai_clip-vit-base-patch32`
5. [PREPROCESSING_BACKEND] to specify the GPU preprocessing backend (default: `opencv`):
    * `ie`
    * `opencv`
    * `va`
    * `va-surface-sharing`
   The CPU pipeline always uses `opencv`.

## Sample Output

The sample:

* prints the `gst-launch-1.0` full command line into the console
* starts the command and either publishes metadata to a file or prints out FPS if you set OUTPUT=fps

## See also

* [Samples overview](../../../README.md)

## Example Usage

To run the sample with default values:

    ./generate_frame_embeddings.sh /path/to/video.mp4

To specify a source file, device, and output:

    ./generate_frame_embeddings.sh /path/to/video.mp4 GPU fps

To specify a local video, device, output, and model:

    ./generate_frame_embeddings.sh /path/to/video.mp4 CPU json openai_clip-vit-large-patch14

To specify a video device, device, output and model:

    ./generate_frame_embeddings.sh /dev/video0 CPU fps openai_clip-vit-base-patch16
