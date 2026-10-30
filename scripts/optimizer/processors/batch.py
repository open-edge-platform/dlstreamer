# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging
import itertools

from processors.utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

class BatchGenerator: # pylint: disable=missing-class-docstring
    def __init__(self):
        self.tracked_elements = []
        self.batches = [1, 2, 4, 8, 16, 32]
        self.batch_groups = []
        self.pipeline = []
        self.first_iteration = True

    def set_batch_sizes(self, sizes): # pylint: disable=missing-function-docstring
        self.batches = sizes

    def generate_candidates(self, candidates): # pylint: disable=too-many-locals, missing-function-docstring
        logger.info("Batch sizes allowed for optimization: %s", str(self.batches))
        new_candidates = []

        for pipeline in candidates:
            tracked_elements, group_count = _search_for_tracked_elements(pipeline)
            extract_batch = lambda e: parse_element_parameters(pipeline[e["index"]])[1].get("batch-size", "1")
            current_batches = list(map(extract_batch, tracked_elements))

            # prepare all batch combinations
            combinations = itertools.product(self.batches, repeat=group_count)

            # transform batch combinations into pipeline candidates
            for combination in combinations:
                # skip if the generated combination equals the original pipeline
                if list(combination) == current_batches:
                    continue

                # prepare the pipeline
                candidate = pipeline.copy()

                for element in tracked_elements:
                    # Get the pipeline element we're modifying
                    idx = element["index"]
                    (element_type, parameters) = parse_element_parameters(pipeline[idx])

                    # Get the batch for this element
                    batch = combination[element["group_idx"]]

                    # Apply current configuration
                    parameters["batch-size"] = str(batch)
                    parameters = assemble_parameters(parameters)
                    candidate[idx] = f" {element_type} {parameters}"

                new_candidates.append(candidate)
        candidates.extend(new_candidates)

###################################################################################################
def _search_for_tracked_elements(pipeline):
    tracked_elements = []
    instance_ids = {}
    group_count = 0

    # prepare batch groups
    for idx, element in enumerate(pipeline):
        if "gvadetect" in element or "gvaclassify" in element:
            (_, parameters) = parse_element_parameters(element)
            instance_id = parameters.get("model-instance-id")
            group_idx = 0

            # if element has an instance id, get the batch group index
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
