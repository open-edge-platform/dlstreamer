# PointPillars Inference with g3dinference

This sample demonstrates a complete LiDAR-only 3D detection pipeline based on `g3dlidarparse` and `g3dinference`.

The sample is split into two scripts:

- locate or fetch the PointPillars source tree from `openvino_contrib`
- copy the sample point cloud (`000002.bin`) from that source tree
- copy the PointPillars OpenVINO IR files into `MODELS_PATH/public/pointpillars/FP16`
- build the required OpenVINO extension library `libov_pointpillars_extensions.so`
- generate a runtime JSON config with absolute paths to the extension and IR files

`g3dinference_prepare.sh` handles the preparation steps above.

`g3dinference.sh` only runs the pipeline and expects the prepared config to already exist.

## Pipeline

The default pipeline is:

```bash
gst-launch-1.0 -e \
	filesrc location=".../000002.bin" ! \
	application/octet-stream ! \
	g3dlidarparse ! \
	g3dinference config=".../pointpillars_ov_config.json" device=CPU score-threshold=-1 ! \
	gvametaconvert add-tensor-data=true format=json json-indent=2 ! \
	gvametapublish file-format=2 file-path=".../g3dinference_output.json" ! \
	fakesink
```

`g3dlidarparse` converts the raw point cloud into `application/x-lidar` with `LidarMeta`. `g3dinference` loads the PointPillars runtime from the generated JSON config, runs inference, and attaches tensor metadata. `gvametaconvert` and `gvametapublish` serialize that metadata to JSON.

## Prerequisites

You need:

- a DL Streamer build where `g3dlidarparse`, `g3dinference`, `gvametaconvert`, and `gvametapublish` are available
- `gst-launch-1.0` and `gst-inspect-1.0`
- `git`, `cmake`, `make`, and a C++ compiler
- a Python interpreter with the `openvino` package installed, because the PointPillars extension build script resolves OpenVINO through Python

Quick checks:

```bash
gst-inspect-1.0 g3dlidarparse
gst-inspect-1.0 g3dinference
python3 -c "import openvino"
```

## Usage

Prepare everything:

```bash
./g3dinference_prepare.sh
```

Run the default single-frame sample after preparation:

```bash
./g3dinference.sh
```


## Asset Locations

By default the script uses:

- models: `MODELS_PATH` if set, otherwise `../models` next to the `dlstreamer` repo
- sample data: `samples/gstreamer/gst_launch/g3dinference/.pointpillars/data/000002.bin`
- generated config: `samples/gstreamer/gst_launch/g3dinference/.pointpillars/config/pointpillars_ov_config.json`
- JSON output: `samples/gstreamer/gst_launch/g3dinference/.pointpillars/g3dinference_output.json`

The generated config points to:

- `libov_pointpillars_extensions.so` built from `openvino_contrib/modules/3d/pointPillars/ov_extensions`
- `pointpillars_ov_pillar_layer.xml`
- `pointpillars_ov_nn.xml`
- `pointpillars_ov_postproc.xml`

If `POINTPILLARS_ROOT` is not set, `g3dinference_prepare.sh` first looks for a sibling checkout at `../openvino_contrib/modules/3d/pointPillars`. If that is missing, it performs a sparse clone of `openvino_contrib` into the sample cache and builds the extension from there.

## Notes

- `g3dinference_prepare.sh` uses the PointPillars source tree as the single source of truth for sample data, pretrained IR files, and the extension build script.
- `g3dinference_prepare.sh` writes a fresh runtime JSON config with absolute paths, so it does not depend on the upstream `pointpillars_ov_config.json` having usable paths on your machine.
- If you already downloaded the IR files with `samples/download_public_models.sh`, the preparation script reuses them from `MODELS_PATH/public/pointpillars/FP16`.

## See Also

- [g3dinference element documentation](../../../../docs/user-guide/elements/g3dinference.md)
- [Elements overview](../../../../docs/user-guide/elements/elements.md)
