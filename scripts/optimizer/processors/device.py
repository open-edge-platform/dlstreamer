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

from openvino import Core
from openvino.properties.device import Type

from processors.utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

class DeviceGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.devices = Core().available_devices
        self.device_groups = []
        self.candidates = []

    def set_allowed_devices(self, devices):
        _devices = Core().available_devices
        for device in devices:
            if not any(device in d for d in _devices):
                raise RuntimeError(f"Device {device} is not supported by this system! Available devices: {str(_devices)}") # pylint: disable=line-too-long
        self.devices = devices

    def init_pipeline(self, initial_pipeline):
        logger.info("Devices allowed for optimization: %s", str(self.devices))

        self.tracked_elements = []
        self.device_groups = []
        self.candidates = []

        instance_ids = {}

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
                        group_idx = len(self.device_groups)
                        self.device_groups.append(0)
                        instance_ids[instance_id] = group_idx

                # if there's no instance id, treat element as its own group
                else:
                    group_idx = len(self.device_groups)
                    self.device_groups.append(0)


                self.tracked_elements.append({
                    "index": idx,
                    "group_idx": group_idx,
                })

        # prepare device information
        info = compile_device_info()
        devices = list(map(lambda e: (e, info[e]), self.devices))

        # prepare all device combinations
        combinations = itertools.product(devices, repeat=len(self.device_groups))

        # transform device combinations into pipeline candidates
        for combination in combinations:
            # preapre the pipeline as well as score info
            pipeline = initial_pipeline.copy()
            total_score = 0
            used_devices = []

            for element in reversed(self.tracked_elements):
                # Get the pipeline element we're modifying
                idx = element["index"]
                (element_type, parameters) = parse_element_parameters(pipeline[idx])

                # Get the device for this element
                device, device_score = combination[self.device_groups[element["group_idx"]]]

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
                used_devices.append(device)

            # store the score, device combination info and the candidate in a priority queue
            heapq.heappush(self.candidates, (total_score, used_devices, pipeline))

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

def compile_device_info():
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
