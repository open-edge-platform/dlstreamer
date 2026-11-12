These are shell script and Python implementations designed to run DL Streamer and DeepStream pipelines on available Intel and NVIDIA hardware.

## Model Preparation

The sample does not download models or clone repositories. Prepare models for the platform(s) you intend to run before starting either entrypoint. Both implementations require the same files.

### DL Streamer

The example DL Streamer pipeline uses `yolov8_license_plate_detector` and `ch_PP-OCRv4_rec_infer`. Prepare compatible models with the general [`download_other_models.py`](../../../../scripts/download_models/README.md) helper; its README describes setup and supported models. Replacing the examples may require updating the pipeline paths and configuration. The sample expects:

- `public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml`
- `public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml`

### DeepStream / TAO

For NVIDIA hardware, prepare NVIDIA's [`deepstream_tao_apps`](https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps) checkout under this directory and place its TAO model artifacts at these paths:

- `deepstream_tao_apps/models/trafficcamnet/resnet18_trafficcamnet_pruned.onnx`
- `deepstream_tao_apps/models/trafficcamnet/resnet18_trafficcamnet_pruned_int8.txt`
- `deepstream_tao_apps/models/LPD_us/LPDNet_usa_pruned_tao5.onnx`
- `deepstream_tao_apps/models/LPD_us/usa_cal_10.1.0.bin`
- `deepstream_tao_apps/models/LPD_us/usa_lpd_label.txt`
- `deepstream_tao_apps/models/LPR_us/us_lprnet_baseline18_deployable.onnx`
- `deepstream_tao_apps/models/LPR_us/labels_us.txt`
- `deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/dict_us.txt`

Use NVIDIA's TAO model setup instructions to obtain the artifacts. The sample checks for them before launching; it builds the custom LPR parser from the checkout and copies `dict_us.txt` to `dict.txt` inside the working directory.

The Python entrypoint is `coexistence_dls_and_ds.py`; the shell entrypoint is `coexistence_dls_and_ds.sh`. Both accept an input source, `LPR`, and an output path. An HTTPS input is streamed by GStreamer; it is not downloaded to a local video file.

[DL Streamer and DeepStream Coexistence](../../../../docs/user-guide/dev_guide/dlstreamer-deepstream-coexistence.md)