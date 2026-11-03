# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Convert nuScenes scenes into per-sensor image/point-cloud sequences.

GStreamer reads sensors with multifilesrc, which needs sequentially numbered
files, while nuScenes names files by log and timestamp and links them through
its JSON tables. For every requested scene this writes:

    <out>/<scene>/CAM_FRONT/%06d.jpg ... CAM_BACK_RIGHT/%06d.jpg   (symlinks)
    <out>/<scene>/LIDAR_TOP/%06d.bin    float32 [x, y, z, intensity] per point
    <out>/<scene>/calibration.json      sensor topology + static calibration
    <out>/<scene>/calibration_lidar_only.json  the same rig without cameras
    <out>/<scene>/annotations.json      ground-truth boxes in the lidar frame
    <out>/<scene>/manifest.csv          frame index -> sample token, timestamps

Only key frames (the 2 Hz annotated samples) are written, so frame N of every
sensor belongs to the same nuScenes sample and the streams line up by index.

The point clouds drop the 5th nuScenes column (the laser ring index). BEVFusion
overwrites it with the sweep time offset, which is 0 for a key frame, so the
consumer appends a zero column instead.

Calibration is static per scene: camera -> ego -> lidar from calibrated_sensor,
ignoring ego motion between each camera's exposure and the lidar timestamp
(up to ~45 ms). nuScenes keeps one calibrated_sensor per sensor per scene.
"""

import argparse
import csv
import json
import os
import sys
from pathlib import Path

import numpy as np

# Camera order of the BEVFusion model: mux pad i carries camera slot i.
CAMERAS = ['CAM_FRONT', 'CAM_FRONT_RIGHT', 'CAM_FRONT_LEFT',
           'CAM_BACK', 'CAM_BACK_LEFT', 'CAM_BACK_RIGHT']
LIDAR = 'LIDAR_TOP'
LIDAR_PAD = len(CAMERAS)


def quat_to_matrix(q):
    """nuScenes quaternion [w, x, y, z] -> 3x3 rotation matrix."""
    w, x, y, z = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def transform(rotation, translation):
    """4x4 homogeneous transform from a nuScenes rotation/translation record."""
    m = np.eye(4)
    m[:3, :3] = quat_to_matrix(rotation)
    m[:3, 3] = translation
    return m


class NuScenes:
    """The handful of nuScenes tables this conversion needs."""

    def __init__(self, root, version):
        self.root = Path(root)
        meta = self.root / version

        def load(name):
            with open(meta / f'{name}.json') as f:
                return json.load(f)

        self.scenes = load('scene')
        self.samples = {r['token']: r for r in load('sample')}
        self.calib = {r['token']: r for r in load('calibrated_sensor')}
        self.poses = {r['token']: r for r in load('ego_pose')}
        self.categories = {r['token']: r['name'] for r in load('category')}
        self.instances = {r['token']: r for r in load('instance')}
        channel = {r['token']: r['channel'] for r in load('sensor')}

        # sample token -> {channel: key-frame sample_data record}
        self.keyframes = {}
        for r in load('sample_data'):
            if r['is_key_frame']:
                ch = channel[self.calib[r['calibrated_sensor_token']]['sensor_token']]
                self.keyframes.setdefault(r['sample_token'], {})[ch] = r

        self.annotations = {}
        for r in load('sample_annotation'):
            self.annotations.setdefault(r['sample_token'], []).append(r)

    def scene(self, name):
        for s in self.scenes:
            if s['name'] == name:
                return s
        raise KeyError(f'scene {name!r} not found; available: '
                       f'{", ".join(s["name"] for s in self.scenes)}')

    def scene_samples(self, scene):
        token = scene['first_sample_token']
        while token:
            yield self.samples[token]
            token = self.samples[token]['next']


def sensor_to_ego(nusc, record):
    c = nusc.calib[record['calibrated_sensor_token']]
    return transform(c['rotation'], c['translation'])


def write_calibration(nusc, scene_dir, scene, first):
    """Static camera2lidar + intrinsics, keyed by mux pad index."""
    lidar2ego = sensor_to_ego(nusc, first[LIDAR])
    ego2lidar = np.linalg.inv(lidar2ego)
    cameras = {}
    for slot, ch in enumerate(CAMERAS):
        rec = first[ch]
        camera2lidar = ego2lidar @ sensor_to_ego(nusc, rec)
        intrinsics = np.array(nusc.calib[rec['calibrated_sensor_token']]['camera_intrinsic'])
        cameras[str(slot)] = {
            'name': ch,
            'slot': slot,
            'image_size': [rec['width'], rec['height']],
            'camera2lidar': [float(v) for v in camera2lidar.ravel()],
            'intrinsics': [float(v) for v in intrinsics.ravel()],
        }
    calibration = {
        'source': f'nuScenes {scene["name"]}, static calibrated_sensor (no ego-motion compensation)',
        'lidar': {'pad': LIDAR_PAD, 'name': LIDAR},
        'cameras': cameras,
    }
    with open(scene_dir / 'calibration.json', 'w') as f:
        json.dump(calibration, f, indent=2)
    with open(scene_dir / 'calibration_lidar_only.json', 'w') as f:
        json.dump({'source': calibration['source'], 'lidar': calibration['lidar']}, f, indent=2)


def boxes_in_lidar_frame(nusc, sample, lidar_rec):
    """Ground-truth boxes of a sample, in that sample's lidar frame.

    center is the box centre, size is [length, width, height] with length along
    the heading, and yaw is the heading angle about the lidar z axis.
    """
    pose = nusc.poses[lidar_rec['ego_pose_token']]
    global2lidar = np.linalg.inv(transform(pose['rotation'], pose['translation'])
                                 @ sensor_to_ego(nusc, lidar_rec))
    boxes = []
    for ann in nusc.annotations.get(sample['token'], []):
        box2global = transform(ann['rotation'], ann['translation'])
        box2lidar = global2lidar @ box2global
        w, l, h = ann['size']
        boxes.append({
            'category': nusc.categories[nusc.instances[ann['instance_token']]['category_token']],
            'center': [float(v) for v in box2lidar[:3, 3]],
            'size': [float(l), float(w), float(h)],
            'yaw': float(np.arctan2(box2lidar[1, 0], box2lidar[0, 0])),
            'num_lidar_pts': ann['num_lidar_pts'],
            'instance_token': ann['instance_token'],
        })
    return boxes


def convert_scene(nusc, name, out_root):
    scene = nusc.scene(name)
    scene_dir = Path(out_root) / name
    for ch in CAMERAS + [LIDAR]:
        (scene_dir / ch).mkdir(parents=True, exist_ok=True)

    rows, frames = [], []
    for index, sample in enumerate(nusc.scene_samples(scene)):
        frame = nusc.keyframes[sample['token']]
        missing = [ch for ch in CAMERAS + [LIDAR] if ch not in frame]
        if missing:
            raise RuntimeError(f'{name} sample {index}: no key frame for {missing}')
        if index == 0:
            write_calibration(nusc, scene_dir, scene, frame)

        for ch in CAMERAS:
            link = scene_dir / ch / f'{index:06d}.jpg'
            if link.is_symlink() or link.exists():
                link.unlink()
            os.symlink((nusc.root / frame[ch]['filename']).resolve(), link)

        points = np.fromfile(nusc.root / frame[LIDAR]['filename'], dtype=np.float32).reshape(-1, 5)
        np.ascontiguousarray(points[:, :4]).tofile(scene_dir / LIDAR / f'{index:06d}.bin')

        frames.append({'index': index, 'sample_token': sample['token'],
                       'boxes': boxes_in_lidar_frame(nusc, sample, frame[LIDAR])})
        rows.append([index, sample['token'], frame[LIDAR]['timestamp']] +
                    [frame[ch]['timestamp'] for ch in CAMERAS])

    with open(scene_dir / 'annotations.json', 'w') as f:
        json.dump({'frame': 'lidar', 'size': 'length, width, height', 'frames': frames}, f)
    with open(scene_dir / 'manifest.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['index', 'sample_token', 'lidar_timestamp_us'] +
                        [f'{ch.lower()}_timestamp_us' for ch in CAMERAS])
        writer.writerows(rows)
    print(f'{name}: {len(rows)} frames -> {scene_dir}')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--nuscenes-root', required=True,
                        help='directory holding samples/ and the version metadata folder')
    parser.add_argument('--version', default='v1.0-mini')
    parser.add_argument('--out', required=True, help='output directory')
    parser.add_argument('--scenes', nargs='*',
                        help='scene names (default: every scene in the version)')
    args = parser.parse_args()

    nusc = NuScenes(args.nuscenes_root, args.version)
    for name in args.scenes or [s['name'] for s in nusc.scenes]:
        convert_scene(nusc, name, args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
