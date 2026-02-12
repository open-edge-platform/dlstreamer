#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer Open/Close Valve Sample.

This module demonstrates dual GStreamer pipeline control with a valve element
to dynamically route video streams based on object detection results.
"""

import sys
import time
#from contextlib import contextmanager
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
# pylint: disable=no-name-in-module
from gi.repository import Gst, GLib, GstAnalytics
# pylint: enable=no-name-in-module


class DualStreamController:
    """Class to create and control dual GStreamer streams with a valve element."""
    def __init__(self, video_source):
        """
        Initialize dual stream controller
        """

        Gst.init(None)
        self.video_source = video_source
        self.pipeline = None
        self.valve = None
        self.pre_view_classify = None
        self.loop = GLib.MainLoop()
        self.valve_opened = False


    def create_pipeline(self):
        """Create GStreamer pipeline with dual streams"""

        # Define source element string
        source_str = f"filesrc location={self.video_source} ! decodebin3"

        # Build pipeline string:
        pipeline_str = f"""
        {source_str} !
        videoconvert ! videoscale ! videorate !
        video/x-raw,width=640,height=480,framerate=30/1,format=I420 !

        tee name=main_tee allow-not-linked=false !

        queue name=preview_queue
            max-size-buffers=30
            max-size-bytes=0
            max-size-time=300000000
            leaky=no
            flush-on-eos=true !
        identity name=sync_point1 sync=true drop-probability=0.0 !
        textoverlay name=ai_overlay
            text="Detection Video Stream"
            valignment=bottom halignment=center
            font-desc="Sans Bold 14" color=0xFF000000 !
        gvadetect model=/home/labrat/models/public/yolo11s/FP32/yolo11s.xml
            pre-process-backend=opencv
            device=CPU
            threshold=0.6
            inference-interval=10
            inference-region=0
            model-instance-id=inst0
            batch-size=1
            name=detection !
        gvatrack name=object_tracker !
        queue name=detect_to_classify
            max-size-buffers=20
            leaky=no !
        gvaclassify model=./models/vehicle-attributes-recognition-barrier-0039.xml
            model-proc=../../model_proc/intel/vehicle-attributes-recognition-barrier-0039.json
            device=CPU 
            inference-interval=1 
            name=pre_view_classify !
        gvawatermark name=preview_watermark !
        videoconvert name=preview_convert !
        autovideosink name=preview_sink
            sync=true

        main_tee. !
        queue name=stream1_queue
            max-size-buffers=3
            max-size-bytes=0
            max-size-time=300000000
            leaky=no
            flush-on-eos=true !
        identity name=sync_point2 sync=true drop-probability=0.0 !
        valve name=control_valve drop=false !
        queue name=valve_buffer
            max-size-buffers=1
            leaky=no !
        textoverlay name=valve_overlay
            text="Valve Stream"
            valignment=bottom halignment=center
            font-desc="Sans Bold 14" color=0xFF10FF00 !
        gvadetect model=/home/labrat/models/public/yolo11s/FP16/yolo11s.xml
            pre-process-backend=opencv
            device=CPU
            threshold=0.6
            inference-interval=1
            inference-region=0
            model-instance-id=inst0
            batch-size=1
            name=valve_detection !
        queue name=valve_detect_to_classify
            max-size-buffers=20
            leaky=no !            
        gvawatermark  name=valve_watermark !
        videoconvert name=stream1_convert !
        autovideosink name=stream1_sink
            sync=true
        """

        try:
            self.pipeline = Gst.parse_launch(pipeline_str)
            if not self.pipeline:
                print("Error: Could not create pipeline")
                return False
            print("Pipeline created")

            # Get valve element from pipeline
            self.valve = self.pipeline.get_by_name("control_valve")
            if not self.valve:
                print("Error: Could not find control_valve element")
                return False
            print("Found control_valve element")

            # Get pre_view_classify element from pipeline
            # Below we add a probe to the sink pad of pre_view_classify to monitor detected objects
            self.pre_view_classify = self.pipeline.get_by_name("pre_view_classify")
            if not self.pre_view_classify:
                print("Error: Could not find pre_view_classify element")
                return False
            print("Found pre_view_classify element")

            # Get sink pad of pre_view_classify
            pre_view_classify_pad = self.pre_view_classify.get_static_pad("sink")
            if not pre_view_classify_pad:
                print("Unable to get sink pad of gvaclassify")
                return False
            print("Got sink pad of pre_view_classify")

            # and add probe/callback
            pre_view_classify_pad.add_probe(Gst.PadProbeType.BUFFER,
                                            self.object_detector_callback, 0)
            print("All elements found and pipeline created successfully")
        except GLib.Error as e:
            print(f"Error creating pipeline: {e}")
            return False
        return True

    def object_detector_callback(self, pad, info, u_data):
        """
        Callback function for object detection probe on GStreamer pad.

        Processes analytics metadata from the buffer to detect objects and control valve state.
        When a 'truck' object is detected, the valve is opened; otherwise, it is closed.

        Args:
            pad: GStreamer pad object that triggered the probe.
            info: Probe info containing buffer and other data.
            u_data: User data passed to the callback.

        Returns:
            Gst.PadProbeReturn.OK: Indicates the probe should continue normal processing.
        """

        buffer = info.get_buffer()
        if not buffer:
            print("object_detector_callback: no buffer. Continueing...")
            return Gst.PadProbeReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
        if not rmeta:
            return Gst.PadProbeReturn.OK
        else:
            for mtd in rmeta:
                if type(mtd) == GstAnalytics.ODMtd:
                    object_type = GLib.quark_to_string(mtd.get_obj_type())
                    if object_type=="truck":
                        self.open_valve()
                    else:
                        self.close_valve()
        return Gst.PadProbeReturn.OK


    def start(self):
        """
        Start the GStreamer pipeline and initialize monitoring threads.

        Returns:
            bool: True if the pipeline started successfully, False otherwise.        
        """

        # Start pipeline
        if not self.pipeline:
            print("Controller: No pipeline to start")
            return False

        # Set pipeline to PLAYING state
        if self.pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
            print("Failed to start pipeline")
            return False

        # Wait a moment for pipeline to initialize
        time.sleep(1)

        print("Pipeline started successfully")
        return True


    def open_valve(self):
        """Open valve - enable controlled stream (drop=false)"""
        if self.valve:
            self.valve.set_property("drop", False)
            self.valve_opened = True
        else:
            print("Valve not available")

    def close_valve(self):
        """Close valve - disable controlled stream (drop=true)"""
        if self.valve:
            self.valve.set_property("drop", True)
            self.valve_opened = False
        else:
            print("Controller: Valve not available")


def display_header():
    """Display the header information for the Open/Close Valve sample."""
    print("\n# ====================================== #")
    print("#  Copyright (C) 2025 Intel Corporation  #")
    print("#                                        #")
    print("#     SPDX-License-Identifier: MIT       #")
    print("# ====================================== #")
    print("#  DL Streamer Open/Close Valve Sample   #")
    print("# ====================================== #\n")

def main():
    """Main function to run the Open/Close Valve sample."""
    # Display header
    display_header()

    # Create dual stream controller
    video_source = "./videos/cars_extended.mp4"
    controller = DualStreamController(video_source)

    # Create pipeline
    if not controller.create_pipeline():
        print("Failed to create pipeline. Exiting...")
        return 1

    # Get pipeline bus
    bus = controller.pipeline.get_bus()

    # Start pipeline
    if not controller.start():
        print("Failed to start pipeline. Exiting...")
        return 1

    # Run until interrupted
    print("Running pipeline. Press Ctrl+C to stop...")
    terminate = False
    try:
        while not terminate:
            msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE,
                                         Gst.MessageType.EOS | Gst.MessageType.ERROR)
            if msg:
                if msg.type == Gst.MessageType.ERROR:
                    err, debug_info = msg.parse_error()
                    print(f"Error received from element {msg.src.get_name()}")
                    print(f"Debug info: {debug_info}")
                    terminate = True
                if msg.type == Gst.MessageType.EOS:
                    print("Pipeline complete.")
                    terminate = True
    except KeyboardInterrupt as e:
        print(f"Interrupted by user. Stopping pipeline...[{e}]")
        terminate = True
    except GLib.Error as e:
        print(f"Exception occurred: {e}")

    # Stop pipeline
    controller.pipeline.set_state(Gst.State.NULL)
    return 0

if __name__ == "__main__":
    sys.exit(main())
