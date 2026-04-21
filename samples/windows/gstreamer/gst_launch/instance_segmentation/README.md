# Instance Segmentation Sample (Windows)

This sample demonstrates instance segmentation using Mask R-CNN models on Windows.

## How It Works

The sample builds a GStreamer pipeline using:
- `filesrc` or `urisourcebin` for input
- `decodebin3` for video decoding
- [gvadetect](https://dlstreamer.github.io/elements/gvadetect.html) for instance segmentation
- [gvawatermark](https://dlstreamer.github.io/elements/gvawatermark.html) for mask and bounding box visualization
- `d3d11convert` for D3D11-accelerated processing

## Models

Supports two Mask R-CNN variants from TensorFlow Model Zoo:
- **mask_rcnn_inception_resnet_v2_atrous_coco** - Higher accuracy
- **mask_rcnn_resnet50_atrous_coco** - Faster inference

Both models trained on COCO dataset (80 object classes).

> **NOTE**: Run `download_public_models.bat` before using this sample.

## Environment Variables

```batch
set MODELS_PATH=C:\models
```

## Running

```batch
instance_segmentation.bat [MODEL] [DEVICE] [INPUT] [OUTPUT] [JSON_FILE]
```

Arguments:
- **MODEL** (optional) - Model name (default: mask_rcnn_inception_resnet_v2_atrous_coco)
- **DEVICE** (optional) - Inference device (default: CPU). Supported: CPU, GPU, NPU
- **INPUT** (optional) - Input source (default: Pexels video URL)
- **OUTPUT** (optional) - Output type (default: file)
  - `file` - Save to MP4
  - `display` - Show video
  - `fps` - Benchmark
  - `json` - Export metadata
  - `display-and-json` - Both
  - `jpeg` - Save frames as JPEG sequence
- **JSON_FILE** (optional) - JSON output filename (default: output.json)

## Examples

### Default execution (Inception ResNet V2, CPU)
```batch
instance_segmentation.bat
```

### ResNet50 on GPU
```batch
instance_segmentation.bat mask_rcnn_resnet50_atrous_coco GPU C:\videos\street.mp4 display
```

### Export segmentation masks as JPEG
```batch
instance_segmentation.bat mask_rcnn_resnet50_atrous_coco GPU C:\videos\street.mp4 jpeg
```

### Benchmark performance
```batch
instance_segmentation.bat mask_rcnn_resnet50_atrous_coco GPU C:\videos\street.mp4 fps
```

## Output

The model outputs:
- **Bounding boxes** - Object locations
- **Class labels** - Object categories (person, car, etc.)
- **Instance masks** - Pixel-level segmentation masks
- **Confidence scores** - Detection confidence

COCO classes include: person, bicycle, car, motorcycle, airplane, bus, train, truck, boat, traffic light, fire hydrant, stop sign, parking meter, bench, bird, cat, dog, horse, sheep, cow, elephant, bear, zebra, giraffe, backpack, umbrella, handbag, tie, suitcase, frisbee, skis, snowboard, sports ball, kite, baseball bat, baseball glove, skateboard, surfboard, tennis racket, bottle, wine glass, cup, fork, knife, spoon, bowl, banana, apple, sandwich, orange, broccoli, carrot, hot dog, pizza, donut, cake, chair, couch, potted plant, bed, dining table, toilet, TV, laptop, mouse, remote, keyboard, cell phone, microwave, oven, toaster, sink, refrigerator, book, clock, vase, scissors, teddy bear, hair drier, toothbrush.

## Performance Tips

1. **Use ResNet50 model** for faster inference
2. **GPU device** provides best performance
3. **Lower input resolution** reduces processing time
4. **Preprocessing backend**: Use `d3d11` for GPU, `opencv` for CPU

## Troubleshooting

### Model not found
Download public models first:
```batch
cd samples\windows
download_public_models.bat
```

### Low FPS
- Switch to ResNet50 model
- Use GPU device
- Reduce video resolution

### Memory issues
- Use CPU device with smaller batch size
- Close other applications
