# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Convert nuScenes scenes into per-sensor image/point-cloud sequences.

GStreamer reads sensors with multifilesrc, which needs sequentially numbered
files, while nuScenes names files by log and timestamp and links them through
its JSON tables. For every requested scene this writes:

    <out>/<scene>/CAM_FRONT/%06d.jpg ... CAM_BACK_RIGHT/%06d.jpg   (hard links or copies)
    <out>/<scene>/LIDAR_TOP/%06d.bin    float32 [x, y, z, intensity] per point
    <out>/<scene>/calibration.json      sensor topology, calibration, per-frame table
    <out>/<scene>/calibration_lidar_only.json  the same rig without cameras
    <out>/<scene>/annotations.json      ground-truth boxes of the key frames, lidar frame
    <out>/<scene>/manifest.csv          frame index -> lidar/camera timestamps, sample token

Frames. The lidar turns at 20 Hz and the six cameras fire together, as the
beam sweeps past them, on 12 of every 20 turns. --frames all (the default)
writes every turn on which all six cameras fired, so every frame is a
camera+lidar frame, about 12 Hz. --frames keyframes writes only the 2 Hz
annotated samples, which are a subset of those. Frame N of every sensor is the
same turn, so the streams line up by index.

Point clouds. Each frame's point cloud is the current turn followed by the
previous --sweeps turns (default 9, as BEVFusion is trained), every one of
the 20 Hz turns, not only the ones written as frames. This is mmdet3d's
LoadPointsFromMultiSweeps: points of the older turns within 1 m of the sensor
are dropped, and each older turn is moved into the current lidar frame with
the ego poses at both timestamps. A frame with no older turn (the first of a
scene) repeats the current turn instead, as mmdet3d does. The 5th point
column BEVFusion reads is the time lag of the turn a point came from; nuScenes
stores the laser ring index there. It is dropped from the file, and the
per-frame table records how many points each turn contributed and its time
lag, so the consumer can rebuild it.

