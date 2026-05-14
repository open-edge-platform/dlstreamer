# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging

from .utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

class BatchGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.batches = [1, 2, 4, 8, 16, 32]
        self.batch_groups = []
        self.pipeline = []
        self.first_iteration = True

    def set_batch_sizes(self, sizes):
        self.batches = sizes      

    def init_pipeline(self, pipeline):
        self.tracked_elements = []
        self.batch_groups = []
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        instance_ids = {}

        for idx, element in enumerate(self.pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                (_, parameters) = parse_element_parameters(element)
                instance_id = parameters.get("model-instance-id")
                group_idx = 0

                # if element has an instance id, get the batch group index
                if instance_id:
                    group_idx = instance_ids.get(instance_id)

                    # if this instance id is new, create a new group index
                    if group_idx is None:
                        group_idx = len(self.batch_groups)
                        self.batch_groups.append(0)
                        instance_ids[instance_id] = group_idx

                # if there's no instance id, treat element as its own group
                else:
                    group_idx = len(self.batch_groups)
                    self.batch_groups.append(0)


                self.tracked_elements.append({
                    "index": idx,
                    "group_idx": group_idx,
                })

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of batches
        end_of_variants = True
        for idx, cur_batch_idx in enumerate(self.batch_groups):
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            next_batch_idx = (cur_batch_idx + 1) % len(self.batches)
            self.batch_groups[idx] = next_batch_idx

            # Walk through elements while they still
            # have more batch options
            if next_batch_idx > cur_batch_idx:
                end_of_variants = False
                break

        # If all elements have rotated through the entire list
        # of available batches, then we have run out of variants
        if end_of_variants:
            raise StopIteration

        # log batch combinations
        batches = self.batch_groups.copy()
        batches = list(map(lambda e: self.batches[e], batches)) # transform batch indices into batches
        logger.info("Testing batch combination: %s", str(batches))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in self.tracked_elements:
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the batch for this element
            batch = self.batches[self.batch_groups[element["group_idx"]]]

            # Apply current configuration
            parameters["batch-size"] = str(batch)
            parameters = assemble_parameters(parameters)
            pipeline[idx] = f" {element_type} {parameters}"

        return pipeline
