# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging

from openvino import Core

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

class BatchGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.batches = []
        self.pipeline = []
        self.first_iteration = True

    def init_pipeline(self, pipeline):
        self.tracked_elements = []
        self.batches = [1, 2, 4, 8, 16, 32]
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        for idx, element in enumerate(self.pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                self.tracked_elements.append({
                    "index": idx,
                    "batch_idx": 0,
                    "instance_id": "inf" + str(idx)
                })

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of batches
        end_of_variants = True
        for element in self.tracked_elements:
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            cur_batch_idx = element["batch_idx"]
            next_batch_idx = (cur_batch_idx + 1) % len(self.batches)
            element["batch_idx"] = next_batch_idx

            # Walk through elements while they still
            # have more batch options
            if next_batch_idx > cur_batch_idx:
                end_of_variants = False
                break

        # If all elements have rotated through the entire list
        # of available batches, then we have run out of variants
        if end_of_variants:
            raise StopIteration

        # log device combinations
        batches = self.tracked_elements.copy()
        batches = list(map(lambda e: self.batches[e["batch_idx"]], batches)) # transform batch indices into batches
        logger.info("Testing batch combination: %s", str(batches))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in self.tracked_elements:
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the batch for this element
            batch = self.batches[element["batch_idx"]]

            # Apply current configuration
            parameters["batch-size"] = str(batch)
            parameters = assemble_parameters(parameters)
            pipeline[idx] = f" {element_type} {parameters}"

        return pipeline

class NireqGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.nireqs = []
        self.pipeline = []
        self.first_iteration = True

    def init_pipeline(self, pipeline):
        self.tracked_elements = []
        self.nireqs = range(1, 9)
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        for idx, element in enumerate(self.pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                self.tracked_elements.append({
                    "index": idx,
                    "nireq_idx": 0,
                    "instance_id": "inf" + str(idx)
                })

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of nireqs
        end_of_variants = True
        for element in self.tracked_elements:
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            cur_nireq_idx = element["nireq_idx"]
            next_nireq_idx = (cur_nireq_idx + 1) % len(self.nireqs)
            element["nireq_idx"] = next_nireq_idx

            # Walk through elements while they still
            # have more nireq options
            if next_nireq_idx > cur_nireq_idx:
                end_of_variants = False
                break

        # If all elements have rotated through the entire list
        # of available nireqs, then we have run out of variants
        if end_of_variants:
            raise StopIteration

        # log device combinations
        nireqs = self.tracked_elements.copy()
        nireqs = list(map(lambda e: self.nireqs[e["nireq_idx"]], nireqs)) # transform nireq indices into nireqs
        logger.info("Testing nireq combination: %s", str(nireqs))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in self.tracked_elements:
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the nireq for this element
            nireq = self.nireqs[element["nireq_idx"]]

            # Apply current configuration
            parameters["nireq"] = str(nireq)
            parameters = assemble_parameters(parameters)
            pipeline[idx] = f" {element_type} {parameters}"

        return pipeline

####################################### Utils #####################################################

def add_instance_ids(pipeline): # pylint: disable=missing-function-docstring
    ids = {}
    index = 0

    for idx, element in enumerate(pipeline):
        if "gvadetect" in element or "gvaclassify" in element:
            (element_type, parameters) = parse_element_parameters(element)
            instance_id = ids.get(parameters["model"])

            if not instance_id:
                instance_id = "inf" + str(index)
                index += 1
                ids[parameters["model"]] = instance_id

            parameters["model-instance-id"] = instance_id
            parameters = assemble_parameters(parameters)
            pipeline[idx] = f" {element_type} {parameters} "

    return pipeline

# returns element type and parsed parameters
def parse_element_parameters(element):
    parameters = element.strip().split(" ")
    parsed_parameters = {}
    for parameter in parameters[1:]:
        parts = parameter.split("=")
        parsed_parameters[parts[0]] = parts[1]

    return (parameters[0], parsed_parameters)

def assemble_parameters(parameters):
    result = ""
    for parameter, value in parameters.items():
        result = result + parameter + "=" + value + " "

    return result
