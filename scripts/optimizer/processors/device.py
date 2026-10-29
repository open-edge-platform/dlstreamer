# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging
import json
import heapq
import itertools
import os
import shutil
import subprocess  # nosec B404 - runs the DL Streamer dls_devices tool, no shell
from functools import lru_cache

from processors.utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

DLS_DEVICES_ENV = "DLS_DEVICES_BIN"
_DLS_DEVICES_NAME = "dls_devices.exe" if os.name == "nt" else "dls_devices"


def _find_dls_devices():
    """Locate the dls_devices tool: $DLS_DEVICES_BIN, PATH, then the DL Streamer install layout."""
    explicit = os.environ.get(DLS_DEVICES_ENV)
    if explicit:
        return explicit

    on_path = shutil.which(_DLS_DEVICES_NAME)
    if on_path:
        return on_path

    # <prefix>/scripts/optimizer/processors/device.py -> <prefix>/bin
    prefix = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    candidate = os.path.join(prefix, "bin", _DLS_DEVICES_NAME)
    if os.path.isfile(candidate):
        return candidate

    raise RuntimeError(
        f"'{_DLS_DEVICES_NAME}' not found. It is installed with DL Streamer (<install-dir>/bin); "
        f"add it to PATH or point {DLS_DEVICES_ENV} to it.")


@lru_cache(maxsize=1)
def query_devices():
    """Return the OpenVINO devices as reported by dls_devices: [{name, full_name, type}, ...]."""
    executable = _find_dls_devices()
    try:
        result = subprocess.run([executable], capture_output=True, text=True,  # nosec B603
                                check=True, timeout=120)
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"{executable} failed: {exc.stderr.strip()}") from exc
    return tuple(json.loads(result.stdout)["devices"])


def available_devices():
    """Return the names of the OpenVINO devices available to DL Streamer, e.g. ['CPU', 'GPU']."""
    return [device["name"] for device in query_devices()]


class DeviceGenerator: # pylint: disable=missing-class-docstring
    def __init__(self):
        self.tracked_elements = []
        self.devices = available_devices()
        self.candidates = []

    def set_allowed_devices(self, devices): # pylint: disable=missing-function-docstring
        _devices = available_devices()
        for device in devices:
            if not any(device in d for d in _devices):
                raise RuntimeError(f"Device {device} is not supported by this system! Available devices: {str(_devices)}") # pylint: disable=line-too-long
        self.devices = devices

    def init_pipeline(self, initial_pipeline): # pylint: disable=too-many-locals, missing-function-docstring
        logger.info("Devices allowed for optimization: %s", str(self.devices))

        self.tracked_elements = []
        self.candidates = []

        instance_ids = {}
        group_count = 0

        # prepare device groups
        for idx, element in enumerate(initial_pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                (_, parameters) = parse_element_parameters(element)
                instance_id = parameters.get("model-instance-id")
                group_idx = 0

                # if element has an instance id, get the device group index
                if instance_id:
                    group_idx = instance_ids.get(instance_id)

                    # if this instance id is new, create a new group index
                    if group_idx is None:
                        group_idx = group_count
                        instance_ids[instance_id] = group_idx
                        group_count += 1

                # if there's no instance id, treat element as its own group
                else:
                    group_idx = group_count
                    group_count += 1

                self.tracked_elements.append({
                    "index": idx,
                    "group_idx": group_idx,
                })

        # prepare device information
        info = _compile_device_info()
        devices = list(map(lambda e: (e, info[e]), self.devices))

        # prepare all device combinations
        combinations = itertools.product(devices, repeat=group_count)

        # transform device combinations into pipeline candidates
        for combination in combinations:
            # preapre the pipeline as well as score info
            pipeline = initial_pipeline.copy()
            total_score = 0

            for element in reversed(self.tracked_elements):
                # Get the pipeline element we're modifying
                idx = element["index"]
                (element_type, parameters) = parse_element_parameters(pipeline[idx])

                # Get the device for this element
                device, device_score = combination[element["group_idx"]]

                # Configure an appropriate backend and memory location
                memory = ""
                if "GPU" in device:
                    parameters["pre-process-backend"] = "va-surface-sharing"
                    memory = "video/x-raw(memory:VAMemory)"

                if "NPU" in device:
                    parameters["pre-process-backend"] = "va"
                    memory = "video/x-raw(memory:VAMemory)"

                if "CPU" in device:
                    parameters["pre-process-backend"] = "opencv"
                    memory = "video/x-raw"

                # Apply current configuration
                parameters["device"] = device
                parameters = assemble_parameters(parameters)
                pipeline[idx] = f" {element_type} {parameters}"
                pipeline.insert(idx, f" {memory} ")
                pipeline.insert(idx, " vapostproc ")

                total_score -= device_score

            # store the score, device combination info and the candidate in a priority queue
            combination = list(map(lambda e: (e[0]), combination))
            heapq.heappush(self.candidates, (total_score, combination, pipeline))

    def __iter__(self):
        return self

    def __next__(self) -> list:
        try:
            score, combination, pipeline = heapq.heappop(self.candidates)

            # log device combinations
            logger.info("Testing device combination: %s", str(combination))
            logger.debug("Combination score: %s", str(-score))

            return pipeline
        except IndexError as exc:
            raise StopIteration from exc

###################################################################################################

def _compile_device_info():
    device_info = {}

    # Do a first pass where we collect info about CPUs and discrete devices
    for entry in query_devices():
        device = entry["name"]
        if "CPU" in device or entry["type"] == "discrete":
            device_info[device] = entry["full_name"]
        else:
            device_info[device] = "integrated"

    # Do a second pass where we replace the integrated devices with CPU name
    for device, name in device_info.items():
        if name == "integrated":
            device_info[device] = device_info["CPU"]

    # Do a third pass where we replace device names with expected TOPS
    script_dir = os.path.dirname(os.path.abspath(__file__))
    file_path = os.path.join(script_dir, 'device_data.json')
    with open(file_path, 'r', encoding="utf-8") as f:
        data = json.load(f)
        for device, name in device_info.items():
            if "GPU" in device:
                device_info[device] = data["GPU"].get(name, 10)
            elif "NPU" in device:
                device_info[device] = data["NPU"].get(name, 5)
            else:
                device_info[device] = 1

    return device_info
