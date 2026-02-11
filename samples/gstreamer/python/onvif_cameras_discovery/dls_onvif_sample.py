'''
This module discovers ONVIF cameras on the network and launches GStreamer DL Streamer
pipelines for each discovered camera profile. It manages multiple streaming processes
concurrently and handles their output in separate threads.
'''
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import argparse
import shlex
import subprocess
import threading
from typing import List
from onvif import ONVIFCamera   # pylint: disable=import-error
import dls_onvif_discovery_utils as dls_discovery

def run_single_streamer(gst_command: List[str]) -> subprocess.Popen:
    """Runs a single DL Streamer in non-blocking mode
    
    Args:
        gst_command: List of command arguments (avoids shell injection)
    
    Returns:
        subprocess.Popen object or None on failure
    """

    def read_output(pipe, prefix, camera_id):
        """Read output from pipe in a separate thread"""
        try:
            for line in iter(pipe.readline, ''):
                if line:
                    print(f"[{camera_id}] {prefix}: {line.strip()}")
        except Exception as e: # pylint: disable=broad-exception-caught
            print(f"[{camera_id}] Error reading {prefix}: {e}")
        finally:
            pipe.close()

    try:
        process = subprocess.Popen( # pylint: disable=consider-using-with
            gst_command,
            shell=False,
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

    except Exception as e: # pylint: disable=broad-exception-caught
        print(f"Error starting DL Streamer: {e}")
        return None


def prepare_commandline(camera_rtsp_url: str, pipeline_elements: str) -> List[str]:
    """Prepare GStreamer command line from RTSP URL and pipeline elements.

    Args:
        camera_rtsp_url: The RTSP stream URL
        pipeline_elements: GStreamer pipeline elements as a string
    
    Returns:
        List of command arguments (safer than shell string)
    """

    if not camera_rtsp_url or not pipeline_elements:
        raise ValueError("URL and pipeline elements cannot be empty!")

    # Build command as list to avoid shell injection
    prepared_command = ["gst-launch-1.0", "rtspsrc", f"location={camera_rtsp_url}"]

    # Safely parse pipeline elements
    prepared_command.extend(shlex.split(pipeline_elements))

    return prepared_command


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        description='ONVIF Camera Discovery and DL Streamer Pipeline Launcher')
    parser.add_argument('--verbose', type=bool, default=False,
                        help='If False then no verbose output, if True then verbose output')
    parser.add_argument('--user', type=str, help='ONVIF camera username')
    parser.add_argument('--password', type=str, help='ONVIF camera password')
    args = parser.parse_args()


    # List to store all running processes
    running_processes = []

    # Start discovery (send broadcast):
    cameras = dls_discovery.discover_onvif_cameras()

    for camera in cameras:
        # Get ONVIF camera capabilities:
        camera_obj = ONVIFCamera(camera['hostname'], camera['port'], args.user, args.password)

        # Get DL Streamer command line from config.json
        command_json = dls_discovery.get_commandline_by_key("config.json", camera['hostname'])
        if not command_json:
            print(f"No command line found for {camera['hostname']}, skipping...")
            continue

        # Get camera profiles and start DL Streamer for each profile
        profiles = dls_discovery.camera_profiles(camera_obj, args.verbose)
        for i, profile in enumerate(profiles, 1):
            rtsp_url = profile.rtsp_url
            if not rtsp_url:
                print(f"No RTSP URL found for profile {profile.name}, skipping...")
                continue

            commandline_executed = prepare_commandline(rtsp_url, command_json)
            print(f"Executing command line for {camera['hostname']}: {commandline_executed}")
            running_process = run_single_streamer(commandline_executed)
            if running_process:
                running_processes.append(running_process)

    # Keep script running and wait for all processes
    if running_processes:
        print("Press Ctrl+C to stop all processes and exit.\n")
        try:
            # Wait for all processes to complete
            for active_process in running_processes:
                active_process.wait()
        except KeyboardInterrupt:
            print("\n\nStopping all processes...")
            for process_problem in running_processes:
                if process_problem.poll() is None:  # If process is still running
                    process_problem.terminate()
                    print(f"Terminated process PID: {process_problem.pid}")
            print("All processes stopped.")
    else:
        print("No processes started.")
