#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail


CAMERA="/dev/video0"                        # Camera device (default: /dev/video0)
EOS_FRAME_DEFAULT=5                         # TheDefault value of frame number 
                                            # treated as EOS (default: 5)
EOS_FRAME=$EOS_FRAME_DEFAULT
FILE_CORE_NAME_DEFAULT="RS-frame"           # Base name for output files (used with multifilesink)
FILE_CORE_NAME=$FILE_CORE_NAME_DEFAULT
PIPELINE_TAIL=""                            # Arguments for sink element (identity and multifilesink)

display_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --camera <value>        Specify camera device (default: /dev/video0)"
    echo "  --eos-frame <value>     The frame number treated as EOS (default: 5)"
    echo "  --core-name <value>     Base name for output PCD files (default: RS-frame)."
    echo "                          Example: RS-frame_000001.pcd, RS-frame_000002.pcd"
    echo "  --help                  Show this help message"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --camera)
            CAMERA="$2"
            shift 2
            ;;

        --eos-frame)
            EOS_FRAME="$2"
            # Check if EOS_FRAME is greater than 0
            if [ "$EOS_FRAME" -le 0 ]; then
                echo "ERROR: The frame number treated as EOS must be greater than 0." >&2
                EOS_FRAME=$EOS_FRAME_DEFAULT
                echo "Resetting EOS frame number to default value: $EOS_FRAME."
            fi

            shift 2
            ;;

        --core-name)
            FILE_CORE_NAME="$2"

            # Validate FILE_CORE_NAME for valid filename characters
            if [[ "$2" =~ ^[a-zA-Z0-9._-]+$ ]]; then
                FILE_CORE_NAME="$2"
            else
                echo "ERROR: Invalid file core name." >&2
                echo "Resetting core name to default value: $FILE_CORE_NAME"
                FILE_CORE_NAME=$FILE_CORE_NAME_DEFAULT
            fi

            shift 2
            ;;

        --help)
            display_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            display_help
            exit 1
            ;;
    esac
done

# Check if camera device exists
if [ ! -e "$CAMERA" ]; then
    echo "ERROR: Camera device $CAMERA not found!" >&2
    echo "------------------------" >&2
    echo "| Available video devices:" >&2
    echo "------------------------" >&2
    echo "|" $(ls /dev/video* 2>/dev/null || echo "No video devices found.")
    echo "|------------------------" >&2
    echo "Please specify a valid camera device using the --camera option."
    exit 1
fi

# Verify if gvarealsense element is available in the system:
if ! gst-inspect-1.0 gvarealsense > /dev/null 2>&1; then
    echo "ERROR: gvarealsense element not found!" >&2
    echo "You can verify by running: gst-inspect-1.0 | grep gvarealsense 2>/dev/null && echo 'Element gvarealsense found'" >&2
    exit 1
fi

# Display defined parameters for debugging purposes
echo "-------------------------------------------------" >&2
echo "Defined parameters:"
echo "                Camera device: $CAMERA"
echo "  Frame number treated as EOS: $EOS_FRAME"
echo "        Output file core name: $FILE_CORE_NAME"
echo "-------------------------------------------------" >&2

# Build GStreamer pipeline command
PIPELINE_TAIL="identity eos-after=$EOS_FRAME ! gvafpscounter ! multifilesink location=\"${FILE_CORE_NAME}_%06d.pcd\""
COMMAND_LINE="gst-launch-1.0 gvarealsense camera=\"$CAMERA\" ! queue !"

COMMAND_LINE="$COMMAND_LINE $PIPELINE_TAIL"

# Display the final command line for debugging purposes
echo "Executing command line: $COMMAND_LINE"

# Execute the GStreamer pipeline command
eval "$COMMAND_LINE"



