# Live LiDAR Capture with the g3dlidarsrc Element

This directory contains a script demonstrating how to capture real-time point clouds from a physical LiDAR device using the `g3dlidarsrc` element.

Unlike `g3dlidarparse` (which replays recorded `.bin`/`.pcd` frames from disk), `g3dlidarsrc` is a **live source**: it receives UDP packets directly from the sensor, decodes them through the vendor SDK, and emits `application/x-lidar` buffers with attached `LidarMeta`. Its output is byte-for-byte compatible with `g3dlidarparse`, so the same downstream pipeline (`g3dinference`, etc.) works unchanged.

## How It Works

The script builds a GStreamer pipeline with `gst-launch-1.0`. In its simplest form it just verifies the device is streaming:

```
g3dlidarsrc ! fakesink
```

With `--output` but no model, it publishes source metadata as JSON:

```
g3dlidarsrc → gvametaconvert → gvametapublish → fakesink
```

With a model config it runs the full 3D detection pipeline and publishes inference metadata:

```
g3dlidarsrc → g3dinference → gvametaconvert → gvametapublish → fakesink
     ↓             ↓                ↓                ↓
 point clouds   detections    convert to JSON   publish to file
```

The current backend is **RoboSense** (via the header-only `rs_driver` SDK), and the only hardware validated so far is the **RSE1 (E1R)**.

## Prerequisites

### 1. Build DL Streamer with the RoboSense backend

`g3dlidarsrc` has **no compile-time dependency** on any vendor SDK. At runtime it `dlopen`s the backend library matching the config's `vendor` field — for RoboSense, `libg3dlidar_robosense.so`, which wraps the `rs_driver` SDK. That backend is built by the `g3dlidar_robosense` target, gated by the `ENABLE_LIDAR_ROBOSENSE` CMake option — **OFF by default**. Turn it on:

```bash
# From the DL Streamer root
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DENABLE_LIDAR_ROBOSENSE=ON
cmake --build build --target gst3delements g3dlidar_robosense -j"$(nproc)"
```

This produces:
- `build/intel64/Release/lib/gstreamer-1.0/libgst3delements.so` — the plugin (contains `g3dlidarsrc`)
- `build/intel64/Release/lib/libg3dlidar_robosense.so` — the RoboSense backend

> The first configure with `ENABLE_LIDAR_ROBOSENSE=ON` performs a one-time network fetch of the pinned `rs_driver` commit and applies the in-tree patches automatically. See the [element documentation](../../../../docs/user-guide/elements/g3dlidarsrc.md#vendor-backends) for the "bring your own rs_driver" alternative and for adding other vendors.

### 2. Make the plugin and backend discoverable

The element must be on `GST_PLUGIN_PATH`, and the backend must be on `LD_LIBRARY_PATH` (the element loads it by bare name):

```bash
export GST_PLUGIN_PATH="$PWD/build/intel64/Release/lib/gstreamer-1.0:${GST_PLUGIN_PATH:-}"
export LD_LIBRARY_PATH="$PWD/build/intel64/Release/lib:${LD_LIBRARY_PATH:-}"

# Verify the element is registered
gst-inspect-1.0 g3dlidarsrc
```

A standard `make install` places the backend under `lib/`, which `scripts/setup_dls_env.sh` already adds to `LD_LIBRARY_PATH`.

### 3. Connect and configure the LiDAR

- Physically connect the sensor and ensure your host has an IP on the same subnet.
- Confirm the device is emitting MSOP/DIFOP UDP packets (default ports `6699`/`7788`).
- Edit the config JSON to match your setup. A ready-made RSE1 config ships at
  [`configs/robosense_e1r_udp.json`](../../../../src/monolithic/gst/3d_elements/g3dlidarsrc/configs/robosense_e1r_udp.json); the script uses it by default.

  ```json
  {
    "vendor": "robosense",
    "model": "RSE1",
    "transport": { "type": "udp", "bind_address": "0.0.0.0" },
    "params": {}
  }
  ```

  See the [element documentation](../../../../docs/user-guide/elements/g3dlidarsrc.md#configuration) for the full schema (ports, range clipping, timestamp policy, etc.).

## Running the Sample

**Usage:**
```bash
./g3dlidarsrc.sh [OPTIONS]
```

**Options:**
- `-c, --config PATH`: LiDAR JSON config (vendor/model/transport). Default: the bundled RSE1 config.
- `-n, --ntp-sync BOOL`: `true` = LiDAR hardware clock, `false` = pipeline clock (default).
- `-t, --timeout USEC`: no-data timeout in microseconds (`0` = disabled). Default: `5000000` (5 s).
- `-i, --stream-id ID`: stream identifier written into `LidarMeta`. Default: `0`.
- `-m, --model-config PATH`: optional `g3dinference` config; enables the 3D detection stage.
- `-o, --output PATH`: optional JSON output file.
   - Without `--model-config`: publishes source metadata.
   - With `--model-config`: publishes inference metadata (including tensors).
- `-h, --help`: show help.

**Examples:**

1. **Basic capture** — confirm the device is streaming:
   ```bash
   ./g3dlidarsrc.sh
   ```

2. **Bind to a specific NIC + use the LiDAR clock:**
   ```bash
   ./g3dlidarsrc.sh --config /path/to/lidar.json --ntp-sync true
   ```

3. **Publish source metadata to JSON (no model):**
   ```bash
   ./g3dlidarsrc.sh --output source_meta.json
   ```

4. **Full 3D detection with JSON export** (needs a model, see the [g3dinference sample](../g3dinference/README.md)):
   ```bash
   ./g3dlidarsrc.sh --model-config pointpillars_ov_config.json --output detections.json
   ```

5. **Verbose element logging:**
   ```bash
   GST_DEBUG=g3dlidarsrc:5 ./g3dlidarsrc.sh
   ```

## Expected Output

- **With a streaming device**: point-cloud frames flow through the pipeline. With `GST_DEBUG=g3dlidarsrc:5` you'll see per-frame logs (point count, timestamp). With `--output` only, source metadata is written to JSON. With `--model-config` + `--output`, per-frame inference metadata/detections are written to JSON.
- **Without a device**: after `timeout` microseconds with no data, the element fails the pipeline with a `RESOURCE/READ` error. **This is expected** and confirms the element started and was waiting for data — it did not crash.

## Troubleshooting

**`No such element or plugin 'g3dlidarsrc'`**
- The plugin is not on `GST_PLUGIN_PATH`. Build `gst3delements` and export the path (Prerequisite 2).

**`Failed to load backend 'libg3dlidar_robosense.so'`** (at pipeline start)
- The backend is either not built (`ENABLE_LIDAR_ROBOSENSE` was OFF) or not on `LD_LIBRARY_PATH`. See Prerequisites 1–2.

**`Timeout receiving data from LiDAR device`**
- No packets arrived within `timeout`. Check: device powered/connected, host IP on the sensor's subnet, `bind_address` and `msop_port`/`difop_port` in the config, and firewall rules. Raise `--timeout` or set `0` while debugging.

**`Unsupported vendor` / `Unsupported transport`**
- The config `vendor` must be `robosense` and `transport.type` must be `udp` (the only implemented paths today).

## See also
* [Samples overview](../../README.md)
* [g3dlidarsrc element documentation](../../../../docs/user-guide/elements/g3dlidarsrc.md)
* [LiDAR Parse Sample](../g3dlidarparse/README.md) — offline replay counterpart
* [PointPillars Inference with g3dinference](../g3dinference/README.md) — the downstream 3D detection stage
