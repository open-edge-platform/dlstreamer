# g3dlidarsrc

Captures real-time point clouds from a physical LiDAR device and emits `application/x-lidar` buffers with attached `LidarMeta`. The element is a vendor-agnostic source: a single JSON configuration file selects the vendor SDK backend, the device model, and the transport (UDP today; USB and others reserved). Its output is byte-for-byte compatible with `g3dlidarparse`, so it can drive `g3dinference` and the rest of a 3D pipeline without any downstream changes.

## Overview

The `g3dlidarsrc` element is the live-capture counterpart to `g3dlidarparse`. Where `g3dlidarparse` replays recorded BIN/PCD frames from disk, `g3dlidarsrc` receives UDP packets directly from a sensor, decodes them via the vendor SDK, and converts the resulting frames into dense point-cloud buffers.

It is implemented as a `GstPushSrc` live source. The current backend is **RoboSense** (via the header-only `rs_driver` SDK), and the only hardware tested so far is the **RSE1 (E1R)**. Other RoboSense models that `rs_driver` advertises support for (RS16, RS32, RS128, RSM1, etc.) are not validated here. The element has **no compile-time dependency** on any vendor SDK: each vendor is a separate backend shared library that the element loads at runtime via `dlopen`, selected by the config's `vendor` field (see [Vendor backends](#vendor-backends) below).

Key operations:
- **Configuration-driven setup**: A JSON `config` file declares the `vendor`, `model`, and `transport`. 
- **Real-time capture and decode**: A vendor SDK thread receives and decodes raw packets; completed frames are queued and handed to the pipeline.
- **Point-cloud conversion**: Each frame is converted into a contiguous `float[x, y, z, intensity]` payload, identical in layout to `g3dlidarparse` output.
- **Metadata attachment**: Emits `LidarMeta` (point count, frame_id, timestamp, stream_id) on every buffer.
- **Timeout detection**: Fails the pipeline with a clear error if no data arrives within a configurable window.
- **Timestamp selection**: Chooses between the LiDAR hardware clock and the GStreamer pipeline clock via a single `ntp-sync` toggle.

## Properties

| Property  | Type               | Description                                                                                                                                                  | Default  |
|-----------|-------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|----------|
| config    | String             | Path to a JSON config file describing the LiDAR vendor, model and transport.  **Required.**                                                          | null     |
| ntp-sync  | Boolean            | Synchronize received streams to the LiDAR clock (TRUE = use LiDAR clock, FALSE = use pipeline clock from 0)  | false    |
| timeout   | Uint64 | Timeout in microseconds for receiving data (0 = no timeout)                          | 5000000 (5 s) |
| stream-id | Uint   | Stream identifier for this LiDAR source (used in metadata)    | 0        |


## Configuration

The `config` property points to a JSON file with a deliberately layered structure so that adding a new vendor or a new transport is a localized change. There are three layers: **vendor identity**, **transport** (how data arrives, vendor-neutral), and an open **passthrough** object for vendor-specific extras.

A key principle: `transport` only carries fields whose meaning is vendor-neutral (for UDP today, that is just the bind address). Anything that uses **vendor-specific terminology** — for example RoboSense's `dense_points` or `wait_for_difop` SDK options — belongs in `params`, not in `transport`.

### Example

```json
{
  "vendor": "robosense",
  "model": "RSE1",
  "transport": { "type": "udp", "bind_address": "192.168.1.100" },
  "params": {
    "msop_port": 6699,
    "difop_port": 7788,
    "max_distance": 80.0,
    "dense_points": true
  }
}
```

### Top-level fields

| Field       | Required | Description                                                                                                                                                                              |
|-------------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `vendor`    | Yes      | Vendor identity; selects the SDK backend. Currently only `robosense` is implemented.                                                                                                      |
| `model`     | Yes      | Device model within that vendor.                                                  |
| `transport` | Yes      | Object describing how data is transported (vendor-neutral). The `type` field is a discriminator that determines which sub-fields apply.                                                   |
| `params`    | No       | Free-form object for vendor/model-specific extras (port numbers, SDK options, ...). It is not interpreted by the generic parser and is passed through to the corresponding vendor backend. |

### UDP transport (`transport.type = "udp"`)

The UDP transport block contains only the fields that are meaningful to **any** UDP-based LiDAR vendor:

| Field          | Required | Default   | Description                                                                                                |
|----------------|----------|-----------|------------------------------------------------------------------------------------------------------------|
| `type`         | Yes      | —         | Transport discriminator. Must be `"udp"` for the implemented path.                                         |
| `bind_address` | No       | `0.0.0.0` | Local NIC IP to `bind()` the receive socket to (`0.0.0.0` = all interfaces). **Not the LiDAR device's IP.** |

