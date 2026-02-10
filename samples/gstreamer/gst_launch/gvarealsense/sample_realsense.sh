#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail


CAMERA="/dev/video0"  # Camera device (default: /dev/video0)
FILENAME=""           # Output file (empty = use fakesink)
FILE_SIZE_MB=""       # Max file size in MB (empty = run indefinitely)
DUMP_ARGS=""          # Arguments for sink element (filesink or fakesink)


display_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --camera <value>     Specify camera device (default: /dev/video0)"
    echo "  --file <value>       Specify file to save output (default: none, uses fakesink)"
    echo "  --max-size <value>   Maximum file size in MB (only with --file, default: unlimited)"
    echo "  --help               Show this help message"
}


# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --camera)
            CAMERA="$2"
            shift 2
            ;;
        --file)
            FILENAME="$2"
            shift 2
            ;;
        --max-size)
            FILE_SIZE_MB="$2"
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
    echo "Please specify a valid camera device using the --camera option." >&2
    exit 1
fi

# Verify if gvarealsense element is available in the system:
if ! gst-inspect-1.0 gvarealsense > /dev/null 2>&1; then
    echo "ERROR: gvarealsense element not found!" >&2
    echo "You can verify by running: gst-inspect-1.0 | grep gvarealsense 2>/dev/null && echo "Element gvarealsense found"" >&2
    exit 1
fi


# Remove existing output file if it exists (to avoid appending to old data)
if [ -n "$FILENAME" ] && [ -f "$FILENAME" ]; then
    echo "Removing existing file: $FILENAME"
    rm -f "$FILENAME"
fi

# Build GStreamer pipeline command
COMMAND_LINE="gst-launch-1.0 gvarealsense camera=\"$CAMERA\" ! queue ! "

# Add filesink if output file is specified, otherwise use fakesink
if [ -n "$FILENAME" ]; then
    echo "Output will be saved to file: $FILENAME"
    DUMP_ARGS="filesink location=$FILENAME"
else
    DUMP_ARGS="fakesink dump=true"
fi

# Add dump arguments to the command line
COMMAND_LINE="$COMMAND_LINE $DUMP_ARGS"

# Display the final command line for debugging purposes
echo "Executing command line: $COMMAND_LINE"

# If file size limit is specified, run the pipeline in the background and monitor file size
if [ -n "$FILE_SIZE_MB" ] && [ -n "$FILENAME" ]; then
    echo "Pipeline will stop when file reaches $FILE_SIZE_MB MB"
    
    # Start pipeline in background
    bash -c "$COMMAND_LINE" &
    PID=$!
    
    # Monitor file size
    MAX_SIZE_BYTES=$((FILE_SIZE_MB * 1024 * 1024))
    while kill -0 $PID 2>/dev/null; do
        if [ -f "$FILENAME" ]; then
            FILE_SIZE=$(stat -c%s "$FILENAME" 2>/dev/null || echo 0)
            if [ "$FILE_SIZE" -ge "$MAX_SIZE_BYTES" ]; then
                echo "File size limit reached: $FILE_SIZE_MB MB"
                # Send SIGINT to the process group to ensure gst-launch receives it
                kill -INT -$PID 2>/dev/null || kill -INT $PID
                break
            fi
        fi
        sleep 0.5
    done
    
    wait $PID 2>/dev/null || true
else
    # If no file size limit, just run the command line directly
    eval "$COMMAND_LINE"
fi


