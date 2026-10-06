#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample uses a few front-camera frames + intrinsics from PandaSet
# (https://pandaset.org), (c) Hesai & Scale AI, distributed under CC BY 4.0.
# The data is fetched on demand and is NOT redistributed with this repository.
# ==============================================================================
"""Download a few PandaSet front-camera frames + calibration for the gvamono3d sample.

Only the handful of files actually needed are pulled from the public CC BY 4.0
PandaSet archive on Hugging Face using HTTP range requests (a few MB), so the
44 GB archive is never downloaded and nothing is stored in the repository.

Outputs (under --out):
  frames/0000.jpg ...   frames reprojected to KITTI intrinsics, fed to the pipeline
  calibration.json      KITTI P2 + image size for gvamono3d / gvawatermark3d
  crop.env              NUM_FRAMES (actual frame count) consumed by the shell sample
"""

import argparse
import io
import json
import sys
from pathlib import Path
from urllib.parse import urlparse

# Public CC BY 4.0 mirror of PandaSet (single archive, range-readable).
PANDASET_ZIP_URL = "https://huggingface.co/datasets/georghess/pandaset/resolve/main/pandaset.zip?download=true"
_ALLOWED_HOST = "huggingface.co"

# MonoDETR is KITTI-trained, so its learned depth heads expect KITTI's ~81deg-HFOV geometry. PandaSet's
# front camera is much narrower (~52deg), making objects look ~1.8x larger and biasing predicted depth
# toward the camera. We therefore reproject each frame onto KITTI's canonical intrinsics (a pinhole->
# pinhole resample: scale about the principal point + pad), so objects land at KITTI pixel scale. The
# unseen periphery becomes black borders (PandaSet's narrower FOV can't fill KITTI's wider canvas).
_KITTI_W, _KITTI_H = 1242, 375
_KITTI_FX = _KITTI_FY = 721.5377
_KITTI_CX, _KITTI_CY = 609.5593, 172.854
# Canonical KITTI cam2 projection matrix P2 (includes the small stereo baseline term), the exact form
# MonoDETR was trained with.
_KITTI_P2 = [
    [_KITTI_FX, 0.0, _KITTI_CX, 44.85728],
    [0.0, _KITTI_FY, _KITTI_CY, 0.2163791],
    [0.0, 0.0, 1.0, 0.002745884],
]


def _reproject_to_kitti(img, fx, fy, cx, cy):
    """Resample a source frame onto KITTI's canonical intrinsics (pinhole->pinhole scale + pad).

    Maps each output (KITTI) pixel back to the source ray it shares, so a 3D point lands where a KITTI
    camera would image it. Output pixels whose ray falls outside the source FOV become black.
    """
    from PIL import Image  # pylint: disable=import-outside-toplevel

    # AFFINE coeffs map OUTPUT->INPUT: input = (a*x + c, e*y + f), derived from equal ray angles
    # (u - c*)/f* being shared between the two pinhole cameras.
    a = fx / _KITTI_FX
    c = cx - fx * _KITTI_CX / _KITTI_FX
    e = fy / _KITTI_FY
    f = cy - fy * _KITTI_CY / _KITTI_FY
    return img.transform((_KITTI_W, _KITTI_H), Image.Transform.AFFINE, (a, 0.0, c, 0.0, e, f),
                         resample=Image.Resampling.BILINEAR, fillcolor=(0, 0, 0))


def main():  # pylint: disable=too-many-locals
    """Fetch frames + intrinsics, reproject to KITTI intrinsics, and write frames/, calibration.json."""
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--seq", default="001", help="PandaSet sequence id (default: 001)")
    parser.add_argument("--num", type=int, default=10, help="number of frames (default: 10)")
    args = parser.parse_args()

    if urlparse(PANDASET_ZIP_URL).hostname != _ALLOWED_HOST:
        raise SystemExit(f"Refusing non-allowlisted host in {PANDASET_ZIP_URL}")

    try:
        from remotezip import RemoteZip  # pylint: disable=import-outside-toplevel
    except ImportError:
        raise SystemExit(
            "This sample needs the 'remotezip' package to fetch PandaSet frames on demand:\n"
            "    pip install remotezip") from None
    try:
        from PIL import Image  # pylint: disable=import-outside-toplevel
    except ImportError:
        raise SystemExit(
            "This sample needs the 'Pillow' package to reproject frames to KITTI intrinsics:\n"
            "    pip install Pillow") from None

    out_dir = Path(args.out)
    frames_dir = out_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)

    prefix = f"pandaset/{args.seq}/camera/front_camera/"
    print("[info] opening PandaSet archive index (range requests) ...")
    with RemoteZip(PANDASET_ZIP_URL) as archive:
        jpgs = sorted(n for n in archive.namelist() if n.startswith(prefix) and n.endswith(".jpg"))
        if not jpgs:
            raise SystemExit(f"No front-camera frames found for sequence {args.seq}")
        jpgs = jpgs[:args.num]

        intr = json.loads(archive.read(prefix + "intrinsics.json"))
        fx, fy, cx, cy = float(intr["fx"]), float(intr["fy"]), float(intr["cx"]), float(intr["cy"])

        src_wh = None
        for idx, name in enumerate(jpgs):
            img = Image.open(io.BytesIO(archive.read(name))).convert("RGB")
            src_wh = img.size
            warped = _reproject_to_kitti(img, fx, fy, cx, cy)
            warped.save(frames_dir / f"{idx:04d}.jpg", quality=92)
            print(f"[info] fetched {name} -> frames/{idx:04d}.jpg (reprojected to {_KITTI_W}x{_KITTI_H})")

    calib = {
        "projection_matrix": _KITTI_P2,
        "image_size": [_KITTI_W, _KITTI_H],
    }
    (out_dir / "calibration.json").write_text(json.dumps(calib, indent=2))
    # crop.env carries the actual frame count plus the requested count (so the shell can detect a
    # changed --num and re-fetch); the old crop is subsumed by the reprojection.
    (out_dir / "crop.env").write_text(f"NUM_FRAMES={len(jpgs)}\nREQUESTED_FRAMES={args.num}\n")

    src_desc = f"{src_wh[0]}x{src_wh[1]}" if src_wh else "unknown"
    print(f"[info] {len(jpgs)} frames, source {src_desc} -> reprojected to KITTI {_KITTI_W}x{_KITTI_H}")
    print(f"[info] wrote {out_dir / 'calibration.json'} and {out_dir / 'crop.env'}")


if __name__ == "__main__":
    sys.exit(main())
