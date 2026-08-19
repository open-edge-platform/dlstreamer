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

from processors.utils import parse_element_parameters, assemble_parameters

from openvino import Core
from openvino.properties.device import Type

logger = logging.getLogger(__name__)

class DeviceGenerator: # pylint: disable=missing-class-docstring
    def __init__(self):
        self.devices = Core().available_devices

    def set_allowed_devices(self, devices): # pylint: disable=missing-function-docstring
        _devices = Core().available_devices
        for device in devices:
            if not any(device in d for d in _devices):
                raise RuntimeError(f"Device {device} is not supported by this system! Available devices: {str(_devices)}") # pylint: disable=line-too-long
        self.devices = devices

    def generate_candidates(self, candidates): # pylint: disable=too-many-locals, missing-function-docstring
        logger.info("Devices allowed for optimization: %s", str(self.devices))
        new_candidates = []

        for pipeline in candidates:
            tracked_elements, group_count = _search_for_tracked_elements(pipeline)
            extract_device = lambda e: parse_element_parameters(pipeline[e["index"]])[1].get("device", "CPU")
            current_devices = list(map(extract_device, tracked_elements))

            # prepare all device combinations
            combinations = itertools.product(self.devices, repeat=group_count)

            # transform device combinations into pipeline candidates
            for combination in combinations:
                logger.info(f"{str(current_devices)}, {str(list(combination))}")
                # skip if the generated combination equals the original pipeline
                if list(combination) == current_devices:
                    logger.info("skipping")
                    continue

                # prepare the pipeline as well as score info
                candidate = pipeline.copy()

                for element in reversed(tracked_elements):
                    # Get the pipeline element we're modifying
                    idx = element["index"]
                    (element_type, parameters) = parse_element_parameters(pipeline[idx])

                    # Get the device for this element
                    device = combination[element["group_idx"]]

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
                    candidate[idx] = f" {element_type} {parameters}"
                    candidate.insert(idx, f" {memory} ")
                    candidate.insert(idx, " vapostproc ")

                new_candidates.append(candidate)
        candidates.extend(new_candidates)    

###################################################################################################
def _search_for_tracked_elements(pipeline):
    tracked_elements = []
    instance_ids = {}
    group_count = 0

    # prepare device groups
    for idx, element in enumerate(pipeline):
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

            tracked_elements.append({
                "index": idx,
                "group_idx": group_idx,
            })

    return tracked_elements, group_count

def _compile_device_info():
    core = Core()
    available_devices = core.available_devices

    device_info = {}

    # Do a first pass where we collect info about CPUs and discrete devices
    for device in available_devices:
        device_type = core.get_property(device, "DEVICE_TYPE")
        device_name = core.get_property(device, "FULL_DEVICE_NAME")

        if "CPU" in device or device_type == Type.DISCRETE:
            device_info[device] = device_name
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
