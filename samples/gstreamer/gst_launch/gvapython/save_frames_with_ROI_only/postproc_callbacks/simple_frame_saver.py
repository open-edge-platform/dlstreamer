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

    def _convert_to_bgr(self, data, format_name):
        """Convert frame data to BGR format."""
        if format_name in ["NV12", "I420"]:
            yuv_frame = np.squeeze(data)
            color_code = (
                cv2.COLOR_YUV2BGR_NV12 if format_name == "NV12"
                else cv2.COLOR_YUV2BGR_I420
            )
            return cv2.cvtColor(yuv_frame, color_code)

        img = np.array(data, copy=True)
        if format_name in ["BGR", "BGRA", "BGRX"] and img.shape[2] == 4:
            return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        return img

    def _draw_detections(self, img, regions):
        """Draw bounding boxes and labels on image."""
        for roi in regions:
            rect = roi.rect()
            pt1 = (int(rect.x), int(rect.y))
            pt2 = (int(rect.x + rect.w), int(rect.y + rect.h))
            cv2.rectangle(img, pt1, pt2, (0, 255, 0), 2)
            cv2.putText(
                img,
                f"{roi.label()} {roi.confidence():.2f}",
                (pt1[0], pt1[1] - 5),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (0, 255, 0),
                2,
            )

    def process_frame(self, frame: VideoFrame) -> bool:
        """Process each frame and save if conditions are met."""
        current_time = time.time()
        if (current_time - self.last_save_time) < SAVE_INTERVAL:
            return True

        # Check if any detections meet confidence threshold
        if not any(roi.confidence() > MIN_CONFIDENCE for roi in frame.regions()):
            return True

        try:
            video_info = frame.video_info()
            with frame.data() as data:
                img = self._convert_to_bgr(data, video_info.finfo.name)
                self._draw_detections(img, frame.regions())

                filename = f"{OUTPUT_DIR}/frame_{self.save_count:05d}.jpg"
                cv2.imwrite(filename, img)
                self.save_count += 1
                self.last_save_time = current_time
                print(f"Saved: {filename} (format: {video_info.finfo.name})")

        except (cv2.error, OSError, ValueError, IndexError) as e:
            print(f"Error saving frame: {e}")

        return True

#     The layout of the NV12(VA-API decoder) video buffer with padding,
#     align height to 32(on Core Ultra3 Series), take 1280x720 as example:
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
