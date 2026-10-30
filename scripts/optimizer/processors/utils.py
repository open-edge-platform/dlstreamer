# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

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
def parse_element_parameters(element): # pylint: disable=missing-function-docstring
    parameters = element.strip().split(" ")
    parsed_parameters = {}
    for parameter in parameters[1:]:
        parts = parameter.split("=")
        parsed_parameters[parts[0]] = parts[1]

    return (parameters[0], parsed_parameters)

def assemble_parameters(parameters): # pylint: disable=missing-function-docstring
    result = ""
    for parameter, value in parameters.items():
        result = result + parameter + "=" + value + " "

    return result

# prints device, batch-size and nireq of every inference element in a candidate pipeline
def print_inference_parameters(pipeline, printer=print): # pylint: disable=missing-function-docstring
    if isinstance(pipeline, str):
        pipeline = pipeline.split("!")

    for idx, element in enumerate(pipeline):
        if "gvadetect" not in element and "gvaclassify" not in element:
            continue

        (element_type, parameters) = parse_element_parameters(element)
        printer(
            f"[{idx}] {element_type}: "
            f"device={parameters.get('device', 'CPU')} "
            f"batch-size={parameters.get('batch-size', '1')} "
            f"nireq={parameters.get('nireq', '1')}"
        )
