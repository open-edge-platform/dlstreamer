# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
Simple example: Save frames when specific objects are detected.
Minimal implementation for quick deployment.
"""
import os
import time
import cv2
import numpy as np
from gstgva import VideoFrame
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstVideo", "1.0")

# Configuration
OUTPUT_DIR = "saved_frames"
SAVE_INTERVAL = 2.0  # Minimum seconds between saves
MIN_CONFIDENCE = 0.5

# Initialize
os.makedirs(OUTPUT_DIR, exist_ok=True)

class FrameSaver:
    """
    Frame saver class for saving video frames with detected objects.
    
    This class processes video frames from a GStreamer pipeline and saves them
    to disk when objects are detected above a confidence threshold. It implements
    rate limiting to prevent excessive disk I/O and handles multiple video formats.
    
    Attributes:
        last_save_time (float): Timestamp of the last saved frame
        save_count (int): Counter for sequential frame numbering
        
    Configuration (module-level constants):
        OUTPUT_DIR (str): Directory path for saved frames
        SAVE_INTERVAL (float): Minimum seconds between saves
        MIN_CONFIDENCE (float): Minimum detection confidence threshold
    """

    def __init__(self):
        """Initialize the FrameSaver with default state."""
        self.last_save_time = 0
        self.save_count = 0

    def process_frame(self, frame: VideoFrame) -> bool:
        """Process each frame and save if conditions are met."""
        # Check time interval
        current_time = time.time()
        if (current_time - self.last_save_time) < SAVE_INTERVAL:
            return True

        # Check detections
        should_save = False
        for roi in frame.regions():
            if roi.confidence() > MIN_CONFIDENCE:
                should_save = True
                break

        # Save frame
        if should_save:
            try:
                # Get frame format info
                video_info = frame.video_info()
                format_name = video_info.finfo.name

                with frame.data() as data:

                    # Handle different color formats
                    if format_name in ["NV12", "I420"]:
                        # (will need unmerged patch to handle padding correctly)
                        yuv_frame = np.squeeze(data)
                        color_code = (
                            cv2.COLOR_YUV2BGR_NV12
                            if format_name == "NV12"
                            else cv2.COLOR_YUV2BGR_I420
                        )
                        img = cv2.cvtColor(yuv_frame, color_code)

                    elif format_name in ["BGR", "BGRA", "BGRX"]:
                        img = np.array(data, copy=True)
                        if img.shape[2] == 4:
                            # BGRA or BGRX to BGR
                            img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
                        # else: BGR format (no conversion needed)

                    else:
                        # Fallback: assume BGR or try to handle it
                        img = np.array(data, copy=True)

                    # Draw detections
                    for roi in frame.regions():
                        rect = roi.rect()
                        x1, y1 = int(rect.x), int(rect.y)
                        x2, y2 = x1 + int(rect.w), y1 + int(rect.h)
                        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
                        label = f"{roi.label()} {roi.confidence():.2f}"
                        cv2.putText(
                            img,
                            label,
                            (x1, y1 - 5),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.5,
                            (0, 255, 0),
                            2,
                        )

                    # Save
                    filename = f"{OUTPUT_DIR}/frame_{self.save_count:05d}.jpg"
                    cv2.imwrite(filename, img)
                    self.save_count += 1
                    self.last_save_time = current_time
                    print(f"Saved: {filename} (format: {format_name}, shape: {data.shape})")

            except (cv2.error, OSError, ValueError, IndexError) as e:
                print(f"Error saving frame: {e}")

        return True

#     The layout of the NV12(VA-API decoder) video buffer with padding,
#     align height to 32, take 1280x720 as example:
#     ----------------------------------------------
#     | Y_data 1280x720              |padding=0    |
#     |                              |             |
#     |                              |             |
#     |                              |             |
#     ----------------------------------------------
#     | padding 1280x16                            |
#     ----------------------------------------------
#     | UV_data 1280x360             |padding=0    |
#     |                              |             |
#     ----------------------------------------------
#     | padding 1280x24                            |
#     ----------------------------------------------
