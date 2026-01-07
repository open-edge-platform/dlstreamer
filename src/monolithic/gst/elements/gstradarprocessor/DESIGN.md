# Design Document: GstRadarProcessor Element

## 1. Introduction
The `radarprocessor` is a GStreamer element designed to process millimeter-wave (mmWave) radar signal data. It acts as a bridge between raw radar data ingestion (typically from file sources) and advanced radar signal processing algorithms provided by `libradar`. The element handles data reordering, pre-processing, and interfaces with the underlying radar library to generate point clouds, clusters, and tracking data.

## 2. Architecture Overview

### 2.1 Pipeline Context
The element is designed to work in a pipeline where raw radar data is fed frame-by-frame.
**Example Pipeline:**
```bash
multifilesrc location="/home/user/qianlong/raddet/radar/%06d.bin" start-index=559 ! \
application/octet-stream ! \
radarprocessor radar-config=config.json frame-rate=10 process-interval=1 ! \
gvafpscounter ! \
gvametapublish file-path=result/radar_output.json ! \
fakesink
```
*   **Input:** Raw binary radar data (one file per frame).
*   **Output:** The input buffer is passed through, with processing results attached as metadata (future implementation) or published via `gvametapublish`.

### 2.2 Interface
*   **Element Name:** `radarprocessor`
*   **Classification:** `Filter/Converter`
*   **Sink Pad (Input):** `application/octet-stream`
*   **Source Pad (Output):** `application/x-radar-processed`

## 3. Properties

| Property Name  | Type   | Description |
| :---           | :---   | :---        |
| `radar-config`   | String  | Path to the Radar Configuration JSON file. This file contains critical parameters for signal interpretation (RX/TX count, samples, chirps) and algorithm tuning (CFAR thresholds, clustering parameters). |
| `frame-rate`     | Double  | Target frame rate for the output. If set > 0, the element will throttle processing to match this rate. Default is 0 (no limit). |
| `process-interval` | Integer | Interval for processing frames (e.g., 1 = process every frame, 2 = process every other frame). Useful for reducing CPU load. Default is 1. |

## 4. Functional Description

The `radarprocessor` performs the following sequential operations for each buffer:

### 4.1. Configuration & Validation (Initialization)
On the `start` event, the element parses the `radar-config` JSON file.
*   **Parameter Mapping:** Maps JSON configuration values to the `RadarParam` structure required by `libradar`.
    *   *Note:* Handles type conversions (e.g., mismatching Enums between JSON and C++).
*   **Memory allocation:** Initializes internal buffers for `RadarCube`, `RadarPointClouds`, `ClusterResult`, and `TrackingResult`.

### 4.2. Data Validation
For every incoming buffer:
*   Verifies that the buffer size matches the expected size calculated from the configuration: `Total Size = TRN * Num_Chirps * ADC_Samples * sizeof(complex<float>)`.
*   TRN = Num_RX * Num_TX.

### 4.3. Pre-processing (Data Layout Transformation)
The raw input data typically arrives in `Chirps * TRN * Samples` layout. The element transforms this into a `TRN * Chirps * Samples` layout, which constitutes the `RadarCube`.

### 4.4. Signal Conditioning (DC Removal)
To remove static clutter or leakage:
1.  Calculates the mean (average) of the real and imaginary parts for each sample set.
2.  Subtracts this mean from every sample in the respective set.

### 4.5. Frame Rate Control
If `frame-rate` is set, the element calculates the processing duration and injects `nanosleep` delays to ensure the output stream adheres to the specified FPS.

### 4.6. Statistics
The element maintains internal counters:
*   `frame_id`: Sequential frame counter (starts at 0).
*   `total_frames`: Total processed frames.
*   `total_processing_time`: Cumulative processing duration.
*   **Interop:** Works seamlessly with standard GStreamer elements like `gvafpscounter` for real-time FPS logging.

### 4.7. Core Algorithm Execution
The element prepares the data structures for `libradar` and executes the processing chain:
1.  **RadarCube Preparation:** Maps the pre-processed data to `RadarCube`.
2.  **Detection:** (Planned) Calls `radarDetection` to generate `RadarPointClouds`.
3.  **Clustering:** (Planned) Calls `radarClustering` to generate `ClusterResult`.
4.  **Tracking:** (Planned) Calls `radarTracking` to generate `TrackingResult`.

## 5. Data Structures

The element manages the lifecycle of the following key structures defined in `libradar.h`:
*   **RadarParam:** Configuration parameters.
*   **RadarCube:** 3D matrix of the radar signal.
*   **RadarPointClouds:** Detected reflection points (Range, Speed, Angle, SNR).
*   **ClusterResult:** Grouped point clouds representing objects.
*   **TrackingResult:** Object tracking over time.

## 6. Output & Metadata (Future Work)
The processed results (`PointClouds`, `ClusterResult`, `TrackingResult`) will be:
1.  Attached to the GStreamer buffer as custom metadata.
2.  Compatible with `gvametapublish` to publish results via MQTT/Kafka for downstream consumption.
