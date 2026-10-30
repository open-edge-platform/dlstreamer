# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import heapq
import json
import logging
from collections import Counter
from pathlib import Path

from processors.utils import parse_element_parameters

logger = logging.getLogger(__name__)

FPS_WEIGHTS_FILE = Path(__file__).parent / "fps_weights.json"
POWER_WEIGHTS_FILE = Path(__file__).parent / "power_weights.json"
DEFAULT_WEIGHT = 0.5
DEVICE_REUSE_PENALTY = 0.8

class FpsTarget: # pylint: disable=missing-class-docstring
    @staticmethod
    def is_better(new_result, old_result): # pylint: disable=missing-function-docstring, too-few-public-methods
        return new_result["fps"] > old_result["fps"]

    @staticmethod
    def sort_candidates(candidates): # pylint: disable=missing-function-docstring
        with open(FPS_WEIGHTS_FILE, "r", encoding="utf-8") as weights_file:
            weights = json.load(weights_file)

        scored_candidates = []

        for index, candidate in enumerate(candidates):
            score = 1.0
            device_usage = Counter()

            for element in candidate:
                if "gvadetect" not in element and "gvaclassify" not in element:
                    continue

                (element_type, parameters) = parse_element_parameters(element)
                element_weights = weights.get(element_type, {})

                for parameter, value in parameters.items():
                    if parameter not in element_weights:
                        continue
                    score *= element_weights[parameter].get(value, DEFAULT_WEIGHT)

                # elements sharing a device compete for it, so every repeated
                # use of the same device stacks another penalty
                device = parameters.get("device")
                if device is not None:
                    score *= DEVICE_REUSE_PENALTY ** device_usage[device]
                    device_usage[device] += 1

            # negated score turns the min-heap into a max-heap, the index keeps
            # the comparison from reaching the candidate lists on score ties
            heapq.heappush(scored_candidates, (-score, index, candidate))

        return scored_candidates

class PowerTarget: # pylint: disable=missing-class-docstring
    @staticmethod
    def is_better(new_result, old_result): # pylint: disable=missing-function-docstring, too-few-public-methods
        return new_result["power"] < old_result["power"]

    @staticmethod
    def sort_candidates(candidates): # pylint: disable=missing-function-docstring
        with open(POWER_WEIGHTS_FILE, "r", encoding="utf-8") as weights_file:
            weights = json.load(weights_file)

        scored_candidates = []

        for index, candidate in enumerate(candidates):
            score = 1.0

            for element in candidate:
                if "gvadetect" not in element and "gvaclassify" not in element:
                    continue

                (element_type, parameters) = parse_element_parameters(element)
                element_weights = weights.get(element_type, {})

                for parameter, value in parameters.items():
                    if parameter not in element_weights:
                        continue
                    score *= element_weights[parameter].get(value, DEFAULT_WEIGHT)

            heapq.heappush(scored_candidates, (score, index, candidate))

        return scored_candidates
