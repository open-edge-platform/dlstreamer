# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import logging

from .utils import parse_element_parameters, assemble_parameters

logger = logging.getLogger(__name__)

class NireqGenerator:
    def __init__(self):
        self.tracked_elements = []
        self.nireqs = range(1, 9)
        self.nireq_groups = []
        self.pipeline = []
        self.first_iteration = True

    def set_nireq_sizes(self, sizes):
        self.nireqs = sizes    

    def init_pipeline(self, pipeline):
        self.tracked_elements = []
        self.nireq_groups = [] 
        self.pipeline = pipeline.copy()
        self.first_iteration = True

        instance_ids = {}

        for idx, element in enumerate(self.pipeline):
            if "gvadetect" in element or "gvaclassify" in element:
                (_, parameters) = parse_element_parameters(element)
                instance_id = parameters.get("model-instance-id")
                group_idx = 0

                # if element has an instance id, get the nireq group index
                if instance_id:
                    group_idx = instance_ids.get(instance_id)

                    # if this instance id is new, create a new group index
                    if group_idx is None:
                        group_idx = len(self.nireq_groups)
                        self.nireq_groups.append(0)
                        instance_ids[instance_id] = group_idx

                # if there's no instance id, treat element as its own group
                else:
                    group_idx = len(self.nireq_groups)
                    self.nireq_groups.append(0)


                self.tracked_elements.append({
                    "index": idx,
                    "group_idx": group_idx,
                })

    def __iter__(self):
        return self

    def __next__(self) -> list:
        # Prepare the next combination of nireqs
        end_of_variants = True
        for idx, cur_nireq_idx in enumerate(self.nireq_groups):
            # Don't change anything on first iteration
            if self.first_iteration:
                self.first_iteration = False
                end_of_variants = False
                break

            next_nireq_idx = (cur_nireq_idx + 1) % len(self.nireqs)
            self.nireq_groups[idx] = next_nireq_idx

            # Walk through elements while they still
            # have more nireq options
            if next_nireq_idx > cur_nireq_idx:
                end_of_variants = False
                break

        # If all elements have rotated through the entire list
        # of available nireqs, then we have run out of variants
        if end_of_variants:
            raise StopIteration

        # log nireq combinations
        nireqs = self.nireq_groups.copy()
        nireqs = list(map(lambda e: self.nireqs[e], nireqs)) # transform nireq indices into nireqs
        logger.info("Testing nireq combination: %s", str(nireqs))

        # Prepare pipeline output
        pipeline = self.pipeline.copy()
        for element in self.tracked_elements:
            # Get the pipeline element we're modifying
            idx = element["index"]
            (element_type, parameters) = parse_element_parameters(pipeline[idx])

            # Get the nireq for this element
            nireq = self.nireqs[self.nireq_groups[element["group_idx"]]]

            # Apply current configuration
            parameters["nireq"] = str(nireq)
            parameters = assemble_parameters(parameters)
            pipeline[idx] = f" {element_type} {parameters}"

        return pipeline
