# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging

from openvino import Core
from .utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

class DeviceGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.devices = Core().available_devices
        logger.info("Devices detected on system: %s", str(self.devices))
        self.device_groups = []
        self.pipeline = []
        self.first_iteration = True

    def set_allowed_devices(self, devices):
        _devices = Core().available_devices
        for device in devices:
            if not any(device in d for d in _devices):
                raise RuntimeError("Device %s is not supported by this system! Available devices: %s" % (device, str(_devices))) # pylint: disable=line-too-long
        self.devices = devices        

    def init_pipeline(self, pipeline):
        logger.info("Devices allowed for optimization: %s", str(self.devices))

        self.tracked_elements = []
        self.device_groups = []
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        instance_ids = {}

        for idx, element in enumerate(self.pipeline):
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

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of devices
        end_of_variants = True
        for idx, cur_device_idx in enumerate(self.device_groups):
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            next_device_idx = (cur_device_idx + 1) % len(self.devices)
            self.device_groups[idx] = next_device_idx

            # Walk through elements while they still
            # have more device options
            if next_device_idx > cur_device_idx:
                end_of_variants = False
                break

        # If all elements have rotated through the entire list
        # of available devices, then we have run out of variants
        if end_of_variants:
            raise StopIteration

        # log device combinations
        devices = self.device_groups.copy()
        devices = list(map(lambda e: self.devices[e], devices)) # transform device indices into names
        logger.info("Testing device combination: %s", str(devices))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in reversed(self.tracked_elements):
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the device for this element
            device = self.devices[self.device_groups[element["group_idx"]]]

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

        return pipeline
