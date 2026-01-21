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
        self.devices = []
        self.pipeline = []
        self.first_iteration = True

    def init_pipeline(self, pipeline):
        self.tracked_elements = []
        self.devices = Core().available_devices
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        for idx, element in enumerate(self.pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                self.tracked_elements.append({"index": idx, "device_idx": 0})

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of devices
        end_of_variants = True
        for element in self.tracked_elements:
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            cur_device_idx = element["device_idx"]
            next_device_idx = (cur_device_idx + 1) % len(self.devices)
            element["device_idx"] = next_device_idx

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
        devices = self.tracked_elements.copy()
        devices = list(map(lambda e: self.devices[e["device_idx"]], devices)) # transform device indices into names
        logger.info("Testing device combination: %s", str(devices))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in self.tracked_elements:
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the device for this element
            device = self.devices[element["device_idx"]]

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
                self.tracked_elements.append({"index": idx, "batch_idx": 0})

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
                self.tracked_elements.append({"index": idx, "nireq_idx": 0})

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
