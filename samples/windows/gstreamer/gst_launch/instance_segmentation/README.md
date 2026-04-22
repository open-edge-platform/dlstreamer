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

```powershell
$set MODELS_PATH=C:\models\models
```

Models should be located at:
- `%MODELS_PATH%\public\mask_rcnn_inception_resnet_v2_atrous_coco\FP16\mask_rcnn_inception_resnet_v2_atrous_coco.xml`
- `%MODELS_PATH%\public\mask_rcnn_resnet50_atrous_coco\FP16\mask_rcnn_resnet50_atrous_coco.xml`

## Running

```powershell
.\instance_segmentation.bat [MODEL] [DEVICE] [INPUT] [OUTPUT] [JSON_FILE] [BENCHMARK_SINK]
```

Arguments:
- **MODEL** - Model name (default: `mask_rcnn_inception_resnet_v2_atrous_coco`)
  - Supported: `mask_rcnn_inception_resnet_v2_atrous_coco`, `mask_rcnn_resnet50_atrous_coco`
- **DEVICE** - Inference device (default: `CPU`)
  - Supported: `CPU`, `GPU`, `NPU`
- **INPUT** - Input source (default: `https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4`)
  - Local file path (e.g., `C:\videos\street.mp4`)
  - URL (e.g., `https://...`)
- **OUTPUT** - Output type (default: `file`)
  - `file` - Save to MP4 with watermark
  - `display` - Display video with overlay
  - `fps` - Benchmark mode (no display)
  - `json` - Export metadata to JSON
  - `display-and-json` - Display and export
  - `jpeg` - Save frames as JPEG sequence
- **JSON_FILE** - JSON output filename (default: `output.json`)
- **BENCHMARK_SINK** - Optional GStreamer element to insert after decode (default: empty)
  - Example: `" ! identity eos-after=100"` - Process only first 100 frames
  - Example: `" ! identity eos-after=1000"` - Process only first 1000 frames
  - Useful for testing/benchmarking with limited frame count

## Examples

### Use default settings (Inception ResNet V2, CPU, Pexels video, save to file)
```powershell
.\instance_segmentation.bat
```

### ResNet50 on GPU with display
```powershell
.\instance_segmentation.bat mask_rcnn_resnet50_atrous_coco GPU "C:\videos\street.mp4" display
```

### Export to JSON
```powershell
.\instance_segmentation.bat mask_rcnn_inception_resnet_v2_atrous_coco CPU "C:\videos\street.mp4" json segmentation.json
```

### Export segmentation masks as JPEG sequence
```powershell
.\instance_segmentation.bat mask_rcnn_resnet50_atrous_coco GPU "C:\videos\street.mp4" jpeg
```

### Benchmark FPS on NPU
```powershell
.\instance_segmentation.bat mask_rcnn_resnet50_atrous_coco NPU "C:\videos\street.mp4" fps
```

### Process only first 100 frames (for testing)
```powershell
.\instance_segmentation.bat mask_rcnn_inception_resnet_v2_atrous_coco CPU "C:\videos\street.mp4" json output.json " ! identity eos-after=100"
```

## Output

The model outputs:
- **Bounding boxes** - Object locations
- **Class labels** - Object categories (person, car, etc.)
- **Instance masks** - Pixel-level segmentation masks
- **Confidence scores** - Detection confidence

COCO classes include: person, bicycle, car, motorcycle, airplane, bus, train, truck, boat, traffic light, fire hydrant, stop sign, parking meter, bench, bird, cat, dog, horse, sheep, cow, elephant, bear, zebra, giraffe, backpack, umbrella, handbag, tie, suitcase, frisbee, skis, snowboard, sports ball, kite, baseball bat, baseball glove, skateboard, surfboard, tennis racket, bottle, wine glass, cup, fork, knife, spoon, bowl, banana, apple, sandwich, orange, broccoli, carrot, hot dog, pizza, donut, cake, chair, couch, potted plant, bed, dining table, toilet, TV, laptop, mouse, remote, keyboard, cell phone, microwave, oven, toaster, sink, refrigerator, book, clock, vase, scissors, teddy bear, hair drier, toothbrush.

## Pipeline Architecture

```mermaid
graph LR
    A[filesrc/urisourcebin] --> B[decodebin3]
    B --> C[gvadetect]
    C --> D[queue]
    D --> E{OUTPUT}
    
    E -->|file| F1[d3d11convert]
    F1 --> F2[gvawatermark]
    F2 --> F3[gvafpscounter]
    F3 --> F4[d3d11h264enc]
    F4 --> F5[h264parse]
    F5 --> F6[mp4mux]
    F6 --> F7[filesink]
    
    E -->|display| G1[d3d11convert]
    G1 --> G2[gvawatermark]
    G2 --> G3[videoconvertscale]
    G3 --> G4[gvafpscounter]
    G4 --> G5[autovideosink]
    
    E -->|fps| H1[gvafpscounter]
    H1 --> H2[fakesink]
    
    E -->|json| I1[gvametaconvert]
    I1 --> I2[gvametapublish]
    I2 --> I3[fakesink]
    
    E -->|display-and-json| J1[d3d11convert]
    J1 --> J2[gvawatermark]
    J2 --> J3[gvametaconvert]
    J3 --> J4[gvametapublish]
    J4 --> J5[videoconvert]
    J5 --> J6[gvafpscounter]
    J6 --> J7[autovideosink]
    
    E -->|jpeg| K1[d3d11convert]
    K1 --> K2[gvawatermark]
    K2 --> K3[jpegenc]
    K3 --> K4[multifilesink]
```

## Performance Tips

1. **Use ResNet50 model** for faster inference
2. **GPU device** provides best performance
3. **Lower input resolution** reduces processing time
4. **Preprocessing backend**: Use `d3d11` for GPU, `opencv` for CPU

## See also
* [Windows Samples overview](../../../README.md)
* [Linux Instance Segmentation Sample](../../../../gstreamer/gst_launch/instance_segmentation/README.md)
