# ONVIF Camera Discovery Sample 

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Module Documentation](#module-documentation)
4. [API Reference](#api-reference)
5. [Configuration](#configuration)
6. [Usage Examples](#usage-examples)



---

## Overview

This sample demonstrates automatic discovery and streaming from ONVIF-compliant IP cameras using DL Streamer. The application discovers cameras on the local network, retrieves their streaming profiles, and launches concurrent DL Streamer pipelines for video processing.

### Key Capabilities
- **Automatic Camera Discovery**: Uses WS-Discovery multicast protocol to find ONVIF cameras
- **Profile Extraction**: Retrieves detailed video/audio encoder configurations
- **Concurrent Streaming**: Manages multiple DL Streamer pipelines simultaneously
- **Thread-safe Output**: Handles stdout/stderr from multiple processes
- **Graceful Shutdown**: Properly terminates all child processes on exit

### Technology Stack
- **ONVIF Protocol**: Industry-standard IP camera communication protocol
- **WS-Discovery**: Multicast network discovery (SOAP over UDP)
- **GStreamer**: Multimedia framework for video processing
- **Python 3.x**: Core implementation language

---

## Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    dls_onvif_sample.py                      │
│                  (Main Orchestrator)                        │
│  - CLI argument parsing                                     │
│  - Process lifecycle management                             │
│  - Multi-threaded output handling                           │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ├─────────────────────────────────────────────┐
                 │                                             │
                 ▼                                             ▼
┌────────────────────────────────┐      ┌──────────────────────────────────┐
│  dls_onvif_discovery_utils.py  │      │      dls_onvif_data.py          │
│  (Discovery & Query)           │      │   (Data Structures)             │
│  - WS-Discovery multicast      │      │   - ONVIFProfile class          │
│  - XML response parsing        │      │   - Profile properties          │
│  - Media profile retrieval     │      │   - Configuration storage       │
│  - RTSP URI extraction         │      │                                  │
│  - Config file management      │      │                                  │
└────────────────────────────────┘      └──────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────┐
│                    config.json                              │
│  (Pipeline Configuration)                                   │
│  - Maps camera IPs to GStreamer pipeline elements          │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

```mermaid
sequenceDiagram
    participant User
    participant Main as dls_onvif_sample.py
    participant Utils as discovery_utils
    participant Network
    participant Camera as ONVIF Camera
    participant GStreamer as DL Streamer Process

    User->>Main: python dls_onvif_sample.py --user admin --password pass
    Main->>Utils: discover_onvif_cameras()
    Utils->>Network: Send WS-Discovery Probe (multicast)
    Camera->>Network: ProbeMatch Response
    Network->>Utils: Parse XML response
    Utils->>Main: Return camera list [{'hostname': IP, 'port': PORT}]
    
    loop For each discovered camera
        Main->>Camera: Create ONVIFCamera(hostname, port, user, pass)
        Main->>Utils: get_commandline_by_key(config.json, hostname)
        Utils->>Main: Return pipeline elements
        Main->>Camera: camera_profiles(camera_obj)
        Camera->>Main: Return profile list with RTSP URLs
        
        loop For each profile
            Main->>Main: prepare_commandline(rtsp_url, pipeline_elements)
            Main->>GStreamer: run_single_streamer(command)
            GStreamer-->>Main: Return process handle
            Main->>Main: Append to running_processes[]
        end
    end
    
    Main->>User: "Press Ctrl+C to stop..."
    Main->>GStreamer: Wait for all processes
    User->>Main: Ctrl+C (KeyboardInterrupt)
    Main->>GStreamer: Terminate all processes
    Main->>User: "All processes stopped."
```

---

## Module Documentation

### 1. `dls_onvif_sample.py`

**Purpose**: Main entry point that orchestrates camera discovery and pipeline execution.

#### Functions

##### `run_single_streamer(gst_command: str) -> subprocess.Popen`

Launches a GStreamer DL Streamer pipeline in non-blocking mode with thread-safe output handling.

**Parameters:**
- `gst_command` (str): Complete GStreamer command line string

**Returns:**
- `subprocess.Popen`: Process handle, or `None` on failure

**Implementation Details:**
- Uses `subprocess.Popen` with `shell=True` for flexible command execution
- Captures `stdout` and `stderr` to prevent buffer blocking
- Spawns daemon threads to read output streams asynchronously
- Tags output with process ID for multi-process identification
- Uses `bufsize=1` for line-buffered output

**Example:**
```python
command = "gst-launch-1.0 rtspsrc location=rtsp://10.0.0.1/stream ! decodebin ! autovideosink"
process = run_single_streamer(command)
if process:
    print(f"Started streamer with PID: {process.pid}")
```

---

##### `prepare_commandline(camera_rtsp_url: str, pipeline_elements: str) -> List[str]`

Constructs a complete GStreamer command line from RTSP URL and pipeline elements.

**Parameters:**
- `camera_rtsp_url` (str): RTSP stream URL (e.g., `rtsp://10.0.0.1:554/stream`)
- `pipeline_elements` (str): GStreamer pipeline chain (e.g., `! decodebin ! autovideosink`)

**Returns:**
- `List[str]`: Complete command line ready for execution

**Raises:**
- `ValueError`: If either parameter is empty or None

**Example:**
```python
rtsp_url = "rtsp://192.168.1.100:554/profile1"
pipeline = "! rtph264depay ! h264parse ! avdec_h264 ! autovideosink"
command = prepare_commandline(rtsp_url, pipeline)
# Result: "gst-launch-1.0 rtspsrc location=rtsp://192.168.1.100:554/profile1 ! rtph264depay ! h264parse ! avdec_h264 ! autovideosink"
```

---

#### Main Execution Flow

```python
if __name__ == "__main__":
```

1. **Argument Parsing**
   - `--verbose`: Enable detailed logging (default: False)
   - `--user`: ONVIF username for camera authentication
   - `--password`: ONVIF password for camera authentication

2. **Camera Discovery**
   - Broadcasts WS-Discovery probe on local network
   - Collects responses for 5 seconds
   - Returns list of discovered cameras

3. **Profile Retrieval & Pipeline Launch**
   - For each camera:
     - Authenticate using provided credentials
     - Load pipeline configuration from `config.json`
     - Query all available media profiles
     - Extract RTSP URL from each profile
     - Launch DL Streamer with constructed command
     - Store process handle in `running_processes[]`

4. **Process Management**
   - Wait for all processes to complete
   - Handle `KeyboardInterrupt` (Ctrl+C) gracefully
   - Terminate all child processes on exit
   - Report process status

---

### 2. `dls_onvif_discovery_utils.py`

**Purpose**: Utility functions for ONVIF camera discovery and media profile management.

#### Functions

##### `discover_onvif_cameras(verbose: bool = False) -> List[dict]`

Discovers ONVIF cameras on the local network using WS-Discovery multicast protocol.

**Parameters:**
- `verbose` (bool): Enable detailed discovery logging

**Returns:**
- `List[dict]`: List of camera dictionaries with keys:
  - `hostname` (str): Camera IP address
  - `port` (int): ONVIF service port

**Protocol Details:**
- **Multicast Group**: `239.255.255.250:3702` (ONVIF standard)
- **Timeout**: 5 seconds for receiving responses
- **Message Format**: SOAP 1.2 with WS-Discovery extensions

**SOAP Probe Message:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope" 
               xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing" 
               xmlns:tns="http://schemas.xmlsoap.org/ws/2005/04/discovery">
    <soap:Header>
        <wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>
        <wsa:MessageID>uuid:probe-message</wsa:MessageID>
        <wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>
    </soap:Header>
    <soap:Body>
        <tns:Probe>
            <tns:Types>dn:NetworkVideoTransmitter</tns:Types>
        </tns:Probe>
    </soap:Body>
</soap:Envelope>
```

**Response Parsing:**
- Extracts `XAddrs` element containing service endpoint URL
- Parses hostname and port from URL
- Filters for valid `ProbeMatches` responses
- Deduplicates camera entries

**Example:**
```python
cameras = discover_onvif_cameras(verbose=True)
for cam in cameras:
    print(f"Found camera at {cam['hostname']}:{cam['port']}")
```

---

##### `camera_profiles(client, verbose: bool = False) -> List[ONVIFProfile]`

Queries an ONVIF camera for available media profiles and extracts detailed configuration.

**Parameters:**
- `client`: ONVIF client instance (from `onvif-zeep` library)
- `verbose` (bool): Enable detailed profile logging

**Returns:**
- `List[ONVIFProfile]`: List of profile objects containing:
  - Profile metadata (name, token, fixed status, video_source_configuration, video_encoder_configuration)
  - Video source configuration
  - Video encoder settings (codec, resolution, bitrate, quality)
  - Audio configurations (if available)
  - PTZ settings (if available)
  - RTSP streaming URI

**Retrieved Information:**

| Category | Properties |
|----------|------------|
| **Profile** | name, token, fixed |
| **Video Source** | name, token, source_token, bounds (x, y, width, height) |
| **Video Encoder** | encoding (H264/H265/MJPEG), resolution, quality, framerate_limit, bitrate_limit, GOP length |
| **PTZ** | name, token, node_token |
| **Streaming** | rtsp_url |

**Error Handling:**
- `AttributeError`: Profile missing expected attributes
- `KeyError`: Malformed ONVIF response
- `TimeoutError`: Network communication timeout
- `ConnectionError`: Camera unreachable

**Example:**
```python
from onvif import ONVIFCamera

camera = ONVIFCamera('192.168.1.100', 80, 'admin', 'password')
profiles = camera_profiles(camera, verbose=True)

for profile in profiles:
    print(f"Profile: {profile.name}")
    print(f"  Resolution: {profile.vec_resolution}")
    print(f"  RTSP URL: {profile.rtsp_url}")
```

---

##### `get_commandline_by_key(file_path: str, key: str, verbose: bool = False) -> Optional[str]`

Retrieves GStreamer pipeline elements from JSON configuration file.

**Parameters:**
- `file_path` (str): Path to configuration JSON file
- `key` (str): Lookup key (typically camera IP address)
- `verbose` (bool): Enable logging

**Returns:**
- `Optional[str]`: Pipeline elements string, or `None` if key not found

**Configuration Format:**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink",
    "192.168.1.101": "! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc ! autovideosink"
}
```

**Error Handling:**
- `FileNotFoundError`: Config file doesn't exist
- `json.JSONDecodeError`: Invalid JSON syntax
- Generic exceptions logged and return `None`

---

##### `extract_xaddrs(xml_string: str) -> Optional[str]`

Extracts the XAddrs element from WS-Discovery probe response.

**Parameters:**
- `xml_string` (str): Raw XML response from camera

**Returns:**
- `Optional[str]`: XAddrs URL or `None` if not found

**Example:**
```python
response = '''<?xml version="1.0"?>
<soap:Envelope>
    <wsdd:XAddrs>http://192.168.1.100:80/onvif/device_service</wsdd:XAddrs>
</soap:Envelope>'''

xaddrs = extract_xaddrs(response)
# Result: "http://192.168.1.100:80/onvif/device_service"
```

---

##### `parse_xaddrs_url(xaddrs: str) -> dict`

Parses XAddrs URL into structured components.

**Parameters:**
- `xaddrs` (str): Full XAddrs URL string

**Returns:**
- `dict`: URL components:
  - `full_url` (str): Complete URL
  - `scheme` (str): Protocol (http/https)
  - `hostname` (str): IP address or hostname
  - `port` (int): Service port
  - `path` (str): URL path
  - `base_url` (str): Scheme + netloc

**Example:**
```python
xaddrs = "http://192.168.1.100:8080/onvif/device_service"
parsed = parse_xaddrs_url(xaddrs)
# Result: {
#     'full_url': 'http://192.168.1.100:8080/onvif/device_service',
#     'scheme': 'http',
#     'hostname': '192.168.1.100',
#     'port': 8080,
#     'path': '/onvif/device_service',
#     'base_url': 'http://192.168.1.100:8080'
# }
```

---

### 3. `dls_onvif_data.py`

**Purpose**: Data structures for storing ONVIF camera profile information.

#### Classes

##### `ONVIFProfile`

Contains ONVIF camera profile data with relevant configuration details for this sample.

**Attributes:**

| Property | Type | Description |
|----------|------|-------------|
| `name` | str | Profile name |
| `token` | str | Unique profile identifier |
| `fixed` | bool | Whether profile is immutable |
| `video_source_configuration` |  str | Video source configuration of the ONVIF profile |
| `video_encoder_configuration` | str | Video encoder configuration of the ONVIF profile |
| `rtsp_url` | str | RTSP streaming endpoint |
| **Video Source Configuration** |
| `vsc_name` | str | Video source name |
| `vsc_token` | str | Video source token |
| `vsc_source_token` | str | Source reference token |
| `vsc_bounds` | dict | Video bounds (x, y, width, height) |
| **Video Encoder Configuration** |
| `vec_name` | str | Encoder configuration name |
| `vec_token` | str | Encoder configuration token |
| `vec_encoding` | str | Codec (H264, H265, MJPEG, etc.) |
| `vec_resolution` | dict | Resolution (width, height) |
| `vec_quality` | int | Quality level (1-100) |
| `vec_rate_control` | int | Rate control of the Video Encoder Configuration |
| `vec_multicast` | int | Multicast of the Video Encoder Configuration |
| **Audio Encoder Configuration** |
| `aec_name` | str | Audio encoder name |
| `aec_token` | str | Audio encoder token  |
| `aec_encoding` | str | Encoding of the audio encoder configuration |
| `aec_bitrate` | int | Bitrate of the audio encoder configuration |
| `aec_sample_rate` | int | Sample rate of the audio encoder configuration |
| **PTZ Configuration** |
| `ptz_name` | str | PTZ configuration name |
| `ptz_token` | str | PTZ configuration token |
| `ptz_node_token` | str | PTZ node identifier |

**Implementation:**
- All properties use `@property` decorators for encapsulation
- Initializes with default values (empty strings, empty dicts, zero integers)
- Provides getter/setter methods for all attributes

**Example Usage:**
```python
profile = ONVIFProfile()
profile.name = "MainStream"
profile.vec_resolution = {'width': 1920, 'height': 1080}
profile.vec_encoding = "H264"
profile.rtsp_url = "rtsp://192.168.1.100/stream1"

print(f"{profile.name}: {profile.vec_resolution['width']}x{profile.vec_resolution['height']}")
```

---

## Configuration

### `config.json`

Maps camera IP addresses to custom GStreamer pipeline command lines.

**Format:**
```json
{
    "<camera_ip>": "<gstreamer_pipeline_elements>"
}
```

**Example:**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink",
    "192.168.1.101": "! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc ! autovideosink",
    "192.168.1.102": "! rtph264depay ! h264parse ! video/x-h264 ! mpegtsmux ! filesink location=output.ts"
}
```

**Pipeline Elements Breakdown:**

| Element | Purpose |
|---------|---------|
| `rtph264depay` | Extracts H.264 stream from RTP packets |
| `h264parse` | Parses H.264 bitstream |
| `avdec_h264` | Software H.264 decoder (libav) |
| `vaapih264dec` | Hardware-accelerated H.264 decoder (VA-API) |
| `videoconvert` | Converts video format/colorspace |
| `vaapipostproc` | Hardware video post-processing |
| `autovideosink` | Automatic video output selection |
| `filesink` | Saves stream to file |

**Best Practices:**
- Use hardware decoders (`vaapih264dec`) for better performance
- Match decoder to camera's codec (H.264, H.265, MJPEG)
- Include `! videoconvert` before display sinks
- Test pipelines with `gst-launch-1.0` before adding to config

---

### `requirements.txt`

**Current Dependencies:**
```
onvif-zeep
```

**Package Details:**

| Package | Version | Purpose |
|---------|---------|---------|
| `onvif-zeep` | Latest | ONVIF client library using Zeep SOAP client |

**Installation:**
```bash
pip install -r requirements.txt
```

**Additional System Requirements:**
- **GStreamer 1.0+**: Core multimedia framework
- **GStreamer plugins**: base, good, bad, ugly (for codec support)
- **VA-API drivers** (optional): For hardware acceleration on Intel platforms

**Installation Commands (Ubuntu/Debian):**
```bash
# Python dependencies
pip install onvif-zeep

# GStreamer core and plugins
sudo apt-get install \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-vaapi

# Intel Media SDK (optional, for Intel hardware acceleration)
sudo apt-get install intel-media-va-driver-non-free
```

---
**Docker**


To run the Docker image with network access for ONVIF camera discovery:

```bash
docker run --rm -it --network host  <image_name>
```

**Command Explanation:**
- `--network host`: Enables host network mode for multicast discovery and direct camera access
- `--rm`: Automatically removes container after exit
- `-it`: Interactive terminal mode

**Note:** Host network mode is required for WS-Discovery multicast (239.255.255.250:3702) to function properly.

---

## Usage Examples

### Basic Usage

**Discover cameras and launch default pipelines:**
```bash
python dls_onvif_sample.py --user admin --password admin123
```

**With verbose logging:**
```bash
python dls_onvif_sample.py --user admin --password admin123 --verbose True
```

---

### Complete Workflow Example

**Step 1: Create configuration file**
```bash
cat > config.json << EOF
{
    "192.168.1.100": "! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink"
}
EOF
```

**Step 2: Test camera connectivity**
```bash
# Manually test RTSP stream
gst-launch-1.0 rtspsrc location=rtsp://admin:admin123@192.168.1.100/stream ! \
    rtph264depay ! h264parse ! avdec_h264 ! autovideosink
```

**Step 3: Run discovery and streaming**
```bash
python dls_onvif_sample.py \
    --user admin \
    --password admin123 \
    --verbose True
```

**Expected Output:**
```
Discovered ONVIF cameras:
Hostname: 192.168.1.100, Port: 80
DL Streamer started (PID:12345)
Executing command line for 192.168.1.100: gst-launch-1.0 rtspsrc location=rtsp://192.168.1.100/stream ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink
[PID:12345] OUT: Setting pipeline to PAUSED ...
[PID:12345] OUT: Pipeline is PREROLLING ...
[PID:12345] OUT: Pipeline is PREROLLED ...
[PID:12345] OUT: Setting pipeline to PLAYING ...
[PID:12345] OUT: New clock: GstSystemClock

Press Ctrl+C to stop all processes and exit.
```

**Step 4: Stop streaming**
```
Press Ctrl+C

Stopping all processes...
Terminated process PID: 12345
All processes stopped.
```

---

### Advanced Pipeline Examples

**Hardware-accelerated decoding (Intel VA-API):**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! vaapih264dec ! vaapipostproc ! autovideosink"
}
```

**Save to file:**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! video/x-h264 ! mp4mux ! filesink location=/tmp/camera1.mp4"
}
```

**Multiple operations (display + save):**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! tee name=t ! queue ! avdec_h264 ! autovideosink t. ! queue ! mp4mux ! filesink location=output.mp4"
}
```

**DL Streamer with object detection:**
```json
{
    "192.168.1.100": "! rtph264depay ! h264parse ! avdec_h264 ! gvadetect model=/path/to/model.xml ! gvawatermark ! autovideosink"
}
```

---


---

## License

Copyright (C) 2026 Intel Corporation

SPDX-License-Identifier: MIT

---

## References

- **ONVIF Specification**: https://www.onvif.org/specs/core/ONVIF-Core-Specification.pdf
- **WS-Discovery**: http://docs.oasis-open.org/ws-dd/discovery/1.1/wsdd-discovery-1.1-spec.html
- **GStreamer Documentation**: https://gstreamer.freedesktop.org/documentation/
- **DL Streamer**: https://github.com/dlstreamer/dlstreamer
- **onvif-zeep Library**: https://github.com/FalkTannhaeuser/python-onvif-zeep

---

## Support

For issues, questions, or contributions, please refer to the main DL Streamer project repository.
