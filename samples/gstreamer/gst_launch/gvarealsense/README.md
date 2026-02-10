# RealSense™ Depth Camera sample  (gst-launch command line)


## Overview

This sample demonstrates how to capture video stream from a 3D RealSense™ Depth Camera using DL Streamer's **gvarealsense** element.

## Prerequisites

### Real Sense SDK 2.0
- **Install the RealSense drivers and libraries** to enable communication with Intel RealSense cameras:

    ```
    sudo apt install librealsense2-dkms
    sudo apt install librealsense2
    ```
- **Verify** if Real Sense camera works. 

    For example, the details of camera /dev/video0 can be examined using the following method:

    ```
    v4l2-ctl --device=/dev/video0 --all
    # or
    media-ctl -d /dev/media0 -p
    ```
    and video can be:
    ```
    ffplay /dev/video0
    ```

- **Verify** if gvarealsense is installed properly in the system
    ```
    gst-inspect-1.0 | grep gvarealsense 2>/dev/null && echo "Element gvarealsense found"
    ```

## Usage

### Using the sample script (recommended)

The `sample_realsense.sh` script provides a convenient wrapper for capturing video from RealSense cameras with additional features:

```bash
# Basic usage - display frame data to console
./sample_realsense.sh --camera /dev/video0

# Save to file
./sample_realsense.sh --camera /dev/video0 --file output.raw

# Save to file with size limit (stops when file reaches 100 MB)
./sample_realsense.sh --camera /dev/video0 --file output.raw --max-size 100

# Display help
./sample_realsense.sh --help
```

**Script options:**
- `--camera <device>` - Camera device path (default: /dev/video0)
- `--file <path>` - Output file path (optional, uses fakesink if not specified)
- `--max-size <MB>` - Maximum file size in megabytes (requires --file)
- `--help` - Display usage information

**Features:**
- Automatic validation of camera device availability
- Verification of gvarealsense element installation
- Automatic removal of existing output files to prevent data corruption
- File size monitoring and automatic pipeline shutdown when limit is reached

### Direct gst-launch usage

Alternatively, you can use `gst-launch-1.0` directly:

```bash
gst-launch-1.0 gvarealsense camera=/dev/video0 ! queue ! fakesink dump=true
```

## Pipeline Elements

- `gvarealsense` - source element for Intel RealSense camera
- `queue` - buffering element for thread decoupling
- `fakesink` - test sink that displays buffer metadata when dump=true

</br>

**Note**: The Intel RealSense™ 3D Depth Camera generates substantial data volumes during operation. Please ensure adequate disk space is available before initiating capture sessions.

**Note**: When using the `--max-size` option, the output file may exceed the specified size limit. This is normal behavior due to buffering and should not be a cause for concern.

## Finding Your Camera Device

To list all available video devices on your system:

```bash
ls /dev/video*
```

The script will automatically display available devices if the specified camera is not found.

To get detailed information about a specific camera device:

```bash
v4l2-ctl --device=/dev/video0 --all
```

## Stopping the Pipeline

Press **Ctrl+C** (SIGINT) to gracefully stop the pipeline at any time. When using `--max-size`, the pipeline will automatically stop when the file size limit is reached.

## Troubleshooting

### Camera device not found

**Error:** `ERROR: Camera device /dev/videoX not found!`

**Solution:**
- Check available devices: `ls /dev/video*`
- Verify RealSense camera is connected: `lsusb | grep Intel`
- Check permissions: `ls -l /dev/video*` (you may need to add your user to the `video` group: `sudo usermod -a -G video $USER`)

### gvarealsense element not found

**Error:** `ERROR: gvarealsense element not found!`

**Solution:**
- Verify gvarealsense installation: `gst-inspect-1.0 gvarealsense`
- Ensure DL Streamer is properly installed with RealSense support
- Check that `librealsense2` is installed: `dpkg -l | grep librealsense2`

### File size limit not working

**Issue:** Pipeline continues running after reaching `--max-size`

**Notes:**
- The `--max-size` option requires `--file` to be specified
- The script monitors file size every 0.5 seconds, so slight overrun is expected
- File size is checked in bytes (1 MB = 1,048,576 bytes)

### Permission denied when writing to file

**Error:** Cannot write to output file

**Solution:**
- Check write permissions in the target directory
- Ensure you have sufficient disk space: `df -h`
- If the file exists and is locked, the script will attempt to remove it automatically

## Performance Considerations

- **Data rate:** RealSense cameras can generate 10-100 MB/s depending on resolution and frame rate
- **File size limits:** Use `--max-size` for long captures to prevent disk overflow
- **Monitoring:** The script checks file size every 0.5 seconds when `--max-size` is used
- **Cleanup:** Existing output files are automatically removed to prevent data corruption