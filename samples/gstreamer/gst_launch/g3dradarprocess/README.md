# Radar Signal Processing with g3dradarprocess Element

This directory contains a script demonstrating how to use the `g3dradarprocess` element for processing millimeter-wave (mmWave) radar signal data.

The `g3dradarprocess` element processes raw radar signals to generate point clouds, clusters, and tracking data. It acts as a bridge between raw radar data ingestion (typically from file sources) and advanced radar signal processing algorithms provided by `libradar`.

## How It Works

The script constructs a GStreamer pipeline that processes radar binary data from files and applies radar signal processing algorithms for detection, clustering, and tracking.

The sample utilizes GStreamer command-line tool `gst-launch-1.0` which can build and run a GStreamer pipeline described in a string format.
The string contains a list of GStreamer elements separated by an exclamation mark `!`, each element may have properties specified in the format `property=value`.

This sample builds GStreamer pipeline of the following elements:
- `multifilesrc` for reading sequential radar binary files
- `g3dradarprocess` for radar signal processing (DC removal, reordering, detection, clustering, tracking)
- `gvametaconvert` (optional) for converting metadata to JSON format
- `gvametapublish` (optional) for publishing JSON results to files, Kafka, MQTT, etc.
- `fakesink` for discarding output (processing results are available as metadata)

The `g3dradarprocess` element performs the following operations:
1. **Data Layout Transformation**: Converts raw data from `Chirps * TRN * Samples` to `TRN * Chirps * Samples` layout
2. **Signal Conditioning**: Performs DC removal to eliminate static clutter
3. **Radar Detection**: Generates point clouds with detected reflection points (range, speed, angle, SNR)
4. **Clustering**: Groups nearby points into objects
5. **Tracking**: Tracks objects over time across multiple frames

## Prerequisites

### 1. Verify DL Streamer Installation

Ensure DL Streamer is properly compiled and the `g3dradarprocess` element is available:

```bash
gst-inspect-1.0 g3dradarprocess
```

If the element is found, you should see detailed information about the element, its properties, and pad templates.

### 2. Download Radar Data and Configuration

Download the sample radar dataset which includes:
- Raw radar binary files (`.bin` format)
- Radar configuration JSON file

```bash
# From DL Streamer root directory
wget --no-proxy https://af01p-igk.devtools.intel.com/artifactory/platform_hero-igk-local/RadarData/Radar_raddet_data.zip
unzip Radar_raddet_data.zip
rm Radar_raddet_data.zip
```

This will create a `raddet` directory containing:
- `radar/` - Raw radar signal data files (000559.bin, 000560.bin, etc.)
- `RadarConfig_raddet.json` - Radar configuration file with signal parameters

### 3. Install Radar Processing Dependencies

The `g3dradarprocess` element requires Intel oneAPI Base Toolkit and the `libradar` library:

```bash
# From DL Streamer root directory
./scripts/install_radar_dependencies.sh
```

This script will:
1. Install Intel oneAPI Base Toolkit
2. Install `libradar` from Intel SED repository
3. Source the oneAPI environment variables

**NOTE**: After installation, you may need to manually source oneAPI environment in new terminal sessions:
> ```bash
> source /opt/intel/oneapi/setvars.sh
> ```

## Running the Sample

**Usage:**
```bash
./radar_process_sample.sh [OPTIONS]
```

**Options:**
- `-d, --data-path PATH`: Path to radar data directory (containing radar/ subfolder)
- `-c, --config PATH`: Path to radar configuration JSON file
- `-f, --frame-rate RATE`: Target frame rate for output (0 = no limit)
- `-s, --start-index INDEX`: Starting frame index
- `-o, --output PATH`: Path for JSON output file (enables metadata publishing)
- `-h, --help`: Show help message

**Examples:**

1. **Basic usage with default settings**
   ```bash
   ./radar_process_sample.sh
   ```