Calibration. Each camera exposes up to ~45 ms before the lidar timestamp,
while the vehicle moves. The top-level camera2lidar is the static mounting
(calibrated_sensor only). The per-frame table also gives, for every frame,
camera2lidar including the ego motion between the exposure and the lidar
timestamp (mmdet3d's obtain_sensor2top), which is what BEVFusion is trained on.
"""

import argparse
import csv
import json
import os
import shutil
import sys
from collections import OrderedDict
from pathlib import Path

import numpy as np

# Camera order of the BEVFusion model: mux pad i carries camera slot i.
CAMERAS = ['CAM_FRONT', 'CAM_FRONT_RIGHT', 'CAM_FRONT_LEFT',
           'CAM_BACK', 'CAM_BACK_LEFT', 'CAM_BACK_RIGHT']
LIDAR = 'LIDAR_TOP'
LIDAR_PAD = len(CAMERAS)

# A camera image belongs to a lidar turn when it was exposed within this much
# of where that camera usually fires relative to the lidar timestamp. Cameras
# fire at most once per 50 ms turn and jitter by a few ms.
CAMERA_MATCH_US = 15000
# mmdet3d LoadPointsFromMultiSweeps(remove_close=True) radius, metres.
CLOSE_RADIUS = 1.0


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
        self.sample_data = {r['token']: r for r in load('sample_data')}
        channel = {r['token']: r['channel'] for r in load('sensor')}

        # sample token -> {channel: key-frame sample_data record}
        self.keyframes = {}
        for r in self.sample_data.values():
            r['channel'] = channel[self.calib[r['calibrated_sensor_token']]['sensor_token']]
            if r['is_key_frame']:
                self.keyframes.setdefault(r['sample_token'], {})[r['channel']] = r

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

    def chain(self, record):
        """Every sample_data of record's sensor in its scene, in time order."""
        while record['prev']:
            record = self.sample_data[record['prev']]
        out = [record]
        while out[-1]['next']:
            out.append(self.sample_data[out[-1]['next']])
        return out

    def sensor_to_ego(self, record):
        c = self.calib[record['calibrated_sensor_token']]
        return transform(c['rotation'], c['translation'])

    def ego_to_global(self, record):
        p = self.poses[record['ego_pose_token']]
        return transform(p['rotation'], p['translation'])

    def sensor_to_global(self, record):
        return self.ego_to_global(record) @ self.sensor_to_ego(record)


def select_frames(nusc, scene, keyframes_only):
    """[(lidar record, {camera: record}, sample or None)] in time order."""
    samples = list(nusc.scene_samples(scene))
    for index, sample in enumerate(samples):
        missing = [ch for ch in CAMERAS + [LIDAR] if ch not in nusc.keyframes[sample['token']]]
        if missing:
            raise RuntimeError(f'{scene["name"]} sample {index}: no key frame for {missing}')
    sample_by_lidar = {nusc.keyframes[s['token']][LIDAR]['token']: s for s in samples}
    if keyframes_only:
        return [(nusc.keyframes[s['token']][LIDAR], {ch: nusc.keyframes[s['token']][ch] for ch in CAMERAS}, s)
                for s in samples]

    first = nusc.keyframes[samples[0]['token']]
    lidar_chain = nusc.chain(first[LIDAR])
    frames = []
    cams = {}
    for ch in CAMERAS:
        chain = nusc.chain(first[ch])
        times = np.array([r['timestamp'] for r in chain], dtype=np.int64)
        # Where this camera fires relative to the lidar timestamp, from the key frames.
        offset = int(np.median([nusc.keyframes[s['token']][ch]['timestamp'] -
                                nusc.keyframes[s['token']][LIDAR]['timestamp'] for s in samples]))
        cams[ch] = (chain, times, offset)
    for lidar in lidar_chain:
        matched = {}
        for ch, (chain, times, offset) in cams.items():
            target = lidar['timestamp'] + offset
            i = int(np.searchsorted(times, target))
            best = min((j for j in (i - 1, i) if 0 <= j < len(times)), key=lambda j: abs(times[j] - target))
            if abs(times[best] - target) <= CAMERA_MATCH_US:
                matched[ch] = chain[best]
        sample = sample_by_lidar.get(lidar['token'])
        if sample is not None:
            key = nusc.keyframes[sample['token']]
            wrong = [ch for ch in CAMERAS if matched.get(ch) is not key[ch]]
            if wrong:
                raise RuntimeError(f'{scene["name"]}: key frame {sample["token"]} matched other images for {wrong}')
        if len(matched) == len(CAMERAS):
            frames.append((lidar, matched, sample))
    return frames


class SweepLoader:
    """Multi-sweep point clouds as mmdet3d LoadPointsFromMultiSweeps builds them."""

    def __init__(self, nusc, sweeps):
        self.nusc = nusc
        self.sweeps = sweeps
        self.cache = OrderedDict()  # token -> close points of an older turn (float32 [N, 5])

    def raw(self, record):
        return np.fromfile(self.nusc.root / record['filename'], dtype=np.float32).reshape(-1, 5)

    def without_close(self, record):
        points = self.cache.get(record['token'])
        if points is None:
            points = self.raw(record)
            xy = np.abs(points[:, :2])
            points = points[~((xy[:, 0] < CLOSE_RADIUS) & (xy[:, 1] < CLOSE_RADIUS))]
            self.cache[record['token']] = points
            while len(self.cache) > self.sweeps + 2:
                self.cache.popitem(last=False)
        return points

    def load(self, lidar):
        """(float32 [N, 4] points, [(point count, time lag in s)] per turn, current turn first)."""
        current = self.raw(lidar)
        parts = [current[:, :4]]
        turns = [(len(current), 0.0)]
        older = []
        record = lidar
        while len(older) < self.sweeps and record['prev']:
            record = self.nusc.sample_data[record['prev']]
            older.append(record)
        if self.sweeps and not older:
            # mmdet3d pad_empty_sweeps: repeat the current turn.
            xy = np.abs(current[:, :2])
            pad = current[~((xy[:, 0] < CLOSE_RADIUS) & (xy[:, 1] < CLOSE_RADIUS)), :4]
            for _ in range(self.sweeps):
                parts.append(pad)
                turns.append((len(pad), 0.0))
        global_to_lidar = np.linalg.inv(self.nusc.sensor_to_global(lidar))
        ts = lidar['timestamp'] / 1e6
        for sweep in older:
            points = self.without_close(sweep)
            m = global_to_lidar @ self.nusc.sensor_to_global(sweep)
            moved = np.empty((len(points), 4), dtype=np.float32)
            moved[:, :3] = (points[:, :3].astype(np.float64) @ m[:3, :3].T + m[:3, 3]).astype(np.float32)
            moved[:, 3] = points[:, 3]
            parts.append(moved)
            # Same arithmetic as mmdet3d, so the float32 time column matches it bit for bit.
            turns.append((len(points), ts - sweep['timestamp'] / 1e6))
        return np.ascontiguousarray(np.concatenate(parts)), turns


def static_cameras(nusc, lidar, cams):
    """camera2lidar from the mounting only, + intrinsics, keyed by mux pad index."""
    ego2lidar = np.linalg.inv(nusc.sensor_to_ego(lidar))
    cameras = {}
    for slot, ch in enumerate(CAMERAS):
        rec = cams[ch]
        camera2lidar = ego2lidar @ nusc.sensor_to_ego(rec)
        intrinsics = np.array(nusc.calib[rec['calibrated_sensor_token']]['camera_intrinsic'])
        cameras[str(slot)] = {
            'name': ch,
            'slot': slot,
            'image_size': [rec['width'], rec['height']],
            'camera2lidar': [float(v) for v in camera2lidar.ravel()],
            'intrinsics': [float(v) for v in intrinsics.ravel()],
        }
    return cameras


def frame_camera2lidar(nusc, lidar, cams):
    """camera2lidar at each camera's exposure, keyed by mux pad index (mmdet3d obtain_sensor2top)."""
    global_to_lidar = np.linalg.inv(nusc.sensor_to_global(lidar))
    return {str(slot): {'camera2lidar': [float(v) for v in (global_to_lidar @ nusc.sensor_to_global(cams[ch])).ravel()]}
            for slot, ch in enumerate(CAMERAS)}


def write_calibration(path, header, frames):
    """JSON with the static part indented and one line per frame."""
    body = json.dumps(header, indent=2)
    lines = ',\n'.join('    ' + json.dumps(f, separators=(',', ':')) for f in frames)
    with open(path, 'w') as f:
        f.write(body[:-2] + ',\n  "frames": [\n' + lines + '\n  ]\n}\n')


def boxes_in_lidar_frame(nusc, sample, lidar_rec):
    """Ground-truth boxes of a sample, in that sample's lidar frame.

    center is the box centre, size is [length, width, height] with length along
    the heading, and yaw is the heading angle about the lidar z axis.
    """
    global2lidar = np.linalg.inv(nusc.sensor_to_global(lidar_rec))
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


def place_image(src, dst):
    """Hard link the image when possible (same file system), else copy it.

    Either way the sequence outlives the dataset, unlike a symlink.
    """
    if dst.exists() or dst.is_symlink():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


def convert_scene(nusc, name, out_root, keyframes_only, sweeps):
    scene = nusc.scene(name)
    scene_dir = Path(out_root) / name
    for ch in CAMERAS + [LIDAR]:
        (scene_dir / ch).mkdir(parents=True, exist_ok=True)

    selected = select_frames(nusc, scene, keyframes_only)
    loader = SweepLoader(nusc, sweeps)
    rows, annotated, table, table_lidar = [], [], [], []
    t0 = selected[0][0]['timestamp']
    for index, (lidar, cams, sample) in enumerate(selected):
        for ch in CAMERAS:
            place_image(nusc.root / cams[ch]['filename'], scene_dir / ch / f'{index:06d}.jpg')

        points, turns = loader.load(lidar)
        points.tofile(scene_dir / LIDAR / f'{index:06d}.bin')

        lidar_entry = {'points': len(points), 'sweeps': [{'points': n, 'time_lag': t} for n, t in turns]}
        table.append({'timestamp_us': lidar['timestamp'] - t0, 'lidar': lidar_entry,
                      'cameras': frame_camera2lidar(nusc, lidar, cams)})
        table_lidar.append({'timestamp_us': lidar['timestamp'] - t0, 'lidar': lidar_entry})
        if sample is not None:
            annotated.append({'index': index, 'sample_token': sample['token'],
                              'boxes': boxes_in_lidar_frame(nusc, sample, lidar)})
        rows.append([index, sample['token'] if sample else '', lidar['timestamp']] +
                    [cams[ch]['timestamp'] for ch in CAMERAS])

    source = (f'nuScenes {scene["name"]}; top-level camera2lidar: static calibrated_sensor; '
              f'frames[i].cameras: with ego motion to the lidar timestamp; '
              f'frames[i].lidar.sweeps: the turns in LIDAR_TOP/%06d.bin, current first; '
              f'timestamp_us: lidar time since frame 0')
    lidar0, cams0, _ = selected[0]
    header = {'source': source, 'lidar': {'pad': LIDAR_PAD, 'name': LIDAR},
              'cameras': static_cameras(nusc, lidar0, cams0)}
    write_calibration(scene_dir / 'calibration.json', header, table)
    write_calibration(scene_dir / 'calibration_lidar_only.json',
                      {'source': source, 'lidar': header['lidar']}, table_lidar)
    with open(scene_dir / 'annotations.json', 'w') as f:
        json.dump({'frame': 'lidar', 'size': 'length, width, height', 'frames': annotated}, f)
    with open(scene_dir / 'manifest.csv', 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['index', 'sample_token', 'lidar_timestamp_us'] +
                        [f'{ch.lower()}_timestamp_us' for ch in CAMERAS])
        writer.writerows(rows)
    seconds = (selected[-1][0]['timestamp'] - t0) / 1e6
    print(f'{name}: {len(rows)} frames ({len(annotated)} annotated) over {seconds:.1f} s, '
          f'{len(rows) / max(seconds, 1e-9):.1f} Hz -> {scene_dir}')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--nuscenes-root', required=True,
                        help='directory holding samples/, sweeps/ and the version metadata folder')
    parser.add_argument('--version', default='v1.0-mini')
    parser.add_argument('--out', required=True, help='output directory')
    parser.add_argument('--scenes', nargs='*',
                        help='scene names (default: every scene in the version)')
    parser.add_argument('--frames', choices=['all', 'keyframes'], default='all',
                        help="'all': every lidar turn with all six cameras, ~12 Hz (default); "
                             "'keyframes': the 2 Hz annotated samples only")
    parser.add_argument('--sweeps', type=int, default=9,
                        help='older lidar turns accumulated into each point cloud (default 9; 0 = current only)')
    args = parser.parse_args()

    nusc = NuScenes(args.nuscenes_root, args.version)
    for name in args.scenes or [s['name'] for s in nusc.scenes]:
        convert_scene(nusc, name, args.out, args.frames == 'keyframes', args.sweeps)
    return 0


if __name__ == '__main__':
    sys.exit(main())
