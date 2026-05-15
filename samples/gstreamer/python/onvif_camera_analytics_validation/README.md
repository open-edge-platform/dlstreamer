# ONVIF Camera Analytics Validation

Event-driven validation for any **ONVIF-enabled camera**. Connects to an RTSP
stream and an MQTT event broker. Uses DLStreamer's **gvagenai** element to run
continuous **VLM (Visual Language Model)** inference via **OpenVINO GenAI** on
Intel hardware (CPU/iGPU/NPU) directly inside the GStreamer pipeline. When an
MQTT analytics event arrives, pairs it with the latest VLM inference result
and displays everything on a **live web dashboard**.



```mermaid
graph LR
    A["ONVIF Camera"] -->|"RTSP stream"| B["DLStreamer Pipeline"]
    B -->|"frames"| D["gvagenai\n(VLM inference)"]
    A -->|"MQTT events"| C["MQTTEventListener"]
    C -->|"triggers"| D
    D -->|"VLM text"| F["Web Dashboard\nhttp://localhost:8080"]
    C -->|"camera event"| F
    B -->|"JPEG frame"| F

    style A fill:#e3f2fd,stroke:#1565C0
    style B fill:#fff3e0,stroke:#E65100
    style C fill:#fff3e0,stroke:#E65100
    style D fill:#e8f5e9,stroke:#2E7D32
    style F fill:#f3e5f5,stroke:#9C27B0
```

## Prerequisites

- Python 3.10+
- An ONVIF-enabled camera with MQTT event publishing
- An MQTT broker (e.g. Mosquitto)

## Setup

### 1. Install the MQTT broker

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable mosquitto && sudo systemctl start mosquitto
```

Ensure `/etc/mosquitto/mosquitto.conf` allows external connections:

```text
listener 1883
allow_anonymous true
```

```bash
sudo systemctl restart mosquitto
```

### 2. Configure camera MQTT events

Point your camera's MQTT settings to `tcp://<broker-ip>:1883` with topic
prefix `onvif/analytics/`. The exact steps vary by manufacturer — refer to
your camera's documentation.

Verify events arrive:

```bash
mosquitto_sub -h localhost -t "onvif/analytics/#" -v
```

### 3. Set up DLStreamer environment

```bash
cd dlstreamer/samples/gstreamer/python/onvif_camera_analytics_validation
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
```

### 4. Create virtual environment and install dependencies

```bash
python3 -m venv --system-site-packages .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 5. Export a VLM model (run once)

```bash
hf download OpenVINO/gemma-3-4b-it-int8-ov --cache-dir ./Gemma3-4B
```

Gemma 3 4B (~3.3 GB in int4) is recommended — compact enough for
**Intel Core Ultra** laptops with good object recognition.

> **Note:** Gated models require a [Hugging Face](https://huggingface.co/)
> account. Accept the license on the model page, then:
> ```bash
> pip install hf && hf auth login
> ```

## Quick Start

```bash
python3 onvif_camera_analytics_validation.py \
    --camera-ip 192.168.1.100 \
    --onvif-user root --onvif-pass root \
    --model-path ./Gemma3-4B
```

> If `--rtsp-uri` is omitted, the app discovers the stream URI via ONVIF.
> Common RTSP paths by manufacturer:
>
> | Manufacturer | RTSP URI |
> |---|---|
> | Axis | `rtsp://<IP>:554/axis-media/media.amp` |
> | i-PRO | `rtsp://<IP>:554/MediaInput/stream_1` |
> | Hanwha | `rtsp://<IP>:554/profile2/media.smp` |
> | Bosch | `rtsp://<IP>:554/rtsp_tunnel` |

Open **http://localhost:8080** to view the dashboard. When the camera detects objects and publishes MQTT events, the dashboard will show the camera's reported analytics alongside the VLM's independent description of the same scene.

### Example Output

#### Dashboard
![ONVIF Camera Analytics Validation Dashboard](./onvif_camera_analytics_validation_sample.png)


### Dangerous Object Detection

Beyond validating standard object counts (people, vehicles, animals), the VLM
prompt instructs the model to check for **weapons and dangerous objects** —
guns, knives, firearms — that the camera's built-in analytics may not detect.
When an MQTT event arrives reporting detected objects, the VLM independently
examines the frame and flags any threatening items the camera missed. This
enables a security-focused validation layer where conventional object
detection handles routine counting while the VLM adds threat-awareness
without requiring a dedicated weapon-detection model.


