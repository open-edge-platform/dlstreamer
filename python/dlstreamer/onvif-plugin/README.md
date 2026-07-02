# `dlsonvif` — ONVIF Device Provider plugin for GStreamer

This directory contains a GStreamer plugin that registers a
`Gst.DeviceProvider` named **`onvifdeviceprovider`** (class
`Source/Network/ONVIF`). The implementation is in Python; a thin C shim
ensures the feature lands in GStreamer's persistent registry cache and
is visible to pure-C tools (`gst-device-monitor-1.0`,
`gst-inspect-1.0`, etc.).

## What is provided

| File | Role |
|---|---|
| `c/onvifdeviceprovider_shim.c` | C plugin source |
| `libgstdlsonvif.so` | Compiled plugin (GStreamer name: `dlsonvif`) |
| `python/onvifdeviceprovider.py` | `OnvifDeviceProvider` / `OnvifDevice` implementation |
| `python/dls_onvif_src.py` | Separate `dlsonvifsrc_py` element (loaded by `libgstpython`) |

The `dlsonvif` plugin exposes **1 feature** (`onvifdeviceprovider`).
The second pipeline-relevant feature (`dlsonvifsrc_py`) is registered by
the `python` plugin (`libgstpython`) from the file
`python/dls_onvif_src.py`. For a full reference of all ONVIF-related
features seen in `gst-inspect-1.0` — see the
[Feature reference](#feature-reference) section below.

## Feature reference

This section describes every `Gst*Factory` returned by:

```bash
gst-inspect-1.0 2>&1 | grep -i onvif
# dlsonvif:  onvifdeviceprovider (GstDeviceProviderFactory)
# python:    dlsonvifsrc_py: DLS ONVIF Source Python
# rtponvif:  rtponvifparse: ONVIF NTP timestamps RTP extension
# rtponvif:  rtponviftimestamp: ONVIF NTP timestamps RTP extension
```

Full technical documentation of each of the four features: GObject
header, hierarchy, threading, on-the-wire data format, lifecycle, error
paths, and operating modes.

### 1. `dlsonvif` → `onvifdeviceprovider` (GstDeviceProviderFactory)

**Type:** `Gst.DeviceProvider` — **not** a pipeline element (not usable
with `gst-launch`, invisible to `gst-inspect-1.0 <feature>` without the
plugin name). Implementation: [python/onvifdeviceprovider.py](python/onvifdeviceprovider.py),
loaded via the C shim [c/onvifdeviceprovider_shim.c](c/onvifdeviceprovider_shim.c)
into `libgstdlsonvif.so`.

| Field | Value |
|---|---|
| Factory name | `onvifdeviceprovider` |
| Long name | `ONVIF Device Provider` |
| Classification | `Source/Network/ONVIF` |
| Rank | `PRIMARY` (256) |
| Plugin | `dlsonvif` (`libgstdlsonvif.so`, license MIT, package `dlstreamer`) |
| Author | Intel DLStreamer |
| GObject hierarchy | `OnvifDeviceProvider → GstDeviceProvider → GstObject → GInitiallyUnowned → GObject` |
| Device class | `OnvifDevice → GstDevice → GstObject → GInitiallyUnowned → GObject` |

**What it provides.** A stream of `DEVICE_ADDED` / `DEVICE_CHANGED` /
`DEVICE_REMOVED` notifications on the bus of any `Gst.DeviceMonitor`
with a `Source/Network/ONVIF` filter, where each event carries a fully
synchronized representation of an ONVIF camera (XAddr, capabilities,
media profiles, auth-status). Each device can build a configured
`rtspsrc` on its own (`do_create_element`), allows re-injecting
credentials without recreating the element (`do_reconfigure_element`),
and accepts the `onvif-set-credentials` custom event as a mechanism for
passing a password from the application to the provider.

#### 1.1. What it uses (runtime dependencies)

- `python-onvif-zeep` (SOAP, optionally `lxml` + `zeep`) — via the
  internal `dlstreamer.onvif` package (classes `DlsOnvifDiscoveryEngine`,
  `discover_onvif_cameras`).
- `requests` — HTTP transport for SOAP.
- `gi` / `PyGObject` — GStreamer/GObject bindings.
- `ctypes` — direct calls into `libgstreamer-1.0.so.0` /
  `libgobject-2.0.so.0`, because PyGObject **does not export**
  `gst_device_provider_class_set_metadata` nor a way to register a
  device provider from Python. Registration is implemented manually in
  `register_provider()` / `register_provider_with_plugin()` via
  `gst_plugin_register_static_full` with a C callback held as a
  module-global CFUNCTYPE reference (`_PLUGIN_INIT_REF`), so the GC does
  not free it during the registry's lifetime.
- Network: **UDP multicast 239.255.255.250:3702** (WS-Discovery `Probe`),
  **TCP** to `host:port/onvif/device_service` (SOAP), and a local
  short-lived **HTTP server** in push-event mode (outside this
  feature — see the sample-app).

#### 1.2. Threading

Each provider instance maintains:

| Thread | Created in | Function |
|---|---|---|
| `onvifdeviceprovider-discover` (daemon) | `do_start()` | Discovery loop: `_discovery_loop` → `_probe_once` every `_probe_interval` seconds (default 30 s, override env `ONVIF_PROVIDER_PROBE_INTERVAL`). Sleeps in 0.5-second chunks so `do_stop()` can wake it within under 0.5 s. |
| PullPoint workers (per camera, phase 5) | registered in `self._event_workers: dict[str, (Thread, Event)]` | Best-effort subscription to ONVIF Profile S §7.7.4 events (CreatePullPointSubscription + PullMessages, timeout `PT30S`). |

Synchronization: a `threading.RLock()` protecting `self._devices`,
`self._credentials`, `self._auth_status`, `self._event_workers`. A
`threading.Event()` is used as the stop-signal for the discovery thread
and for each PullPoint worker.

#### 1.3. Lifecycle (`GstDeviceProvider` vfuncs)

```
Gst.DeviceMonitor.start()                     [external API]
  → OnvifDeviceProvider.do_start()
     → checks _IMPORT_ERROR (is dlstreamer.onvif available?)
     → spawn `onvifdeviceprovider-discover`
     → discovery_loop():
          probe_once() {
            for cam in discover_onvif_cameras():
              xaddr = build_xaddr(cam)
              if xaddr not in self._devices:
                device = build_device(xaddr, host, port) {
                  ONVIFCamera(host, port, user, password)   # SOAP client
                  GetDeviceInformation()                    # manufacturer/model/serial
                  GetCapabilities({Category: "All"})        # Media/Events/Imaging/PTZ XAddrs
                  camera_profiles(client) → list[Profile]   # GetProfiles + GetStreamUri
                  build_caps_from_profiles(profiles)        # Gst.Caps
                  base_properties(...)                      # Gst.Structure
                  return OnvifDevice(...)
                }
                self.device_add(device)                     # → DEVICE_ADDED on bus
            for stale in self._devices - seen_xaddrs:
              self._drop_device(stale)                       # → DEVICE_REMOVED on bus
          }
          sleep(_probe_interval) in 0.5 s chunks

Gst.DeviceMonitor.stop()
  → do_stop():
     set stop_event
     join PullPoint workers (timeout 2 s each)
     join discovery worker (timeout _probe_interval + 5 s)
     for device in self._devices: self.device_remove(device)
     self._devices.clear()
```

Every `build_device` stage is wrapped in `try/except` — a
**partial failure** (e.g. SOAP `GetDeviceInformation` fails with 401)
still publishes the device, only with empty metadata and
`onvif.auth-status="unauthorized"`. Caps may be empty
(`Gst.Caps.new_empty()`).

#### 1.4. Device properties

`Gst.Device.get_properties()` returns a `Gst.Structure` named
`onvif-device` containing:

| Key | Type | Source / meaning |
|---|---|---|
| `onvif.xaddr` | string | Canonical, stable camera ID — `xaddr`/`full_url` from WS-Discovery, fallback `http://host:port/onvif/device_service`. **Used as the identity key** in all internal dictionaries. |
| `onvif.host` | string | Hostname/IP extracted from the XAddr. |
| `onvif.port` | int | ONVIF service port (default 80). |
| `onvif.scopes` | string | Space-separated scope URIs from WS-Discovery (`onvif://www.onvif.org/…`); empty until enriched. |
| `onvif.auth-status` | string | Authorization state: `anonymous` (no attempts), `authenticated` (`GetDeviceInformation` OK), `unauthorized` (401/403), `unknown` (device unknown during a set-credentials attempt). |
| `onvif.manufacturer` | string | `GetDeviceInformation.Manufacturer`, empty if unauthorized. |
| `onvif.model` | string | as above, `Model`. |
| `onvif.serial` | string | as above, `SerialNumber`. |
| `onvif.capabilities` | `Gst.Structure` (`onvif-capabilities`) | Substructure from `GetCapabilities`. |

The `onvif-capabilities` substructure:

| Key | Type | Meaning |
|---|---|---|
| `has-media1` | bool | Whether the camera offers Media v1 (`http://…/onvif/media_service`). |
| `has-media2` | bool | Whether it offers Media2 (reserved; currently always `False`). |
| `has-events` | bool | Whether it offers the Events service (`tns1:…`). |
| `has-receiver` | bool | Whether it has a Receiver service. |
| `media-version` | int | `1` if Media v1, `0` if absent. |
| `media` / `events` / `imaging` / `ptz` / `analytics` / `device` | nested `Gst.Structure` (`onvif-capability-<name>`) with an `xaddr` field (string) | Service URL of the given category, when present in the `GetCapabilities` response. |

#### 1.5. Caps (`Gst.Device.get_caps()`)

One `Gst.Structure` of type `onvif-profile` per media profile returned
by `GetProfiles`. Fields:

| Field | Type | Source / description |
|---|---|---|
| `media` | string | Always `"video"` (audio/metadata profiles are not emitted in the current impl.). |
| `encoding-name` | string | `H264` / `H265` / `JPEG` / `MPEG4` — from `profile.vec_encoding`, uppercased. |
| `width`, `height` | int | `profile.vec_resolution.{width,height}` (only when > 0). |
| `framerate` | `Gst.Fraction` | `profile.vec_framerate_limit / 1` (round-trip compatible with `rtspsrc.caps`). |
| `bitrate` | int | `profile.vec_bitrate_limit` in kbit/s (only when > 0). |
| `profile` | string | `vec_h264_profile` or `vec_mpeg4_profile` (e.g. `"Main"`, `"High"`, `"Baseline"`). |
| `profile-token` | string | `profile.token` — argument to `GetStreamUri` on the camera side (e.g. `"Profile_1"`). |
| `stream-uri` | string | Raw RTSP URL from `GetStreamUri` (without credentials). |

No profiles → `Gst.Caps.new_empty()` (the device remains, but
`do_create_element` returns `None`).

#### 1.6. `do_create_element(name)`

```
caps = self.get_caps()
if caps.size == 0: return None
struct = caps[0]
stream_uri = struct["stream-uri"]
if creds := provider.get_credentials(self.xaddr):
    stream_uri = inject_credentials(stream_uri, *creds)  # rtsp://user:pwd@host…
element = Gst.ElementFactory.make("rtspsrc", name)
element["location"] = stream_uri
element["latency"] = 200                                  # ms, best-effort
return element
```

**Only profile #0 is selected**. If the application wants a different
profile, it must read the caps and build the `rtspsrc` itself from the
`stream-uri` of the chosen structure (a typical consumer pattern; the
classic `Gst.Device` API has no "select a profile and give me an
element").

#### 1.7. `do_reconfigure_element(element)`

Re-injects credentials into the `location` of an existing `rtspsrc`
(after a password change via `onvif-set-credentials`):

```
current = element["location"]
if not current: return False
if creds := provider.get_credentials(self.xaddr):
    element["location"] = inject_credentials(current, *creds)
return True
```

The element must be in the `NULL` or `READY` state (rtspsrc requires a
location remount). The wrapper does not enforce this.

#### 1.8. Custom events accepted by the device

`OnvifDevice.send_event(event)` (overrides `Gst.Device.send_event`,
which does not exist in GStreamer C — this API is shimmed here for the
`onvifcm` library):

| Structure | Required fields | Format | Effect |
|---|---|---|---|
| `onvif-set-credentials` | `uri` (string) | `onvif://<user>[:<password>]@<host>[:<port>]` (URL-encoded per RFC 3986; `%21` for `!`, etc.) | 1. `urlparse(uri)`, validation of the scheme and the presence of `username`. 2. Store `(user, password)` in `self._credentials[xaddr]`. 3. `_verify_credentials(host, port, user, password)` → SOAP `GetDeviceInformation` (default zeep timeout). 4. Update `onvif.auth-status` (`authenticated`/`unauthorized`). 5. `_refresh_device(xaddr)` → `device_remove(old)` + `device_add(new)` with refreshed properties and caps. |

Return value of `send_event`: `True` if `GetDeviceInformation` succeeded
(authorization OK), `False` for an invalid URI or an unauthorized
camera.

#### 1.9. ONVIF events on the monitor bus (phase 5)

Per camera, an optional worker is started that subscribes to the ONVIF
Events Service (Profile S §7.7.4, PullPoint pattern). Events are mapped
to the structure:

| Structure | Fields | Type |
|---|---|---|
| `onvif-event` | `topic` | string (`tns1:VideoSource/MotionAlarm`, `tns1:RuleEngine/...`) |
|  | `utc-time` | guint64 (NTP-style, or epoch in ns depending on implementation) |
|  | `source` | string (`source.name` from the `NotificationMessage`) |
|  | `data` | string (serialized payload) |

Default PullMessages timeout: `PT30S` (`_DEFAULT_PULLPOINT_TIMEOUT`).
Workers are **best-effort** — a subscription failure does not break
discovery and does not publish a device error.

#### 1.10. Logging and diagnostics

- Debug channel: the `[onvifdeviceprovider]` prefix on all
  `Gst.info/debug/warning/error` calls (the `_gst_log` helper).
- A Python `Gst.DeviceProvider` **has no direct access to the bus**, so
  errors are routed to `Gst.error()` — consumers catch them via
  `GST_DEBUG=onvifdeviceprovider:5`.
- C shim logs: the `onvifshim` category (`GST_DEBUG=onvifshim:5`).

#### 1.11. Provider properties

**None** — configuration is via environment variables:

| ENV | Default | Effect |
|---|---|---|
| `ONVIF_PROVIDER_PROBE_INTERVAL` | `30.0` (s) | WS-Discovery re-probe interval. `≤ 0` = single-shot (one probe, thread ends). |
| `ONVIFCM_PLUGIN_PATH` | — | Additional path for `libgstpython` / `libgstdlsonvif.so` (scanned by `onvifcm`). |
| `GST_DEBUG` | — | Typically: `onvifdeviceprovider:5,onvifshim:5`. |

#### 1.12. Registration: two paths

| Function | Called by | Cache persistence |
|---|---|---|
| `register_provider()` | `dlstreamer.onvif.__init__` (in-process; e.g. `onvifcm`) — internally `gst_plugin_register_static_full` with a dummy plugin `"onvifdeviceprovider"`. | **NO** — a static plugin has no file on disk, so `gst_registry_save` does not persist it; visible in-process only. |
| `register_provider_with_plugin(plugin_addr)` | C shim `libgstdlsonvif.so` — `plugin_init()` → a Python call with a `GstPlugin*` converted via ctypes. | **YES** — the feature is linked into a real on-disk plugin → an entry in the binary registry cache → visible to `gst-inspect-1.0`, `gst-device-monitor-1.0`. |

The detection of which path to take is in
`c/onvifdeviceprovider_shim.c` (it checks `/proc/self/comm` to avoid
registering in-process inside the `gst-plugin-scanner` subprocess).

#### 1.13. Inspection

```bash
# plugin name (not the feature):
gst-inspect-1.0 dlsonvif

# typical usage:
gst-device-monitor-1.0 -f Source/Network/ONVIF --follow

# in Python:
Gst.Registry.get().find_feature("onvifdeviceprovider", Gst.DeviceProviderFactory)
```

---

### 2. `python` → `dlsonvifsrc_py` (GstElementFactory)

**Type:** `Gst.Bin` (a container with a single ALWAYS src pad).
Implementation: [python/dls_onvif_src.py](python/dls_onvif_src.py);
registered by the upstream `python` plugin (`libgstpython.so`) —
**not** by our C shim. Registration mechanism: the module-level variable
`__gstelementfactory__ = ("dlsonvifsrc_py", Gst.Rank.NONE, OnvifSrc)`,
which `libgstpython` looks for in every `.py` on `GST_PLUGIN_PATH`.

| Field | Value |
|---|---|
| Factory name | `dlsonvifsrc_py` |
| Long name | `DLS ONVIF Source Python` |
| Classification | `Source/Network` |
| Rank | `NONE` (0) |
| Plugin | `python` (`libgstpython.so` from `gst-python`, file `dls_onvif_src.py` on `GST_PLUGIN_PATH`) |
| Author | Intel DLStreamer |
| GObject hierarchy | `OnvifSrc → GstBin → GstElement → GstObject → GInitiallyUnowned → GObject` |
| Internal pipeline | `uridecodebin3 ! videoconvert ! [ghost src]` |

**What it provides.** A single src pad emitting *decoded* `video/x-raw`
directly from an ONVIF camera, after automatic WS-Discovery and RTSP URL
resolution via SOAP. The application does not need to build
`rtspsrc + depayloader + decoder` — `dlsonvifsrc_py ! videoconvert ! <sink>`
is enough.

#### 2.1. What it uses (runtime dependencies)

- `python-onvif-zeep` + `zeep` + `lxml` — via the internal
  `dlstreamer.onvif.dls_onvif_discovery_engine` (classes
  `discover_onvif_cameras`, `DlsOnvifDiscoveryEngine`).
- `gi` / `PyGObject` — `Gst`, `GObject`.
- GStreamer element: **`uridecodebin3`** (from `gst-plugins-base` ≥ 1.20)
  — internally manages the full RTSP pipeline (`rtspsrc` → depay →
  parser → decoder) together with the jitter buffer.
- GStreamer element: **`videoconvert`** (from `gst-plugins-base`) —
  pixel-format conversion to canonical raw video.
- If the Python dependencies are not available, the module still
  registers, but discovery mode returns `Gst.StateChangeReturn.FAILURE`
  with `[dlsonvifsrc_py] dlstreamer.onvif unavailable: <ImportError>`.
  The `rtsp-uri` (override) mode works **without** the ONVIF
  dependencies.

#### 2.2. Network

- **In discovery mode:** UDP multicast 239.255.255.250:3702 (Probe),
  TCP `host:port/onvif/device_service` (SOAP `GetProfiles` + `GetStreamUri`),
  then RTSP (TCP/UDP per `rtspsrc` negotiation).
- **In override mode (`rtsp-uri`):** RTSP only.

#### 2.3. Threading

The element creates no auxiliary threads of its own — discovery happens
**synchronously** in `do_change_state(NULL → READY)` and blocks for the
duration of `discovery-timeout` (default 5 s). Afterwards all real-time
operations run in `uridecodebin3` threads.

#### 2.4. Pad templates

| Name | Direction | Presence | Caps template | Actual caps |
|---|---|---|---|---|
| `src` | SRC | ALWAYS | `ANY` | `video/x-raw, format=I420/NV12/RGB/…` after `videoconvert` (auto-negotiated with downstream) |

Sink pads — none (it is a source). The internal `uridecodebin3` has
dynamic pads, connected by the `pad-added` handler
(`_on_uridecodebin_pad_added`) — **only the first pad starting with
`video/` is linked**; audio and metadata pads are ignored (left
unlinked).

#### 2.5. Properties

| Name | Type | Range | Default | Effect |
|---|---|---|---|---|
| `hostname` | string | any | `""` | WS-Discovery filter: only the camera with this hostname/IP. `""` = the first one found. |
| `port` | int | 0–65535 | `0` | ONVIF port filter. `0` = any port. |
| `user-id` | string | any | `""` | Username for ONVIF SOAP and RTSP (injected into the URL: `rtsp://user:pwd@host…`). |
| `user-pw` | string | any | `""` | Password, as above. |
| `profile-index` | int | 0–64 | `0` | Index of the media profile to stream (0-based). If `>=len(profiles)`, falls back to 0 with the warning `profile-index … out of range`. |
| `discovery-timeout` | int | 1–60 s | `5` | Timeout (s) for WS-Discovery responses. The probe blocks `do_change_state` for this time. |
| `rtsp-uri` | string | any URL | `""` | **Override:** if non-empty, discovery and SOAP are skipped and the internal `uridecodebin3` receives this URI directly. Useful for unit tests and when the application knows the URL from another source. |
| `latency` | int | 0–10000 ms | `200` | `uridecodebin3` latency (RTSP jitter buffer). Skipped (warning `TypeError`) on older `uridecodebin3` versions without this property. |

All properties are **read once** during `NULL → READY` — a change in the
`PLAYING` state has no effect until the next `READY → NULL → READY`
cycle.

#### 2.6. Lifecycle (state transitions)

```
NULL → READY:
  if rtsp-uri is non-empty:
    resolved_uri = rtsp-uri
  else:
    cam = discover_onvif_cameras(verbose=False)        # 5 s (or `discovery-timeout`)
    match = first camera matching the hostname/port filter
    if not match: return FAILURE
    client = ONVIFCamera(host, port, user, password)
    profiles = DlsOnvifDiscoveryEngine().camera_profiles(client)
    profile = profiles[profile-index | 0]
    rtsp_url = profile.rtsp_url
    if user and password and "@" not in rtsp_url:
        rtsp_url = "rtsp://user:pwd@" + rest
    resolved_uri = rtsp_url
  uridecodebin3["uri"] = resolved_uri
  uridecodebin3["latency"] = latency_ms

READY → PAUSED → PLAYING:
  delegated to GstBin.do_change_state (uridecodebin3 starts the pipeline).

PAUSED → READY → NULL:
  resolved_uri = None  (re-discovery on the next NULL → READY)
  delegated to GstBin.
```

**Re-discovery.** To force another probe (e.g. after a camera
hot-swap), the application must go through a full
`set_state(NULL) → set_state(PLAYING)` cycle.

#### 2.7. Signals and events

- **No signals of its own** (`__gsignals__` is empty).
- Upstream/downstream events are forwarded by `GstBin` without
  modification.
- The element **does not consume** `onvif-set-credentials` (that is a
  device-provider event, not this source's) — credentials are set via
  the `user-id` / `user-pw` properties.

#### 2.8. Error paths

| Situation | Result |
|---|---|
| `dlstreamer.onvif` or `python-onvif-zeep` fail to import | In `NULL → READY` in discovery mode → `Gst.error("dlstreamer.onvif unavailable: …")` and `Gst.StateChangeReturn.FAILURE`. The `rtsp-uri` mode works. |
| `uridecodebin3` or `videoconvert` missing | In `__init__` → `Gst.error("required GStreamer elements not available")`, the element is non-functional (no src pad). |
| WS-Discovery found no matching camera | `Gst.error("no ONVIF camera matched the requested filters")` + `FAILURE`. |
| `ONVIFCamera(host, port, user, password)` raises | `Gst.error("ONVIF connect to host:port failed: …")` + `FAILURE`. |
| `GetProfiles` raises or returns an empty list | `Gst.error(...)` + `FAILURE`. |
| Profile without `rtsp_url` | `Gst.error("profile 'X' has no RTSP URL")` + `FAILURE`. |
| `uridecodebin3 → videoconvert` link failed (unusual) | `Gst.error("failed to link uridecodebin3 -> videoconvert: …")`, the pad will not be linked — the bin emits no data. |

#### 2.9. Logging

All element messages carry the `[dlsonvifsrc_py]` prefix. Enable:

```bash
GST_DEBUG=2,*onvif*:6,uridecodebin3:5 gst-launch-1.0 dlsonvifsrc_py ! fakesink
```

#### 2.10. Examples

```bash
# Discovery + autoplay of the first camera found (anonymously):
gst-launch-1.0 dlsonvifsrc_py discovery-timeout=2 ! videoconvert ! fakesink

# Specific camera + credentials + second profile:
gst-launch-1.0 dlsonvifsrc_py hostname=10.91.106.215 port=8080 \
    user-id=admin user-pw=Secret profile-index=1 latency=300 \
  ! videoconvert ! autovideosink

# Override (no WS-Discovery and no SOAP):
gst-launch-1.0 dlsonvifsrc_py rtsp-uri=rtsp://10.91.106.215:8554/cam0_main \
  ! videoconvert ! autovideosink

# Python:
import gi; gi.require_version("Gst", "1.0")
from gi.repository import Gst
Gst.init(None)
pipeline = Gst.parse_launch(
    "dlsonvifsrc_py hostname=10.91.106.215 user-id=admin user-pw=Secret "
    "! videoconvert ! autovideosink"
)
pipeline.set_state(Gst.State.PLAYING)
```

#### 2.11. Inspection

```bash
gst-inspect-1.0 dlsonvifsrc_py
# shows long-name, classification, pad templates, list of properties
```

---

### 3. `rtponvif` → `rtponvifparse` (GstElementFactory)

**Type:** an upstream `GstElement` from `gst-plugins-bad` (Axis
Communications / Collabora, **not** owned by this repo —
[gst/onvif/gstrtponvifparse.c](../../../build/deps/gstreamer/src/gstreamer/subprojects/gst-plugins-bad/gst/onvif/gstrtponvifparse.c)).
A **receiving** element (depacketization side): it extracts the absolute
UTC time and status flags from the ONVIF RTP header extension in the
stream coming from a camera (Profile G replay / Profile S with precise
timestamps).

| Field | Value |
|---|---|
| Factory name | `rtponvifparse` |
| Long name | `ONVIF NTP timestamps RTP extension` |
| Classification | `Effect/RTP` |
| Rank | `NONE` (0) |
| Plugin | `rtponvif` (`libgstrtponvif.so`, gst-plugins-bad, `description="ONVIF Streaming features"`) |
| Author | Guillaume Desmottes \<guillaume.desmottes@collabora.com\> (Collabora) |
| License | LGPL 2.1 |
| GObject hierarchy | `GstRtpOnvifParse → GstElement → GstObject → GInitiallyUnowned → GObject` |

#### 3.1. What it provides

For each RTP buffer:

1. **Parses the ONVIF RTP header extension** with ID `0xABAC`, length
   `EXTENSION_SIZE = 3` words (3 × 32-bit = 12 bytes of extension
   payload). If the extension is missing or has a different ID/length,
   the buffer is passed through unchanged.
2. **Sets `GST_BUFFER_PTS`** to the absolute time computed from the
   64-bit NTP timestamp (seconds + fraction since the 1900-01-01 UTC
   epoch). If the timestamp is `0xFFFFFFFF:0xFFFFFFFF` (the "unknown"
   value) → `GST_BUFFER_PTS = GST_CLOCK_TIME_NONE`.
3. **Translates the status flags** into `GstBuffer` flags (see §3.4).
4. **On the `T` flag** (termination) — sends `GST_EVENT_EOS` on the src
   pad *after* pushing the buffer and returns `GST_FLOW_EOS`.

The element does not change the payload data (caps `application/x-rtp`
→ `application/x-rtp`), `GST_PAD_SET_PROXY_CAPS` on the sink pad.

#### 3.2. What it uses

- `gst/rtp/gstrtpbuffer.h` — `gst_rtp_buffer_map(GST_MAP_READWRITE)` +
  `gst_rtp_buffer_get_extension_data()` to read the extension header.
- No network dependencies of its own — it works only on buffers from
  upstream.

#### 3.3. Pad templates

| Name | Direction | Presence | Caps |
|---|---|---|---|
| `sink` | SINK | ALWAYS | `application/x-rtp` (PROXY_CAPS) |
| `src` | SRC | ALWAYS | `application/x-rtp` |

Chain function: `gst_rtp_onvif_parse_chain` (per-buffer; no chain-list).

#### 3.4. ONVIF RTP header extension format (`0xABAC`)

Per the ONVIF Streaming Spec. After the standard RTP header extension
header (`defined-by-profile=0xABAC`, `length=3`), 12 bytes follow:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            NTP timestamp, seconds (32 bit, big-endian)        |  data[0..3]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           NTP timestamp, fraction (32 bit, big-endian)        |  data[4..7]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|C|E|D|T|  mbz  |     CSeq      |          reserved (mbz)        |  data[8..11]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Bit (in `data[8]`) | Mask | Name | Mapping in `rtponvifparse` |
|---|---|---|---|
| 7 | `1<<7` | **C** — Clean point (key frame / IDR) | set → `GST_BUFFER_FLAG_UNSET(DELTA_UNIT)`; unset → `SET(DELTA_UNIT)`. |
| 6 | `1<<6` | **E** — End of contiguous recording | currently *read but not handled* (`TODO` in the code). |
| 5 | `1<<5` | **D** — Discontinuity | set → `GST_BUFFER_FLAG_SET(DISCONT)`; unset → `UNSET(DISCONT)`. |
| 4 | `1<<4` | **T** — Termination (end of stream) | set → after pushing the buffer sends `EOS` and returns `GST_FLOW_EOS`. |

Time conversion: `pts_ns = seconds * GST_SECOND + (fraction * 1e9 >> 32) * GST_NSECOND`.
`data[9]` = low byte of CSeq (currently read, `TODO` — unused).

#### 3.5. Position in the pipeline

**Directly after** `rtspsrc` / `rtpbin` / the RTP session manager,
**before** the depayloader (`rtph264depay`, `rtph265depay`, …). The
header extension is part of the RTP, not the payload — the depayloader
would lose it.

#### 3.6. Error paths

| Situation | Result |
|---|---|
| `gst_rtp_buffer_map` failed | `GST_ELEMENT_ERROR(STREAM, FAILED, "Failed to map RTP buffer")`, `GST_FLOW_ERROR`, buffer freed. |
| Missing extension or wrong ID/length | Buffer passed through unchanged (no-op). |
| Push to src failed | The error code from `gst_pad_push` is returned. |

#### 3.7. Example

```bash
gst-launch-1.0 rtspsrc location=rtsp://cam/onvif !            \
    rtponvifparse ! rtph264depay ! h264parse ! avdec_h264 !   \
    videoconvert ! autovideosink
```

---

### 4. `rtponvif` → `rtponviftimestamp` (GstElementFactory)

**Type:** an upstream `GstElement` from `gst-plugins-bad` (as above,
[gst/onvif/gstrtponviftimestamp.c](../../../build/deps/gstreamer/src/gstreamer/subprojects/gst-plugins-bad/gst/onvif/gstrtponviftimestamp.c)).
The **inverse** of `rtponvifparse` (packetization / sender side): on the
RTSP **server** side (typically inside `gst-rtsp-server` in ONVIF replay
mode) it injects the `0xABAC` header extension with the UTC time and the
C/E/D/T flags into every RTP packet. Used when implementing a
**Profile G replay endpoint** or a custom Profile S server with a
precise send time.

| Field | Value |
|---|---|
| Factory name | `rtponviftimestamp` |
| Long name | `ONVIF NTP timestamps RTP extension` |
| Classification | `Effect/RTP` |
| Rank | `NONE` (0) |
| Plugin | `rtponvif` (`libgstrtponvif.so`, gst-plugins-bad) |
| Author | Guillaume Desmottes (Collabora) |
| License | LGPL 2.1 |
| GObject hierarchy | `GstRtpOnvifTimestamp → GstElement → GstObject → GInitiallyUnowned → GObject` |
| Debug category | `rtponviftimestamp` |

#### 4.1. What it provides

For each buffer (or `GstBufferList`):

1. `gst_rtp_buffer_set_extension_data(0xABAC, 3)` — allocates a
   12-byte ONVIF extension in the RTP header.
2. Writes the **64-bit NTP timestamp** (big-endian) from the buffer's
   UTC time (`GST_WRITE_UINT64_BE`). Conversion:
   `ntp = utc_ns * (1<<32) / GST_SECOND`.
3. Sets the C/E/D/T status bits (`data[8]`) per the rules in §4.5.
4. Writes the low byte of `cseq` into `data[9]`, zeroes `data[10..12]`.

#### 4.2. What it uses

- `gst/rtp/gstrtpbuffer.h` — `gst_rtp_buffer_set_extension_data()` +
  `gst_rtp_buffer_get_extension_data()`.
- `GstReferenceTimestampMeta` with caps `timestamp/x-unix` — the
  `use-reference-timestamps` mode (adds the constant `2208988800 s` =
  the UNIX↔NTP epoch difference, in nanoseconds).
- `GstSegment` (`GST_FORMAT_TIME`) — required to translate
  `GST_BUFFER_PTS/DTS` into stream-time
  (`gst_segment_to_stream_time_full`) in `ntp-offset` mode. Without a
  time segment → `GST_ELEMENT_ERROR("did not receive a time segment
  yet")`.
- `gst_element_get_clock()` + `g_get_real_time()` — auto-computation of
  `ntp-offset` when it is not set explicitly and reference-meta is not
  used.

#### 4.3. Pad templates

| Name | Direction | Presence | Caps |
|---|---|---|---|
| `sink` | SINK | ALWAYS | `application/x-rtp` (PROXY_CAPS, PROXY_ALLOCATION) |
| `src` | SRC | ALWAYS | `application/x-rtp` |

Chain functions: `gst_rtp_onvif_timestamp_chain` (per-buffer) **and**
`gst_rtp_onvif_timestamp_chain_list` (a custom chain-list
implementation, to avoid copying every buffer —
`gst_pad_chain_list_default` refs buffers, making them non-writable).

#### 4.4. Properties

| Name | Type | Range | Default | Mutability | Description |
|---|---|---|---|---|---|
| `ntp-offset` | guint64 | `0 … G_MAXUINT64` | `0xFFFFFFFFFFFFFFFF` (`-1`, auto) | READWRITE | Offset between the pipeline running-time and absolute UTC, in **ns since 1900-01-01**. `-1` = the element computes it from `clock_time` and `g_get_real_time()` on the first buffer (requires a clock — in `PAUSED` without a clock the error "Can not guess ntp-offset" is raised). |
| `cseq` | guint | `0 … G_MAXUINT32` | `0` | READWRITE | The `CSeq` of the RTSP `PLAY` request that started the playback. The low byte is copied into `data[9]` of every extension. Profile G replay. |
| `set-e-bit` | gboolean | — | `false` | READWRITE | Set the **E** bit (end of a contiguous recording segment) in the **last** buffer of the segment. Requires caching one element → **+1 packet of latency**. |
| `set-t-bit` | gboolean | — | `false` | READWRITE | Set the **T** bit (termination) in the last buffer after `EOS`. **+1 packet of latency**. |
| `drop-out-of-segment` | gboolean | — | `true` | READWRITE | Drop buffers outside the `GstSegment` (beyond the spec — enables full reverse playback). When `false`, a buffer without a valid stream-time is still sent (without a timestamp). |
| `use-reference-timestamps` | gboolean | — | `false` | READWRITE + **MUTABLE_READY** (Since 1.22) | Take UTC from `GstReferenceTimestampMeta` (`timestamp/x-unix`) instead of the `ntp-offset` mechanism. Required when the source already has accurate UTC (e.g. a PTP camera). If set **and** `ntp-offset` is explicit → a warning and `ntp-offset` is zeroed. |

#### 4.5. Bit-setting rules (`data[8]` = `C E D T mbz`)

| Bit | Mask | Set condition |
|---|---|---|
| **C** | `1<<7` | The buffer does **not** have `GST_BUFFER_FLAG_DELTA_UNIT` (i.e. it is a key frame / clean point). |
| **E** | `1<<6` | `last == TRUE` (last buffer of the segment/list) **and** `self->set_e_bit` (set by `EOS` or the custom `GstOnvifTimestamp` event with `discont=TRUE`). Reset after use. |
| **D** | `1<<5` | `self->set_d_bit` (after `READY→PAUSED`, `FLUSH_STOP`, or an ntp-offset event with `discont=TRUE`) **or** the buffer has `GST_BUFFER_FLAG_DISCONT`. Reset after use. |
| **T** | `1<<4` | `last == TRUE` **and** `self->set_t_bit` (set by `EOS` when `prop_set_t_bit`). Reset after use. |

#### 4.6. Events

| Event | Handling |
|---|---|
| `GST_EVENT_SEGMENT` | Copied into `self->segment` (`gst_event_copy_segment`); must be `GST_FORMAT_TIME` for `ntp-offset` mode. |
| `GST_EVENT_EOS` | Forces `set_e_bit=TRUE` (+ `set_t_bit` if `prop_set_t_bit`), pushes the cached buffer and the event queue. |
| `GST_EVENT_FLUSH_STOP` | Clears the cache, `set_d_bit=TRUE`, resets `e/t`, reinitializes the segment. |
| `GST_EVENT_CUSTOM_DOWNSTREAM` named `GstOnvifTimestamp` | Contains the fields `ntp-offset` (clock-time) and `discont` (bool). Updates `self->ntp_offset` and `set_d_bit`; in `set-e-bit` mode it may mark the cached buffer as the end of the segment. The event is **consumed** (dropped). |

Serialized events arriving while a cached buffer exists are
**queued** (`event_queue`) and sent after the buffer is pushed.

#### 4.7. Lifecycle (`change_state`)

```
READY → PAUSED:
  if use-reference-timestamps && ntp-offset explicit: warning, ntp_offset = NONE
  elif use-reference-timestamps: reference-meta mode
  else: ntp_offset = prop_ntp_offset
  set_d_bit = TRUE; set_e_bit = FALSE; set_t_bit = FALSE
PAUSED → READY:
  purge_cached_buffer_and_events(); gst_segment_init(UNDEFINED)
```

#### 4.8. Caching and latency

When `set-e-bit` or `set-t-bit` is active, the element **holds one
buffer/list in the cache** (`self->buffer` / `self->list`) so it knows
which element is last in the segment → hence **+1 packet of latency**.
Without these flags, buffers are modified and sent immediately (zero
extra latency).

#### 4.9. Position in the pipeline

**After** the payloader (`rtph264pay`, `rtph265pay`, …), **before** the
RTP/UDP sink (`udpsink`) or in the `pay0` factory of
`gst-rtsp-server`. Works on `application/x-rtp`.

#### 4.10. Error paths

| Situation | Result |
|---|---|
| No clock in `PAUSED` with auto `ntp-offset` | `GST_ELEMENT_ERROR(STREAM, FAILED, "Can not guess ntp-offset with no clock")`. |
| No time segment | `GST_ELEMENT_ERROR(STREAM, FAILED, "did not receive a time segment yet")`. |
| `set/get_extension_data` failed | `GST_ELEMENT_ERROR(STREAM, FAILED, "Failed to set/get extension data")`. |
| `use-reference-timestamps` but no meta on the buffer | `GST_ERROR("UTC reference timestamp not found")`, `time=NONE`; with `drop-out-of-segment=true` → `FALSE`/error. |

#### 4.11. Example (replay-server fragment)

```bash
# inside the media factory of gst-rtsp-server (pay0 launch string):
( filesrc location=clip.mkv ! matroskademux ! h264parse !       \
  rtph264pay name=pay0 pt=96 !                                  \
  rtponviftimestamp set-e-bit=true set-t-bit=true cseq=42 )

# with an explicit ntp-offset (ns since 1900) instead of auto:
... ! rtph264pay ! rtponviftimestamp ntp-offset=16780000000000000000 ! udpsink

# a source with accurate UTC (PTP) — reference-timestamp meta:
... ! rtph264pay ! rtponviftimestamp use-reference-timestamps=true ! udpsink
```

#### 4.12. Inspection

```bash
gst-inspect-1.0 rtponvifparse
gst-inspect-1.0 rtponviftimestamp
gst-inspect-1.0 rtponvif          # the whole plugin: both elements
```

## Requirements

- GStreamer ≥ 1.20 with `libgstpython` (gst-python)
- Python 3.10+ with `gi` / `PyGObject`
- Python packages: `python-onvif-zeep` (or compatible), `zeep`,
  `requests` (pulled in by `dlstreamer.onvif`)
- A C compiler (`gcc` / `clang`) + `pkg-config` + `python3-config`

## Building the shim

```bash
./scripts/build_onvif_shim.sh
# → python/dlstreamer/onvif-plugin/libgstdlsonvif.so
```

The script uses `pkg-config gstreamer-1.0` and `python3-config --embed`.

## Running

### Variant A — running from the source tree (recommended during dev)

```bash
source python3venv/bin/activate
source scripts/setup_dls_env.sh

export GST_PLUGIN_PATH="$PWD/python/dlstreamer/onvif-plugin:$GST_PLUGIN_PATH"
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# Clear the registry cache if it was previously built without the shim:
rm -rf ~/.cache/gstreamer-1.0/

gst-inspect-1.0 dlsonvif
# → displays the plugin + "1 features: +-- 1 device providers"
```

The C shim automatically appends its directories to `sys.path`:
- `<so_dir>/python/` — to find `onvifdeviceprovider.py`
- `<so_dir>/../../` — to find the `dlstreamer.onvif` package

Thanks to this, in Variant A `PYTHONPATH` is optional for plugin
loading itself — it is only required for a direct `import
dlstreamer.onvif` from the application.

### Variant B — system installation

```bash
# 1. Copy the C plugin into GStreamer's plugin directory:
sudo cp python/dlstreamer/onvif-plugin/libgstdlsonvif.so \
        /opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0/

# 2. Expose the Python module on sys.path (one of the following):
#    a) copy the whole python/dlstreamer/onvif-plugin/python/ directory to
#       a known path and set PYTHONPATH permanently, or
#    b) install `dlstreamer-onvif` as a package into site-packages
#       (if you have an external distribution), or
#    c) leave the plugin in the tree (Variant A) and pass GST_PLUGIN_PATH.

# 3. Clear the cache:
sudo rm -rf ~/.cache/gstreamer-1.0/ /root/.cache/gstreamer-1.0/
```

After installation, `gst-inspect-1.0 dlsonvif` works in any terminal
without setting `GST_PLUGIN_PATH`.

## Usage

### `gst-device-monitor-1.0` (pure CLI)

```bash
gst-device-monitor-1.0 -f Source/Network/ONVIF
# Probing devices...
# Monitoring devices, waiting for devices to be removed or new devices to be added...
# Device found:
#   name  : <camera friendly name>
#   class : Source/Network/ONVIF
#   caps  : onvif-profile, media=video, encoding-name=H264, width=1920, ...
#   properties:
#     onvif.host = 192.168.x.y
#     onvif.port = 80
#     onvif.xaddr = http://192.168.x.y/onvif/device_service
#     ...
#   gst-launch-1.0 rtspsrc location=rtsp://... ! ...
```

**Important:** use the `-f` / `--follow` flag. Without it,
`gst-device-monitor-1.0` takes a snapshot immediately after
`monitor.start()` and ends the process before our WS-Discovery loop
(~5 s) manages to publish the first device — you will see only
`Probing devices...` and an empty list.

### `Gst.DeviceMonitor` from Python

```python
from gi.repository import Gst

Gst.init(None)
monitor = Gst.DeviceMonitor.new()
monitor.add_filter("Source/Network/ONVIF", None)
monitor.start()

for dev in monitor.get_devices():
    print(dev.get_display_name(), "→", dev.get_properties().to_string())

monitor.stop()
```

### The `onvifcm` library

```python
import onvifcm

onvifcm.init_discovery()
onvifcm.start_discovery()
for cam in onvifcm.list_discovered_cameras():
    print(cam)
```

`import dlstreamer.onvif` is performed indirectly by `onvifcm` and
auto-registers the provider in-process (in-process static plugin),
independently of the C shim.

### Passing credentials to a camera

The provider emits devices without injected credentials in
`device.launch_line` (hostname/port only). To pass a username and
password, send a custom event to the `rtspsrc` element (or to the
`Gst.Device` itself before `create_element()`):

```python
s = Gst.Structure.new_empty("onvif-set-credentials")
s.set_value("user", "admin")
s.set_value("password", "1234")
event = Gst.Event.new_custom(Gst.EventType.CUSTOM_DOWNSTREAM, s)
device.send_event(event)
```

## Diagnostics

| Symptom | Most common cause |
|---|---|
| `No such element or plugin 'dlsonvif'` | `GST_PLUGIN_PATH` does not point to the directory with `libgstdlsonvif.so`, or the registry cache is stale — clear `~/.cache/gstreamer-1.0/`. |
| `gst-inspect-1.0 onvifdeviceprovider` → nothing | Expected. `gst-inspect-1.0 <name>` only searches element factories. Use `gst-inspect-1.0 dlsonvif` or `gst-device-monitor-1.0`. |
| `gst-device-monitor-1.0 Source/Network/ONVIF` shows only `Probing devices...` and exits | Missing the `-f` / `--follow` flag — the tool exits before WS-Discovery (~5 s) manages to publish the devices. |
| `0 features` in `gst-inspect-1.0 dlsonvif` | In-process auto-registration from `dlstreamer.onvif.__init__` ran inside the scanner subprocess and stole the feature. Check that the `gst-plugin-scanner` detection (`/proc/self/comm`) works. |
| `undefined symbol: PyTuple_Type` when loading the plugin | The shim did not `dlopen(libpython3.X.so.1.0, RTLD_GLOBAL)` before `Py_Initialize()`. This should be done automatically by `ensure_libpython_global()`. |
| No devices despite an available camera | A firewall is blocking multicast UDP 3702 (WS-Discovery). Check `iptables`/`ufw`. You can increase the timeout: the `discovery-timeout` property on the provider. |

Enable logs:

```bash
GST_DEBUG="onvifshim:5,GST_REGISTRY:4,GST_PLUGIN_LOADING:4" \
  gst-device-monitor-1.0 Source/Network/ONVIF 2>&1 | less
```

On the Python side, set `GST_DEBUG_NO_COLOR=1` + the regular GStreamer
logs will show messages from the module (the `onvifdeviceprovider`
channel).

## Why two plugins?

- **`dlsonvif`** (C shim) — the device provider wrapper. It must be in a
  `.so`, because GStreamer caches features only from real on-disk
  plugins; a pure-Python `gst_plugin_register_static_full` is visible
  in-process only.
- **`python` / `dls_onvif_src.py`** — a `Gst.Bin` element loaded by the
  existing `libgstpython`. A different use case (pipeline element), a
  different registration mechanism (`__gstelementfactory__`).

The C plugin name **must differ** from the feature name
(`onvifdeviceprovider`), otherwise `gst_registry_add_feature`
automatically creates a static plugin of the same name and steals the
registration.