2. **Custom data path and configuration**
   ```bash
   ./radar_process_sample.sh --data-path /path/to/raddet --config /path/to/RadarConfig_raddet.json
   ```

3. **Process at 30 FPS with result publishing**
   ```bash
   ./radar_process_sample.sh --frame-rate 30 --output radar_results.json
   ```

4. **Custom starting frame and output path**
   ```bash
   ./radar_process_sample.sh --start-index 600 --output my_results.json
   ```

5. **Enable debug logging**
   ```bash
   GST_DEBUG=g3dradarprocess:4 ./radar_process_sample.sh
   ```

6. **Verbose debug output**
   ```bash
   GST_DEBUG=g3dradarprocess:5 ./radar_process_sample.sh --frame-rate 10
   ```

**Output:**
- When `--output` is specified, results are saved to the JSON file using `gvametaconvert` + `gvametapublish`
- JSON contains frame-by-frame processing results:
  - **frame_id**: Sequential frame identifier
  - **timestamp**: Processing timestamp
  - **point_clouds**: Detected radar points (ranges, speeds, angles, SNRs)
  - **clusters**: Grouped point clouds representing objects (centers, sizes, velocities)
  - **tracked_objects**: Tracked object IDs and trajectories
- Console output shows FPS and processing statistics

## Metadata Publishing Architecture

The sample uses DL Streamer's standard metadata publishing mechanism:

```
g3dradarprocess → gvametaconvert → gvametapublish → fakesink
      ↓                 ↓                  ↓
  Adds metadata    Converts to JSON    Publishes to file/Kafka/MQTT
```


## Understanding the Output

### Point Clouds
Raw detections from the radar sensor:
- **ranges[]**: Distance to each reflection point (meters)
- **speeds[]**: Doppler velocity (m/s)
- **angles[]**: Azimuth angle (radians)
- **snrs[]**: Signal-to-noise ratio (dB)

### Clusters
Grouped detections representing objects:
- **cluster_idx[]**: Mapping from points to clusters
- **cx[], cy[]**: Cluster center coordinates (meters)
- **rx[], ry[]**: Cluster extents/size (meters)
- **av[]**: Average velocity (m/s)

### Tracking Results
Multi-frame object tracking:
- **tracker_ids[]**: Unique object identifiers
- **x[], y[]**: Object position (meters)
- **vx[], vy[]**: Velocity vectors (m/s)

## Troubleshooting

### Common Issues

**Element not found:**
```
No such element or plugin 'g3dradarprocess'
```
- **Solution**: Ensure DL Streamer is properly compiled with 3D elements support and the library is in the GStreamer plugin path

**Missing dependencies:**
```
Error loading radar processing library
```
- **Solution**: Run `./scripts/install_radar_dependencies.sh` and source the radar environment:
  ```bash
  source setup_radar_env.sh
  ```

**Invalid buffer size:**
```
Buffer size mismatch
```
- **Solution**: Verify the radar configuration file matches your data format (RX/TX count, samples, chirps)

**File not found:**
```
Could not read from resource
```
- **Solution**: Verify the data path is correct and contains the radar binary files with the proper naming format

### Debug Logging

Enable detailed logs to diagnose issues:

```bash
# Level 4: Info messages
GST_DEBUG=g3dradarprocess:4 ./radar_process_sample.sh

# Level 5: Debug messages (very verbose)
GST_DEBUG=g3dradarprocess:5 ./radar_process_sample.sh

# All GStreamer debug messages
GST_DEBUG=*:3,g3dradarprocess:5 ./radar_process_sample.sh
```

## Performance Considerations

- **Frame Rate Control**: Use `--frame-rate` to throttle processing and match real-time requirements
- **oneAPI**: The element leverages Intel oneAPI for optimized signal processing

## See also
* [Samples overview](../../README.md)
* [g3dradarprocess element documentation](../../../../docs/source/elements/g3dradarprocess.md)