## CLI Options

| Flag | Default | Description |
|------|---------|-------------|
| `--camera-ip` | `192.168.1.100` | Camera IP address |
| `--onvif-port` | `80` | ONVIF HTTP port |
| `--onvif-user` / `--onvif-pass` | `admin` | ONVIF credentials |
| `--rtsp-uri` | (auto via ONVIF) | RTSP URI override |
| `--model-path` | `$GENAI_MODEL_PATH` | OpenVINO VLM model directory |
| `--device` | `CPU` | Intel device: `CPU`, `GPU`, `NPU`, `AUTO` |
| `--prompt` | (object listing) | VLM prompt |
| `--frame-rate` | `1` | VLM inference rate (fps) via gvagenai |
| `--max-tokens` | `150` | Max VLM generation tokens |
| `--mqtt-broker` | `localhost` | MQTT broker address |
| `--mqtt-port` | `1883` | MQTT broker port |
| `--mqtt-topics` | `onvif/analytics/#` | MQTT subscribe topics |
| `--web-port` | `8080` | Web dashboard port |

## Web Dashboard

| Panel | Content |
|-------|---------|
| **Camera Frame** | Processed frame captured at the moment of the MQTT event |
| **VLM Description** | VLM text from inference on that frame |
| **MQTT Event** | Raw event data from the camera |
| **Event Navigation** | Previous/Next/Latest buttons to browse event history |

## Files

| File | Purpose |
|------|--------|
| `onvif_camera_analytics_validation.py` | Main app — DLStreamer pipeline with gvagenai, MQTT event pairing, web dashboard |
| `util.py` | ONVIF SOAP client, MQTT listener, event parsing |
| `requirements.txt` | Python dependencies |

## DLStreamer Pipeline

The GStreamer pipeline ingests the RTSP stream and splits it via a `tee`
into two branches:

```
urisourcebin → decodebin3 → videoconvertscale → video/x-raw,format=RGB → tee
                                                                           │
                ┌──────────────────────────────────────────────────────────┘
                │
                ├─► queue (leaky) → valve → gvagenai → jpegenc → appsink
                │       Branch 1: On-demand VLM inference + frame capture
                │
                └─► queue (leaky) → videorate (1 fps) → jpegenc → appsink
                        Branch 2: Continuous JPEG preview (fallback)
```

| Element | Role |
|---------|------|
| `urisourcebin` | Connects to the camera's RTSP stream (TCP transport) with auto-reconnect |
| `decodebin3` | Decodes H.264/H.265 video |
| `videoconvertscale` | Converts to RGB for downstream processing |
| `tee` | Splits the stream into two branches |
| `valve` (`vlm_valve`) | Controls when frames reach gvagenai — closed by default, opened on MQTT event |
| `gvagenai` | Runs VLM inference (e.g. Gemma 3 4B) via OpenVINO GenAI on CPU/GPU/NPU |
| `jpegenc` (Branch 1) | Encodes the exact VLM-processed frame to JPEG for the dashboard |
| `appsink` (`vlm_jpeg_sink`) | Captures the JPEG of the frame VLM actually processed |
| `videorate` + `jpegenc` (Branch 2) | Continuous 1 fps JPEG snapshots (fallback if VLM frame unavailable) |
| `appsink` (`jpeg_sink`) | Delivers fallback JPEG buffers to the Python application |

**On-demand inference flow:** The valve starts closed after initial model load.
When an MQTT event with object detection data arrives, the application opens
the valve (`drop=false`), letting one frame through to `gvagenai`. A pad probe
on gvagenai's source pad extracts the VLM text result and immediately closes
the valve. The same frame then flows through `jpegenc` → `vlm_jpeg_sink`,
which captures the JPEG and signals readiness — guaranteeing the dashboard
shows the **exact frame** the VLM described.

## ONVIF Profile M

This application is designed around **ONVIF Profile M** — the metadata and
analytics profile for IP cameras. Profile M standardizes how cameras expose
on-device analytics (object detection, counting, classification) so that
third-party systems can consume and validate them.

### What Profile M Provides

