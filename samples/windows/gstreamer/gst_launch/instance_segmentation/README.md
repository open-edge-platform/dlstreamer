# Instance Segmentation Sample (Windows)

This sample demonstrates instance segmentation using Mask R-CNN models on Windows.

## How It Works

The sample builds a GStreamer pipeline using:
- `filesrc` or `urisourcebin` for input
- `decodebin3` for video decoding
- `gvadetect` for instance segmentation
- `gvawatermark` for mask and bounding box visualization
- `d3d11convert` for D3D11-accelerated processing

## Models

Supports two Mask R-CNN variants from TensorFlow Model Zoo:
- **mask_rcnn_inception_resnet_v2_atrous_coco** - Higher accuracy
- **mask_rcnn_resnet50_atrous_coco** - Faster inference

Both models trained on COCO dataset (80 object classes).

> **NOTE**: Run `download_public_models.bat` before using this sample.

## Environment Variables

```PowerShell
$set MODELS_PATH = "C:\models"
```

Models should be located at:
- `%MODELS_PATH%\public\mask_rcnn_inception_resnet_v2_atrous_coco\FP16\mask_rcnn_inception_resnet_v2_atrous_coco.xml`
- `%MODELS_PATH%\public\mask_rcnn_resnet50_atrous_coco\FP16\mask_rcnn_resnet50_atrous_coco.xml`

## Running

```PowerShell
.\instance_segmentation.ps1 [-Model <model>] [-Device <device>] [-InputSource <path>] [-OutputType <type>] [-JsonFile <file>] [-FrameLimiter <element>]
```

Parameters:
- **-Model** - Model name (default: `mask_rcnn_inception_resnet_v2_atrous_coco`)
  - Supported: `mask_rcnn_inception_resnet_v2_atrous_coco`, `mask_rcnn_resnet50_atrous_coco`
- **-Device** - Inference device (default: `CPU`)
  - Supported: `CPU`, `GPU`, `NPU`
- **-InputSource** - Input source (default: `https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4`)
  - Local file path (e.g., `C:\videos\street.mp4`)
  - URL (e.g., `https://...`)
- **-OutputType** - Output type (default: `file`)
  - `file` - Save to MP4 with watermark
  - `display` - Display video with overlay
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Display and export
  - `jpeg` - Save frames as JPEG sequence
- **-JsonFile** - JSON output filename (default: `output.json`)
- **-FrameLimiter** - Optional GStreamer element to insert after decode (default: empty)
  - Example: `" ! identity eos-after=100"` - Process only first 100 frames
  - Example: `" ! identity eos-after=1000"` - Process only first 1000 frames
  - Useful for testing/benchmarking with limited frame count

## Examples

### Use default settings (Inception ResNet V2, CPU, Pexels video, save to file)
```PowerShell
.\instance_segmentation.ps1
```

### ResNet50 on GPU with display
```PowerShell
.\instance_segmentation.ps1 -Model mask_rcnn_resnet50_atrous_coco -Device GPU -InputSource "C:\videos\street.mp4" -OutputType display
```

### Export to JSON
```PowerShell
.\instance_segmentation.ps1 -Model mask_rcnn_inception_resnet_v2_atrous_coco -Device CPU -InputSource "C:\videos\street.mp4" -OutputType json -JsonFile segmentation.json
```

### Export segmentation masks as JPEG sequence
```PowerShell
.\instance_segmentation.ps1 -Model mask_rcnn_resnet50_atrous_coco -Device GPU -InputSource "C:\videos\street.mp4" -OutputType jpeg
```

### Benchmark FPS on NPU
```PowerShell
.\instance_segmentation.ps1 -Model mask_rcnn_resnet50_atrous_coco -Device NPU -InputSource "C:\videos\street.mp4" -OutputType fps
```

### Process only first 100 frames (for testing)
```PowerShell
.\instance_segmentation.ps1 -Model mask_rcnn_inception_resnet_v2_atrous_coco -Device CPU -InputSource "C:\videos\street.mp4" -OutputType json -FrameLimiter " ! identity eos-after=100"
```

## Output

