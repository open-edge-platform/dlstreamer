################################################################################
#  Copyright (C) 2026 Intel Corporation
#
#  SPDX-License-Identifier: MIT
################################################################################

import argparse
import dls_onvif_data
import dls_onvif_discovery_utils as dls_discovery
import json
from typing import Optional, Dict, List
import subprocess
import threading
from onvif import ONVIFCamera

def run_single_streamer(command: str) -> subprocess.Popen:
    """Runs a single DL Streamer in non-blocking mode"""

    def read_output(pipe, prefix, camera_id):
        """Read output from pipe in a separate thread"""
        try:
            for line in iter(pipe.readline, ''):
                if line:
                    print(f"[{camera_id}] {prefix}: {line.strip()}")
        except Exception as e:
            print(f"[{camera_id}] Error reading {prefix}: {e}")
        finally:
            pipe.close()

    try:
        print(f"Starting DL Streamer with command: {command}")
        process = subprocess.Popen(
            command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        camera_id = f"PID:{process.pid}"
        print(f"DL Streamer started ({camera_id})")

        # Start threads to read stdout and stderr
        threading.Thread(
            target=read_output, 
            args=(process.stdout, "OUT", camera_id), 
            daemon=True
        ).start()

        threading.Thread(
            target=read_output, 
            args=(process.stderr, "ERR", camera_id), 
            daemon=True
        ).start()

        return process

    except Exception as e:
        print(f"Error starting DL Streamer: {e}")
        return None


def prepare_commandline(rtsp_url: str, pipeline_elements: str) -> str:
    """Prepare GStreamer command line from RTSP URL and pipeline elements.
    Args:
        rtsp_url: The RTSP stream URL
        pipeline_elements: GStreamer pipeline elements as a string
    Returns:
        Complete GStreamer command line string
    """

    if not rtsp_url or not pipeline_elements:
        raise ValueError("URL and pipeline elements cannot be empty!")
    return f"gst-launch-1.0 rtspsrc location={rtsp_url} {pipeline_elements}"


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='ONVIF Camera Discovery')
    parser.add_argument('--verbose', type=bool, default=False,
                        help='If False then no verbose output, if True then verbose output')
    args = parser.parse_args()

    # Set verbosity level
    dls_onvif_data.VERBOSE = args.verbose

    # List to store all running processes
    running_processes = []

    # Start discovery
    cameras = dls_discovery.discover_onvif_cameras()

    for camera in cameras:
        camera_obj = ONVIFCamera(camera['hostname'], camera['port'], 'admin', 'admin')
        dls_discovery.camera_capabilities(camera_obj)

    for camera in cameras:
        # Get ONVIF camera capabilities:
        camera_obj = ONVIFCamera(camera['hostname'], camera['port'], 'admin', 'admin')

        # Get DL Streamer command line from config.json
        command = dls_discovery.get_commandline_by_key("config.json", camera['hostname'])
        if not command:
            print(f"No command line found for {camera['hostname']}, skipping...")
            continue

        # Get camera profiles and start DL Streamer for each profile
        profiles = dls_discovery.camera_profiles(camera_obj, True)
        for i, profile in enumerate(profiles, 1):
            rtsp_url = profile.rtsp_url
            if not rtsp_url:
                print(f"No RTSP URL found for profile {profile.name}, skipping...")
                continue

            commandline_executed = prepare_commandline(rtsp_url, command)
            print(f"Executing command line for {camera['hostname']}: {commandline_executed}")
            process = run_single_streamer(commandline_executed)
            if process:
                running_processes.append(process)

    # Keep script running and wait for all processes
    if running_processes:
        print(f"\n{len(running_processes)} DL Streamer process(es) started.")
        print("Press Ctrl+C to stop all processes and exit.\n")
        try:
            # Wait for all processes to complete
            for process in running_processes:
                process.wait()
        except KeyboardInterrupt:
            print("\n\nStopping all processes...")
            for process in running_processes:
                if process.poll() is None:  # If process is still running
                    process.terminate()
                    print(f"Terminated process PID: {process.pid}")
            print("All processes stopped.")
    else:
        print("No processes started.")

