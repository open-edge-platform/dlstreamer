# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Reference BEVFusion results for a sequence written by nuscenes_to_sequence.py.

Runs the Python BEVFusion runtime from openvino_contrib (openvino_bevfusion)
on exactly the inputs the g3dbevfusion element sees -- the numbered images,
the 4-float point clouds and calibration.json -- and stores what it produced,
so the element can be checked against it frame by frame.

Inputs are prepared the way the element prepares them: key frame only (no
sweeps, Δt = 0 appended as the 5th point column), static calibration. Two
modes are written: 'fusion' (6 cameras + lidar) and 'lidar' (camera BEV fed
as zeros, the element's lidar-only path).

JPEG decoding is not bit-identical between OpenCV and GStreamer's jpegdec +
videoconvert (chroma upsampling, IDCT, colour conversion), and the camera
branch is sensitive to it. --decoder gstreamer decodes with the same elements
as samples/.../g3dbevfusion.sh, so the element's fusion output can be compared
like for like; --decoder opencv (default) matches run_inference_standalone.py.

Output, per mode:  <out>/<mode>/boxes_%06d.npy   decoded boxes, float32 [N, 11]
                       columns: x, y, z_bottom, dx, dy, dz, yaw, vx, vy, score, class
                   <out>/<mode>/tensors_%06d.npz intermediate tensors (first
                       --tensor-frames frames only; they are ~50 MB each)
Requires BEVFUSION_ROOT = the openvino_bevfusion module directory.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

CAMERAS = ['CAM_FRONT', 'CAM_FRONT_RIGHT', 'CAM_FRONT_LEFT',
           'CAM_BACK', 'CAM_BACK_LEFT', 'CAM_BACK_RIGHT']
HEAD_OUTPUTS = ['center', 'height', 'dim', 'rot', 'vel', 'heatmap',
                'query_heatmap_score', 'dense_heatmap',
                'top_proposals_index', 'top_proposals_class']


def load_runtime():
    root = os.environ.get('BEVFUSION_ROOT')
    if not root:
        sys.exit('BEVFUSION_ROOT must point to openvino_contrib/modules/openvino_bevfusion')
    sys.path.insert(0, root)
    import run_inference_standalone as ris  # pylint: disable=import-outside-toplevel
    return Path(root), ris


def camera_info(scene_dir, calibration, index):
    """The per-camera record layout run_inference_standalone expects."""
    cams = {}
    for pad, cam in calibration['cameras'].items():
        c2l = np.array(cam['camera2lidar'], dtype=np.float64).reshape(4, 4)
        cams[cam['name']] = {
            'data_path': str(scene_dir / cam['name'] / f'{index:06d}.jpg'),
            'sensor2lidar_rotation': c2l[:3, :3],
            'sensor2lidar_translation': c2l[:3, 3],
            'cam_intrinsic': np.array(cam['intrinsics'], dtype=np.float64).reshape(3, 3),
        }
    missing = set(CAMERAS) - set(cams)
    if missing:
        raise ValueError(f'calibration.json has no entry for {sorted(missing)}')
    return {'cams': cams}


def decode_gstreamer(path):
    """BGR pixels as g3dbevfusion.sh decodes them."""
    import cv2  # pylint: disable=import-outside-toplevel
    height, width = cv2.imread(str(path)).shape[:2]
    with tempfile.NamedTemporaryFile(suffix='.bgr') as raw:
        subprocess.run(['gst-launch-1.0', '-q', 'filesrc', f'location={path}', '!', 'jpegparse', '!',
                        'jpegdec', 'idct-method=islow', '!', 'videoconvert', '!', 'video/x-raw,format=BGR', '!',
                        'filesink', f'location={raw.name}'], check=True)
        return np.fromfile(raw.name, dtype=np.uint8).reshape(height, width, 3)


def load_images_gstreamer(ris, info):
    """load_camera_images() with GStreamer decoding instead of cv2.imread."""
    import cv2  # pylint: disable=import-outside-toplevel
    images, augs = [], []
    for name in ris.CAMERA_NAMES:
        img = cv2.cvtColor(decode_gstreamer(info['cams'][name]['data_path']), cv2.COLOR_BGR2RGB)
        h, w = img.shape[:2]
        nw, nh = int(w * ris.RESIZE_LIM), int(h * ris.RESIZE_LIM)
        img = cv2.resize(img, (nw, nh))
        ch = max(0, nh - ris.IMAGE_SIZE[0])
        cw = max(0, (nw - ris.IMAGE_SIZE[1]) // 2)
        img = img[ch:ch + ris.IMAGE_SIZE[0], cw:cw + ris.IMAGE_SIZE[1]]
        aug = np.eye(4, dtype=np.float32)
        aug[0, 0] = aug[1, 1] = ris.RESIZE_LIM
        aug[0, 2], aug[1, 2] = -cw, -ch
        augs.append(aug)
        images.append(((img.astype(np.float32) / 255.0 - ris.IMG_MEAN) / ris.IMG_STD).transpose(2, 0, 1))
    return np.stack(images), np.stack(augs)


def load_points(scene_dir, index):
    xyzi = np.fromfile(scene_dir / 'LIDAR_TOP' / f'{index:06d}.bin', dtype=np.float32).reshape(-1, 4)
    return np.concatenate([xyzi, np.zeros((len(xyzi), 1), np.float32)], axis=1)


def infer_frame(ris, pipe, scene_dir, calibration, index, mode, decoder):
    info = camera_info(scene_dir, calibration, index)
    points = load_points(scene_dir, index)
    lidar_bev, _ = pipe.run_lidar(points)
    tensors = {'points': points, 'lidar_bev': lidar_bev}

    if mode == 'fusion':
        if decoder == 'gstreamer':
            images, aug = load_images_gstreamer(ris, info)
        else:
            images, aug = ris.load_camera_images(info, scene_dir)
        l2i, c2l, intr = ris.get_camera_transforms(info)
        depth_maps = ris.create_depth_maps(points, l2i, aug)
        camera_bev, _ = pipe.run_camera(images, depth_maps, aug, c2l, intr)
        tensors.update(images=images, depth_maps=depth_maps, camera_bev=camera_bev)
    else:
        camera_bev = np.zeros((1, 80, 360, 360), np.float32)

    req = pipe.requests['full_detection']
    import openvino as ov  # pylint: disable=import-outside-toplevel
    req.set_input_tensor(0, ov.Tensor(np.ascontiguousarray(camera_bev)))
    req.set_input_tensor(1, ov.Tensor(np.ascontiguousarray(lidar_bev)))
    req.infer()
    head = {name: req.get_output_tensor(i).data.copy() for i, name in enumerate(HEAD_OUTPUTS)}
    tensors.update({f'head_{k}': v for k, v in head.items()})

    boxes = ris.decode_detections(head, score_threshold=0.0)
    rows = [list(b['center']) + list(b['dims']) + [b['yaw']] + list(b['vel']) +
            [b['score'], b['label']] for b in boxes]
    return np.array(rows, dtype=np.float32).reshape(-1, 11), tensors


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--scene-dir', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--modes', nargs='+', default=['fusion', 'lidar'], choices=['fusion', 'lidar'])
    parser.add_argument('--frames', type=int, default=-1, help='number of frames (default: all)')
    parser.add_argument('--tensor-frames', type=int, default=3)
    parser.add_argument('--device', default='GPU')
    parser.add_argument('--decoder', default='opencv', choices=['opencv', 'gstreamer'],
                        help='JPEG decoder for the camera images (see above)')
    args = parser.parse_args()

    root, ris = load_runtime()
    # Intermediate tensors must be readable on the host, so keep every stage's
    # output in host memory instead of chaining them as GPU remote tensors.
    ris.BEVFusionInference._setup_gpu_sharing = lambda self: None
    pipe = ris.BEVFusionInference(str(root / 'openvino_model'), str(root / 'openvino_extensions'), args.device)

    with open(args.scene_dir / 'calibration.json') as f:
        calibration = json.load(f)
    count = len(list((args.scene_dir / 'LIDAR_TOP').glob('*.bin')))
    if args.frames >= 0:
        count = min(count, args.frames)

    for mode in args.modes:
        out = args.out / mode
        out.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            boxes, tensors = infer_frame(ris, pipe, args.scene_dir, calibration, index, mode, args.decoder)
            np.save(out / f'boxes_{index:06d}.npy', boxes)
            if index < args.tensor_frames:
                np.savez(out / f'tensors_{index:06d}.npz', **tensors)
            top = boxes[0] if len(boxes) else None
            print(f'{mode} {index:3d}: {len(boxes)} boxes, '
                  f'score>0.3: {int((boxes[:, 9] > 0.3).sum()) if len(boxes) else 0}, '
                  f'top score {top[9] if top is not None else 0:.3f}', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