The model outputs:
- **Bounding boxes** - Object locations
- **Class labels** - Object categories (person, car, etc.)
- **Instance masks** - Pixel-level segmentation masks
- **Confidence scores** - Detection confidence

COCO classes include: person, bicycle, car, motorcycle, airplane, bus, train, truck, boat, traffic light, fire hydrant, stop sign, parking meter, bench, bird, cat, dog, horse, sheep, cow, elephant, bear, zebra, giraffe, backpack, umbrella, handbag, tie, suitcase, frisbee, skis, snowboard, sports ball, kite, baseball bat, baseball glove, skateboard, surfboard, tennis racket, bottle, wine glass, cup, fork, knife, spoon, bowl, banana, apple, sandwich, orange, broccoli, carrot, hot dog, pizza, donut, cake, chair, couch, potted plant, bed, dining table, toilet, TV, laptop, mouse, remote, keyboard, cell phone, microwave, oven, toaster, sink, refrigerator, book, clock, vase, scissors, teddy bear, hair drier, toothbrush.

## Pipeline Architecture

**Note**: Pipeline varies based on device type. GPU/NPU use D3D11 hardware acceleration (`d3d11convert`, `d3d11videosink`, `d3d11h264enc`), while CPU uses software path (`videoconvert`, `autovideosink`, `openh264enc`).

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[gvadetect]
    C --> D[queue]
    D --> E{OUTPUT}
    
    E -->|file<br/>GPU/NPU| F1[d3d11convert]
    F1 --> F2[gvawatermark]
    F2 --> F3[gvafpscounter]
    F3 --> F4[d3d11h264enc]
    F4 --> F5[h264parse]
    F5 --> F6[mp4mux]
    F6 --> F7[filesink]
    
    E -->|file<br/>CPU| F1C[videoconvert]
    F1C --> F2C[gvawatermark]
    F2C --> F3C[gvafpscounter]
    F3C --> F4C[openh264enc]
    F4C --> F5C[h264parse]
    F5C --> F6C[mp4mux]
    F6C --> F7C[filesink]
    
    E -->|display<br/>GPU/NPU| G1[d3d11convert]
    G1 --> G2[gvawatermark]
    G2 --> G3[videoconvertscale]
    G3 --> G4[gvafpscounter]
    G4 --> G5[d3d11videosink]
    
    E -->|display<br/>CPU| G1C[gvawatermark]
    G1C --> G2C[videoconvertscale]
    G2C --> G3C[gvafpscounter]
    G3C --> G4C[autovideosink]
    
    E -->|fps| H1[gvafpscounter]
    H1 --> H2[fakesink]
    
    E -->|json| I1[gvametaconvert]
    I1 --> I2[gvametapublish]
    I2 --> I3[fakesink]
    
    E -->|display-and-json<br/>GPU/NPU| J1[d3d11convert]
    J1 --> J2[gvawatermark]
    J2 --> J3[gvametaconvert]
    J3 --> J4[gvametapublish]
    J4 --> J5[videoconvert]
    J5 --> J6[gvafpscounter]
    J6 --> J7[d3d11videosink]
    
    E -->|display-and-json<br/>CPU| J1C[gvawatermark]
    J1C --> J2C[gvametaconvert]
    J2C --> J3C[gvametapublish]
    J3C --> J4C[videoconvert]
    J4C --> J5C[gvafpscounter]
    J5C --> J6C[autovideosink]
    
    E -->|jpeg<br/>GPU/NPU| K1[d3d11convert]
    K1 --> K2[gvawatermark]
    K2 --> K3[videoconvert]
    K3 --> K4[jpegenc]
    K4 --> K5[multifilesink]
    
    E -->|jpeg<br/>CPU| K1C[videoconvert]
    K1C --> K2C[gvawatermark]
    K2C --> K3C[jpegenc]
    K3C --> K4C[multifilesink]
```

## Performance Tips

1. **Use ResNet50 model** for faster inference
2. **GPU device** provides best performance
3. **Lower input resolution** reduces processing time
4. **Preprocessing backend**: Use `d3d11` for GPU, `opencv` for CPU

## See also
* [Windows Samples overview](../../../README.md)
* [Linux Instance Segmentation Sample](../../../../gstreamer/gst_launch/instance_segmentation/README.md)
