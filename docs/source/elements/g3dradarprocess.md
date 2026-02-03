# g3dradarprocess

Processes millimeter-wave (mmWave) radar signal data and generates point clouds, clusters, and tracking results. 
The element acts as a bridge between raw radar data ingestion and advanced radar signal processing algorithms, 
handling data reordering, pre-processing, DC (Direct Current) removal, and interfacing with the underlying radar library.

## Overview

The `g3dradarprocess` element is designed to process raw radar signal data frame-by-frame. It performs the following operations:

- **Data Layout Transformation**: Converts raw data from `Chirps * TRN * Samples` layout to `TRN * Chirps * Samples` layout (RadarCube)
- **Signal Conditioning**: Performs DC removal to eliminate static clutter and leakage
- **Radar Detection**: Generates point clouds containing detected reflection points with range, speed, angle, and SNR
- **Clustering**: Groups nearby points into objects
- **Tracking**: Tracks objects over time across multiple frames

The element attaches custom GStreamer metadata (`GstRadarProcessMeta`) to each buffer containing the processing results.

## Configuration

### Radar Configuration File

The `radar-config` property accepts a path to a JSON configuration file that contains:

- **Signal Interpretation Parameters**: RX/TX count, samples, chirps
- **Algorithm Tuning Parameters**: CFAR thresholds, clustering parameters, tracking parameters

The configuration file must match the format expected by the underlying `libradar` library.

### Properties

| Property        | Type    | Description                                                    | Default              |
|-----------------|---------|----------------------------------------------------------------|----------------------|
| radar-config    | String  | Path to radar configuration JSON file (required)               | null                 |
| frame-rate      | Double  | Target frame rate for output (0 = no limit)                    | 0                    |

## Pipeline Examples

### Basic Processing Pipeline

Process radar data from binary files and display FPS:

```bash
gst-launch-1.0 multifilesrc location="radar/%06d.bin" start-index=559 ! \
  application/octet-stream ! \
  g3dradarprocess radar-config=config.json frame-rate=10 ! \
  fakesink
```

### Processing with JSON Output

Process radar data and publish results to JSON file using `gvametaconvert` and `gvametapublish`:

```bash
gst-launch-1.0 multifilesrc location="radar/%06d.bin" ! \
  application/octet-stream ! \
  g3dradarprocess radar-config=config.json frame-rate=10 ! \
  gvametaconvert format=json json-indent=2 ! \
  gvametapublish file-format=2 file-path=radar_output.json ! \
  fakesink
```

### Processing with Multiple Outputs

Simultaneously publish to JSON file and send to Kafka:

```bash
gst-launch-1.0 multifilesrc location="radar/%06d.bin" ! \
  application/octet-stream ! \
  g3dradarprocess radar-config=config.json ! \
  gvametaconvert format=json ! \
  tee name=t \
  t. ! queue ! gvametapublish file-format=2 file-path=output.json ! fakesink \
  t. ! queue ! gvametapublish method=kafka address=localhost:9092 topic=radar ! fakesink
```

## Input/Output

- **Input Capability**: `application/octet-stream` - Raw binary radar data
- **Output Capability**: `application/x-radar-processed` - Processed data with attached metadata

### Input Requirements

The input buffer size must match the expected size calculated from the configuration:
```
Total Size = TRN * Num_Chirps * ADC_Samples * sizeof(complex<float>)
where TRN = Num_RX * Num_TX
```

## Metadata Structure

The element attaches `GstRadarProcessMeta` to each output buffer containing:

### RadarPointClouds
Detected radar points with arrays of:
- `ranges[]`: Distance to each reflection point
- `speeds[]`: Doppler velocity of each point
- `angles[]`: Azimuth angle of each point
- `snrs[]`: Signal-to-noise ratio for each detection

### ClusterResult
Grouped point clouds representing objects:
- `cluster_idx[]`: Cluster index for each point cloud (mapping points to clusters)
- `cx[]`, `cy[]`: Cluster center coordinates
- `rx[]`, `ry[]`: Cluster extents (size)
- `av[]`: Average velocity per cluster

### TrackingResult
Multi-frame object tracking:
- `tracker_ids[]`: Unique identifier for each tracked object
- `x[]`, `y[]`: Current position estimates
- `vx[]`, `vy[]`: Velocity vectors

## Processing Pipeline

The element performs the following sequential operations for each buffer:

1. **Data Validation**: Verifies buffer size matches expected configuration
2. **Pre-processing**: Transforms data layout from `Chirps * TRN * Samples` to `TRN * Chirps * Samples`
3. **DC Removal**: Calculates and subtracts mean from each sample set to remove static clutter
4. **RadarCube Preparation**: Prepares data structure for radar library
5. **Detection**: Calls `radarDetection` to generate point clouds
6. **Clustering**: Calls `radarClustering` to group points into objects
7. **Tracking**: Calls `radarTracking` for multi-frame object tracking
7. **Metadata Attachment**: Attaches results as GStreamer metadata (`GstRadarProcessMeta`)
8. **Frame Rate Control**: Throttles processing if `frame-rate` is set

## Downstream Consumption

The metadata can be consumed by downstream GStreamer elements:

- **gvametaconvert**: Converts `GstRadarProcessMeta` to JSON format
- **gvametapublish**: Publishes JSON metadata to files, Kafka, MQTT, etc.
- **fakesink**: Simple sink for testing and benchmarking
- **3ddatarender** (in development): Real-time visualization element
- Custom elements can retrieve metadata using `gst_buffer_get_meta()` with `GST_RADAR_PROCESS_META_API_TYPE`

## Element Details (gst-inspect-1.0)

```
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      application/octet-stream
  
  SRC template: 'src'
    Availability: Always
    Capabilities:
      application/x-radar-processed

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  frame-rate          : Frame rate for output (0 = no limit)
                        flags: readable, writable
                        Double. Range:               0 -   1.797693e+308 Default:               0 
  
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "radarprocess0"
  
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  
  qos                 : Handle Quality-of-Service events
                        flags: readable, writable
                        Boolean. Default: false
  
  radar-config        : Path to radar configuration JSON file
                        flags: readable, writable
                        String. Default: null
```
