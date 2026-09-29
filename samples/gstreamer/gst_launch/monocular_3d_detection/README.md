# Monocular 3D Object Detection (gvamono3d / MonoDETR)

This sample runs monocular 3D object detection with the `gvamono3d` element
(MonoDETR) and renders 3D cuboids with `gvawatermark3d`. It demonstrates the
full camera 3D pipeline: a single image plus its camera calibration in, typed
`GstAnalyticsCamera3DODMtd` metadata (location, dimensions, orientation) out.

```
filesrc (assembled mp4) ! decodebin3 ! videoconvert ! gvamono3d ! gvawatermark3d ! <sink>
```

## Data (downloaded on demand)

The sample fetches **front-camera frames and their intrinsics** from
[PandaSet](https://pandaset.org) (© Hesai & Scale AI, **CC BY 4.0**) at runtime,
using HTTP range requests so only a few MB are transferred — the 44 GB archive
is never downloaded and **no media is stored in this repository**. The frames
are then assembled into a short mp4 (`data/pandaset_<seq>.mp4`, ~10 fps) that the
pipeline runs on as video.

MonoDETR is KITTI-trained, and its learned depth heads expect KITTI's wide field
of view (~81° horizontal). PandaSet's front camera is much narrower (~52°), which
makes objects look ~1.8× larger and biases predicted depth toward the camera. To
make the sample work out of the box, the downloader **reprojects each frame onto
KITTI's canonical intrinsics** — a pinhole→pinhole resample (scale about the
principal point + pad) that places objects at KITTI pixel scale. The unseen
periphery becomes black borders (a narrower FOV can't fill KITTI's wider canvas).
It writes a matching `calibration.json` (KITTI `projection_matrix` P2 +
`image_size` 1242×375) and a `crop.env` (frame count) under `data/`.

Requires the `remotezip` and `Pillow` packages:

```bash
pip install remotezip Pillow
```

### Best results: native KITTI data

MonoDETR is trained on KITTI, so it performs best on genuine KITTI imagery — with
no reprojection, black borders, or domain-gap depth bias. To run on KITTI instead
of PandaSet:

1. Download the **KITTI 3D Object Detection** data from the official site (free
   registration required):
   <https://www.cvlibs.net/datasets/kitti/eval_object.php?obj_benchmark=3d>.
   Get *"left color images of object data set"* and *"camera calibration matrices
   of object data set"*.
2. Each image (`image_2/NNNNNN.png`, 1242×375) has a calibration file
   (`calib/NNNNNN.txt`) whose `P2:` row is the projection matrix.
3. Assemble the frames into an mp4 (or point the pipeline at your own clip) and
   pass the matching `calib/NNNNNN.txt` directly via the `calibration-file`
   property — `gvamono3d`/`gvawatermark3d` read the KITTI `.txt` P2 row natively.

Any other camera works too, as long as its calibration matches the frames;
cameras whose field of view differs greatly from KITTI's will show the depth bias
described above unless reprojected as this sample does.

## Model

The MonoDETR OpenVINO IR is **not** downloaded automatically. Create a virtual
environment with the conversion dependencies, then export the IR once:

```bash
python3 -m venv .monodetr-venv
source .monodetr-venv/bin/activate
pip install -r ../../../../scripts/download_models/requirements_convert_monodetr.txt
python3 ../../../../scripts/download_models/convert_monodetr.py --outdir .
```

Then pass its path as the first argument (or place it at
`${MODELS_PATH}/public/monodetr/FP16/monodetr.xml`).

## Run

```bash
./monocular_3d_detection.sh [MODEL] [DEVICE] [OUTPUT] [THRESHOLD] [SEQUENCE] [NUM_FRAMES]
```

| Argument   | Default                                             | Notes |
|------------|-----------------------------------------------------|-------|
| MODEL      | `${MODELS_PATH}/public/monodetr/FP16/monodetr.xml`  | Path to `monodetr.xml` |
| DEVICE     | `GPU`                                               | `CPU`, `GPU`, `NPU` (GPU auto-applies `EXECUTION_MODE_HINT=ACCURACY`) |
| OUTPUT     | `file`                                              | `file`, `display`, `json`, `display-and-json` |
| THRESHOLD  | `0.3`                                               | Detection confidence threshold |
| SEQUENCE   | `001`                                               | PandaSet sequence id |
| NUM_FRAMES | `79`                                                | Number of frames to fetch/assemble into the clip |

Examples:

```bash
# Render an annotated MP4 on GPU
./monocular_3d_detection.sh /path/to/monodetr.xml GPU file

# Write 3D detections as JSON lines (output.json)
./monocular_3d_detection.sh /path/to/monodetr.xml CPU json
```

## Attribution

This sample uses data from PandaSet by Hesai and Scale AI, licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The data is retrieved
on demand and is not redistributed as part of DL Streamer.