> **Reserved transports:** `transport.type` values other than `udp` (for example `usb`) are recognized but not yet implemented. Such a config parses successfully but fails at pipeline start with an explicit error, rather than failing silently.


### RoboSense `params` (`vendor = "robosense"`)

When `vendor` is `robosense`, the `params` object can carry the following keys to override `rs_driver` defaults. Every key is **optional** — omit it to keep the SDK default.

Keys are parsed and validated by the RoboSense backend on startup: any unrecognized key (typos included) fails the pipeline with an error listing every accepted key.

The fields below are validated on the only model we have tested (**RSE1**). 

| Key               | Group   | Type    | Default  | Description                                                                                                                                                                                                                                                |
|-------------------|---------|---------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `msop_port`       | input   | int     | `6699`   | UDP port for **MSOP** packets (the point-cloud stream). Override only if your device has been reconfigured off its factory port. Must match the device's web-UI setting.                                                                                   |
| `difop_port`      | input   | int     | `7788`   | UDP port for **DIFOP** packets (device info, calibration, hardware clock — and on RSE1 the IMU payload). Same rule as above.                                                                                                                               |
| `socket_recv_buf` | input   | int     | `106496` | Size of the kernel receive buffer (`SO_RCVBUF`) in bytes. Raise it if you see dropped packets in `dmesg`/`netstat -su` under bursty load; otherwise leave alone.                                                                                            |
| `min_distance`    | decoder | number  | `0.0`    | Minimum point distance in **metres**. Points closer than this are dropped (`0.0` disables). Useful to cut returns from the sensor housing or a nearby mount.                                                                                               |
| `max_distance`    | decoder | number  | `0.0`    | Maximum point distance in **metres**. Points beyond this are dropped (`0.0` disables, i.e. keep the sensor's full range). Set this when you only care about a near field and want to shrink the per-frame point count for downstream cost.                |
| `dense_points`    | decoder | boolean | `false`  | If `true`, NaN/invalid points are removed and the output cloud is densely packed; if `false`, NaN points are kept (slot-aligned per channel/azimuth, easier to interpret as a 2D range image). Set `true` if downstream code expects compact point arrays. |
| `ts_first_point`  | decoder | boolean | `false`  | If `true`, the per-frame timestamp marks the **first** point in the frame; if `false`, the **last** point. Pick whichever convention your fusion stack uses; it shifts each frame's timestamp by one frame duration.                                       |
| `wait_for_difop`  | decoder | boolean | `true`   | If `true`, the SDK suppresses point clouds until it has seen at least one DIFOP packet (so calibration/temperature/clock info is available). Set to `false` only for debugging when you know DIFOP is missing and want raw points anyway.                  |




## Pipeline Examples

### Basic capture pipeline

```bash
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json ! fakesink
```

### Capture with 3D inference and JSON export

```bash
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json ! \
  g3dinference config=pointpillars_ov_config.json device=CPU ! \
  gvametaconvert add-tensor-data=true format=json json-indent=2 ! \
  gvametapublish file-format=2 file-path=pointpillars.json ! \
  fakesink
```

### Timestamp source selection

```bash
# Use the GStreamer pipeline clock (default; timestamps start from 0)
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json ntp-sync=false ! g3dinference ! ...

# Use the LiDAR hardware clock (absolute time, useful for multi-sensor fusion)
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json ntp-sync=true ! g3dinference ! ...
```

### Timeout configuration

```bash
# Default 5-second timeout
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json ! ...

# Shorter timeout for tests (3 seconds)
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json timeout=3000000 ! ...

# Disable the timeout (debugging)
gst-launch-1.0 g3dlidarsrc config=configs/robosense_e1r_udp.json timeout=0 ! ...
```

## Input/Output

- **Input Capability**: none (this is a source element with no sink pad)
- **Output Capability**: `application/x-lidar` (buffer with attached `LidarMeta`)

The output payload is a contiguous array of 32-bit floats, four per point in `x, y, z, intensity` order, repeated `lidar_point_count` times. This is the same layout `g3dlidarparse` produces, so `g3dinference` and other downstream elements accept either source unchanged.

## Metadata

The element attaches `LidarMeta` to each output buffer:

- `lidar_point_count`: number of points in the frame (each point is 4 floats)
- `frame_id`: sequential frame counter, starting at 0 and incrementing per emitted frame
- `exit_source_timestamp`: timestamp when the buffer leaves the source, in nanoseconds (see `ntp-sync` below)
- `exit_g3dinference_timestamp`: initialized to `GST_CLOCK_TIME_NONE`; filled in later by `g3dinference` if present
- `stream_id`: the configured `stream-id` property value

### Timestamp behavior (`ntp-sync`)

The `ntp-sync` property selects which clock the per-frame timestamp comes from:

| `ntp-sync` | Source clock           | Characteristics                                                                                  |
|------------|------------------------|--------------------------------------------------------------------------------------------------|
| `false` (default) | GStreamer pipeline clock | Nanosecond precision, shared by all elements, starts from 0 in `PLAYING`. Best for single-sensor pipelines and time alignment. |
| `true`     | LiDAR hardware clock (from the per-MSOP-packet timestamp field) | Absolute device time. Useful for multi-LiDAR fusion where sensors share a synchronized hardware clock. |

The chosen value is written to both `LidarMeta.exit_source_timestamp` and `GST_BUFFER_PTS` on every output buffer; `GST_BUFFER_DTS` is set to `GST_CLOCK_TIME_NONE`.



## Vendor backends

`g3dlidarsrc` has **no compile-time dependency** on any vendor SDK. Each vendor is a separate **backend shared library** (`libg3dlidar_<vendor>.so`) that the element loads at runtime. At `start()` the element reads the config's `vendor` field, derives the library name (`vendor="robosense"` → `libg3dlidar_robosense.so`), `dlopen`s it, and resolves a small set of flat C functions via `dlsym`:

```
g3dlidarsrc (in libgst3delements.so)  --dlopen("libg3dlidar_<vendor>.so")-->  backend  -->  vendor SDK
    (vendor-neutral, no SDK include)          C ABI / function pointers                (SDK + threading)
```

The C ABI is vendor-neutral and declared in [g3d_lidar_backend_api.h](../../../include/dlstreamer/lidar/g3d_lidar_backend_api.h) (`g3d_lidar_backend_create` / `set_callbacks` / `init` / `start` / `stop` / `destroy`). It carries only fields common to all UDP LiDARs (model, bind address, timestamp policy); vendor-specific options ride in the raw `params` JSON object, which the element passes through **verbatim** and each backend parses itself. This is why the element does not hardcode a vendor allowlist and knows nothing about any vendor's parameter schema — support for a vendor is simply "is its backend library present?".

Backends live under [`g3dlidarsrc/backends/<vendor>/`](../../../src/monolithic/gst/3d_elements/g3dlidarsrc/backends/) and are each **opt-in** (default OFF), because building one pulls in that vendor's SDK. Turning them all off still builds the element and plugin; `g3dlidarsrc` is registered and fails only at `start()` when the requested vendor's backend is missing.

### RoboSense backend (`rs_driver`)

The RoboSense backend ([backends/robosense/](../../../src/monolithic/gst/3d_elements/g3dlidarsrc/backends/robosense/)) wraps the **header-only C++** `rs_driver` SDK — which has no prebuilt library and no stable C symbols of its own — behind the C ABI. It is built by the `g3dlidar_robosense` target, gated by the `ENABLE_LIDAR_ROBOSENSE` option (default OFF), producing `libg3dlidar_robosense.so` on Linux / `g3dlidar_robosense.dll` on Windows.

#### Linux

```bash
# From the DL Streamer root
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DENABLE_LIDAR_ROBOSENSE=ON
cmake --build build --target gst3delements g3dlidar_robosense -j"$(nproc)"
```

Or with the top-level `Makefile`, which forwards the option:

```bash
make build ENABLE_LIDAR_ROBOSENSE=ON
```

#### Windows

The all-in-one PowerShell script (see the [Windows compilation guide](../dev_guide/advanced_install/advanced_install_guide_windows_compilation.md)) takes an `-enableLidarRobosense` switch, which forwards `-DENABLE_LIDAR_ROBOSENSE=ON` to CMake. Run it from an **administrator** PowerShell in the DL Streamer root:

```powershell
# Build from source, including the RoboSense backend
.\scripts\build_dlstreamer_dlls.ps1 -enableLidarRobosense -setEnv

# Or build the installer package with the backend included (this is what CI runs)
.\scripts\build_dlstreamer_dlls.ps1 -buildInstaller -installerSkipCompression -enableLidarRobosense
```

Without `-enableLidarRobosense` the script builds everything **except** the backend (the CMake option stays OFF). The Windows CI already passes this switch, so the shipped installer includes `g3dlidar_robosense.dll`.

There are two ways to provide the `rs_driver` source it compiles against. **If in doubt, use option B.**

- **Option A — bring your own.** Clone rs_driver yourself (matching the commit listed as `RS_DRIVER_VERSION` in the backend's [CMakeLists.txt](../../../src/monolithic/gst/3d_elements/g3dlidarsrc/backends/robosense/CMakeLists.txt)), apply the in-tree patches once with the helper script, then point CMake at it via either `-DRS_DRIVER_SRC_DIR=/path/to/rs_driver/src` or the `RS_DRIVER_SRC_DIR` environment variable. If the build complains about your tree (wrong commit, patches not applied, etc.), unset the variable and fall back to option B.

  ```bash
  bash <dlstreamer>/src/monolithic/gst/3d_elements/g3dlidarsrc/backends/robosense/patches/apply_patch.sh \
      <dlstreamer>/src/monolithic/gst/3d_elements/g3dlidarsrc/backends/robosense/patches/*.patch
  ```

  The helper is idempotent, so re-running it after a `git clean` is safe.

- **Option B — let CMake do it (default).** Don't set `RS_DRIVER_SRC_DIR`. The first `cmake ..` with `ENABLE_LIDAR_ROBOSENSE=ON` automatically clones the pinned commit, applies the patches, and configures the build. One-time network download; nothing else to do.

### Adding a new vendor

1. Create `g3dlidarsrc/backends/<vendor>/` with a `CMakeLists.txt` that builds `libg3dlidar_<vendor>.so`.
2. Implement the functions in [g3d_lidar_backend_api.h](../../../include/dlstreamer/lidar/g3d_lidar_backend_api.h) with C linkage, containing all of that vendor's SDK code and threading. Parse the vendor's own keys out of `params_json`.
3. Add an `option(ENABLE_LIDAR_<VENDOR> ... OFF)` + `add_subdirectory(<vendor>)` in [backends/CMakeLists.txt](../../../src/monolithic/gst/3d_elements/g3dlidarsrc/backends/CMakeLists.txt).

The element needs **no changes** — a config with `"vendor": "<vendor>"` will load the new backend automatically.

### Runtime requirement

Backends are loaded by bare name (`libg3dlidar_<vendor>.so` on Linux, `g3dlidar_<vendor>.dll` on Windows), so the install directory must be on the dynamic loader's search path at runtime:

- **Linux** — on `LD_LIBRARY_PATH`. A standard install places the backend under `lib/`, which `scripts/setup_dls_env.sh` already adds to `LD_LIBRARY_PATH`.
- **Windows** — on `PATH` (Windows searches `PATH`, not `LD_LIBRARY_PATH`). A standard install / `setup_dls_env.ps1` sets this up; building from the tree with `build_dlstreamer_dlls.ps1 -setEnv` also adds the build output to `PATH`.

If the element fails at start with *"Failed to load backend '…'"*, the backend is either not built (its `ENABLE_LIDAR_<VENDOR>` option was OFF) or not on the library path.

## Element Details (gst-inspect-1.0)

```text
Factory Details:
  Rank                     none (0)
  Long-name                G3D LiDAR Source
  Klass                    Source/Device
  Description              Receives real-time LiDAR point cloud data via rs_driver SDK (g3dlidarsrc)
  Author                   Intel Corporation

Plugin Details:
  Name                     3delements
  Description              DL Streamer 3D Elements
  Filename                 /home/kpi/dlstreamer/build/intel64/Release/lib/libgst3delements.so
  Version                  2026.1.0
  License                  MIT/X11
  Source module            dlstreamer
  Binary package           Deep Learning Streamer elements
  Origin URL               https://github.com/open-edge-platform/dlstreamer/tree/main

GObject
 +----GInitiallyUnowned
       +----GstObject
             +----GstElement
                   +----GstBaseSrc
                         +----GstPushSrc
                               +----GstG3DLidarSrc

Element Flags:
  - SOURCE

Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      application/x-lidar

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  automatic-eos       : Automatically EOS when the segment is done
                        flags: readable, writable
                        Boolean. Default: true

  blocksize           : Size in bytes to read per buffer (-1 = default)
                        flags: readable, writable, deprecated
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0

  config              : Path to a JSON config file describing the LiDAR vendor, model and transport. Required.
                        flags: readable, writable
                        String. Default: null

  do-timestamp        : Apply current stream time to buffers
                        flags: readable, writable, deprecated
                        Boolean. Default: false

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "g3dlidarsrc0"

  ntp-sync            : Timestamp source for buffer PTS (TRUE = LiDAR clock from DIFOP, FALSE = pipeline running time, starts from 0 at PLAYING)
                        flags: readable, writable
                        Boolean. Default: false

  num-buffers         : Number of buffers to output before sending EOS (-1 = unlimited)
                        flags: readable, writable
                        Integer. Range: -1 - 2147483647 Default: -1

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  stream-id           : Stream identifier for this LiDAR source (used in metadata)
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 0

  timeout             : Timeout in microseconds for receiving data (0 = no timeout)
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 5000000

  typefind            : Run typefind before negotiating (deprecated, non-functional)
                        flags: readable, writable, deprecated
                        Boolean. Default: false

```

