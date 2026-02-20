# RealSense™ Depth Camera sample  (gst-launch command line)


## Overview

This sample demonstrates how to capture video stream from a 3D RealSense™ Depth Camera using DL Streamer's **gvarealsense** element.

## Prerequisites

### Real Sense SDK 2.0
- **Install the RealSense drivers and libraries** to enable communication with Intel RealSense cameras:

    ```bash
    sudo apt install librealsense2-dkms
    sudo apt install librealsense2
    ```
- **Verify** if Real Sense camera works. 

    For example, camera /dev/video0 details can be examined using the following methods:

    ```bash
    v4l2-ctl --device=/dev/video0 --all
    # or
    media-ctl -d /dev/media0 -p
    ```
    and video can be played:
    ```bash
    ffplay /dev/video0
    ```

- **Verify** if gvarealsense is installed properly in the system
    ```bash
    gst-inspect-1.0 | grep gvarealsense 2>/dev/null && echo "Element gvarealsense found"
    ```

## Usage

The `sample_realsense.sh` script provides a convenient wrapper for capturing depth data from RealSense cameras and saving it as PCD (Point Cloud Data) files separately for each video frame:

```bash
# Basic usage - capture 5 frames (default) to RS-frame_XXXXXX.pcd files
./sample_realsense.sh --camera /dev/video0

# Capture specific number of frames
./sample_realsense.sh --camera /dev/video0 --eos-frame 10

# Use custom output file name pattern
./sample_realsense.sh --camera /dev/video0 --core-name MyCapture

# Combine options
./sample_realsense.sh --camera /dev/video0 --eos-frame 20 --core-name test-run

# Display help
./sample_realsense.sh --help
```

**Script options:**
- `--camera <device>` - Camera device path (default: /dev/video0)
- `--eos-frame <number>` - The frame number treated as EOS (default: 5). Must be greater than 0.
- `--core-name <name>` - Base name for output PCD files (default: RS-frame). Final file name format: `<core-name>_XXXXXX.pcd`, where XXXXXX is a 6-digit frame number.
- `--help` - Display usage information

**Output:** Files are saved as `<core-name>_XXXXXX.pcd` where XXXXXX is a 6-digit frame number.

**Note:** Depending on pipeline behavior, the EOS (End of Stream) signal can be generated before the last frame is saved to the file.

**Features:**
- Automatic validation of camera device availability
- Verification of gvarealsense element installation
- Automatic frame counting with FPS display (gvafpscounter)
- Saves depth data as standard PCD (Point Cloud Data) files

### Direct gst-launch usage

Alternatively, you can use `gst-launch-1.0` directly:

```bash
gst-launch-1.0 gvarealsense camera="/dev/video0" ! queue ! identity eos-after=10 ! gvafpscounter ! multifilesink location="RS-frame_%06d.pcd"
```

## Pipeline Elements

- `gvarealsense` - source element for Intel RealSense camera
- `queue` - buffering element for thread decoupling
- `identity` - pass-through element with eos-after property to limit frame count; the pipeline stops after reaching the EOS frame number. 
- `gvafpscounter` - displays frame rate statistics
- `multifilesink` - saves each frame as a separate PCD file

</br>

**Note**: The Intel RealSense™ 3D Depth Camera generates substantial data volumes during operation. Each PCD file can be several megabytes in size. Please ensure adequate disk space is available before initiating capture sessions.

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

The pipeline automatically stops after capturing the specified number of frames (using `identity eos-after` property). You can also press **Ctrl+C** (SIGINT) to stop it manually at any time.

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

### Invalid core name

**Error:** `ERROR: Invalid file core name.`

**Solution:**
- Core name must contain only alphanumeric characters, dots (.), dashes (-), and underscores (_)
- Avoid special characters, spaces, or path separators
- Example valid names: `test-run`, `capture_01`, `my.data`

### Invalid EOS frame number

**Error:** `ERROR: The frame number treated as EOS must be greater than 0.`

**Solution:**
- Specify a positive integer value for `--eos-frame`
- The script will reset to the default value (5) if an invalid value is provided

### Permission denied when writing files

**Error:** Cannot write files to current directory

**Solution:**
- Check write permissions in the current directory: `ls -ld .`
- Ensure you have sufficient disk space: `df -h .`
- Consider running from a directory where you have write permissions

## Performance Considerations

- **Data rate:** RealSense cameras can generate 10-100 MB/s depending on resolution and frame rate
- **File size:** Each file can be several megabytes. Plan accordingly when capturing many frames
- **Frame limit:** Use `--eos-frame` to control the number of frames captured
- **File naming:** Files are named with a 6-digit sequential number (e.g., RS-frame_000000.pcd, RS-frame_000001.pcd, ...)
- **FPS monitoring:** The script uses gvafpscounter to display frame rate information during capture