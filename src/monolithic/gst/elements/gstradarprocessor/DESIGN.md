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
radarprocessor radar-config=config.json frame-rate=10 publish-result=true publish-path=radar_output.json ! \
gvafpscounter ! \
fakesink
```
*   **Input:** Raw binary radar data (one file per frame).
*   **Output:** The input buffer is passed through with processing results attached as custom GStreamer metadata.

### 2.2 Interface
*   **Element Name:** `radarprocessor`
*   **Classification:** `Filter/Converter`
*   **Sink Pad (Input):** `application/octet-stream`
*   **Source Pad (Output):** `application/x-radar-processed`

## 3. Properties

| Property Name    | Type    | Description |
| :---             | :---    | :---        |
| `radar-config`   | String  | Path to the Radar Configuration JSON file. This file contains critical parameters for signal interpretation (RX/TX count, samples, chirps) and algorithm tuning (CFAR thresholds, clustering parameters). |
| `frame-rate`     | Double  | Target frame rate for the output. If set > 0, the element will throttle processing to match this rate. Default is 0 (no limit). |
| `publish-result` | Boolean | Enable publishing radar processing results to JSON file. Default is FALSE. |
| `publish-path`   | String  | Path to JSON file for publishing results. Default is "radar_results.json". |

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
2.  **Detection:** Calls `radarDetection` to generate `RadarPointClouds` containing detected reflection points.
3.  **Clustering:** Calls `radarClustering` to generate `ClusterResult` grouping nearby points into objects.
4.  **Tracking:** Calls `radarTracking` to generate `TrackingResult` for object tracking over time.

### 4.8. Metadata Attachment
After processing completes, the element attaches a custom `GstRadarProcessorMeta` to the output buffer containing:
*   **frame_id:** Sequential frame identifier.
*   **RadarPointClouds:** Arrays of ranges, speeds, angles, and SNR values.
*   **ClusterResult:** Cluster centers (cx, cy), sizes (rx, ry), average velocities, and cluster indices.
*   **TrackingResult:** Tracked object IDs, positions (x, y), and velocities (vx, vy).

The metadata is implemented with proper lifecycle management (init/free callbacks) and deep-copies all dynamic arrays to ensure data integrity throughout the pipeline.

### 4.9. Results Publishing
When `publish-result` is enabled, the element publishes processing results to a JSON file specified by `publish-path`:
*   Each frame's results are appended to the JSON array.
*   The JSON output contains frame_id, point clouds, clustering results (including point-to-cluster mapping), and tracking results.
*   Publishing happens synchronously after metadata attachment to ensure data consistency.

## 5. Data Structures

The element manages the lifecycle of the following key structures defined in `libradar.h`:
*   **RadarParam:** Configuration parameters.
*   **RadarCube:** 3D matrix of the radar signal.
*   **RadarPointClouds:** Detected reflection points (Range, Speed, Angle, SNR).
*   **ClusterResult:** Grouped point clouds representing objects.
*   **TrackingResult:** Object tracking over time.

## 6. Output & Metadata

The processed results are attached to each GStreamer buffer as custom metadata (`GstRadarProcessorMeta`):

### 6.1. Metadata Structure
The custom metadata type is registered with the GStreamer metadata API and contains:
*   **frame_id** (guint64): Sequential frame number.
*   **RadarPointClouds**: Detected radar points with the following arrays:
    *   `ranges[]`: Distance to each reflection point.
    *   `speeds[]`: Doppler velocity of each point.
    *   `angles[]`: Azimuth angle of each point.
    *   `snrs[]`: Signal-to-noise ratio for each detection.
*   **ClusterResult**: Grouped point clouds with:
    *   `cluster_idx[]`: Cluster index for each point cloud (mapping points to clusters).
    *   `cx[]`, `cy[]`: Cluster center coordinates.
    *   `rx[]`, `ry[]`: Cluster extents.
    *   `av[]`: Average velocity per cluster.
*   **TrackingResult**: Multi-frame object tracking with:
    *   `tracker_ids[]`: Unique identifier for each tracked object.
    *   `x[]`, `y[]`: Current position estimates.
    *   `vx[]`, `vy[]`: Velocity vectors.

### 6.2. Metadata Lifecycle
*   **Registration:** Metadata API type is registered using `gst_meta_api_type_register()` with proper initialization.
*   **Initialization:** `gst_radar_processor_meta_init()` sets all pointers to NULL and counts to zero.
*   **Allocation:** `gst_buffer_add_radar_processor_meta()` creates metadata and performs deep-copy of all arrays.
*   **Cleanup:** `gst_radar_processor_meta_free()` releases all dynamically allocated arrays using `g_free()`.

### 6.3. Downstream Consumption
The metadata can be consumed by downstream GStreamer elements:
*   **fakesink**: Simple sink for testing and benchmarking without visualization.
*   **3ddatarender** (in development): Real-time visualization element that renders tracked objects in Cartesian coordinate system.
*   Custom elements can retrieve metadata using `gst_buffer_get_meta()` with `GST_RADAR_PROCESSOR_META_API_TYPE`.