| Capability | Description |
|------------|-------------|
| **Analytics Metadata Streaming** | Camera streams bounding boxes, object types, and counts over RTSP metadata or MQTT |
| **Scene Description** | Standardized object classes (Human, Vehicle, Animal, Face, LicensePlate) |
| **Event Rules** | Camera-side rules that trigger MQTT/WS-Notification events on detection |
| **Geolocation & Counting** | Line-crossing counts, area occupancy, directional tracking |
| **Metadata Configuration** | ONVIF API to query/configure which analytics modules are active |

### How This Application Uses Profile M

1. **Discovery** — Queries ONVIF device capabilities and confirms Profile M support via scope `onvif://www.onvif.org/Profile/M`
2. **Event Subscription** — Listens for Profile M analytics events over MQTT (object detection, counting)
3. **Cross-Validation** — Compares the camera's analytics claims against an independent VLM inference on the same frame
4. **Dashboard** — Displays the camera's reported objects alongside the VLM's independent scene description

### Profile M Event Flow

```mermaid
sequenceDiagram
    participant Camera as ONVIF Profile M Camera
    participant MQTT as MQTT Broker
    participant App as Validation App
    participant VLM as gvagenai (VLM)

    Camera->>Camera: On-device analytics (object detection)
    Camera->>MQTT: Publish event (Human detected, count=2)
    MQTT->>App: Deliver event
    App->>VLM: Open valve → inference on current frame
    VLM->>App: "I see 2 people walking..."
    App->>App: Compare camera claim vs VLM result
    App->>App: Display on dashboard
```

### Supported Profile M Object Classes

The application recognizes these ONVIF Profile M analytics object types in
MQTT payloads:

- `Human` / `Person` / `People`
- `Vehicle` / `Car` / `Truck` / `Bus`
- `Animal` / `Dog` / `Cat`
- `Face`
- `LicensePlate`

### Verifying Profile M Support

The application auto-detects Profile M during ONVIF discovery. In the startup
output, look for:

```
  Profile M: FOUND
```

If your camera does not advertise Profile M but still publishes object
detection events via MQTT, the application will still work — Profile M
is checked but not strictly required.

## Camera Compatibility

Works with any ONVIF-compliant camera that publishes MQTT events. Supported
MQTT payload formats:

- **JSON** with `objectCount`/`classCounts` fields
- **JSON** with `data.ObjectType`/`data.IsMotion` fields
- **ONVIF XML** (`tt:MetadataStream` with `tt:Object` elements)
- **ONVIF wsnt:NotificationMessage** XML

ONVIF SOAP discovery (device info, capabilities, profiles, stream URI) uses
unauthenticated requests. Cameras requiring WS-Security digest auth may need
the RTSP URI provided manually via `--rtsp-uri`.

> **Note:** Only MQTT events containing **object detection data** (e.g., Human,
> Vehicle, Animal) are processed through VLM inference. Other event types such
> as tamper detection, audio detection, storage alerts, and generic system
> events are intentionally dropped. This ensures VLM inference is only
> triggered when there are identifiable objects in the scene to validate.

## Troubleshooting

### No MQTT events

1. Check broker: `sudo systemctl status mosquitto`
2. Verify config accepts external connections (`listener 1883`, `allow_anonymous true`)
3. Camera must point to the host's real IP (not `localhost`)
4. Test manually: `mosquitto_sub -h localhost -t "onvif/analytics/#" -v`
5. Events only fire on actual motion/objects — walk in front of the camera

### Dashboard shows "Waiting for inference..."

The first VLM inference takes 30–120 seconds on CPU while the model loads.
Check terminal output for errors.

### Dashboard not updating

Requires an MQTT event with object detection data to trigger VLM inference.
Walk in front of the camera to generate events, or send a test event:

```bash
mosquitto_pub -h localhost -t "onvif/analytics/test" \
  -m '{"objectCount":1,"classCounts":{"Human":1},"objects":[{"type":"Human","confidence":0.9}]}'
```

### Port 8080 in use

```bash
fuser -k 8080/tcp
# or use --web-port <port>
```

### GstApp typelib not found

```bash
sudo apt install gir1.2-gst-plugins-base-1.0
```



---

## Video Credits

- [A Hunter Posing with His Dog and a Shotgun](https://www.pexels.com/video/a-hunter-posing-with-his-dog-and-a-shotgun-6200843/) — Pexels (free to use)