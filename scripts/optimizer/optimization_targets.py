# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import heapq
import json
import logging
from pathlib import Path

from utils import parse_element_parameters

logger = logging.getLogger(__name__)

WEIGHTS_FILE = Path(__file__).parent / "fps_weights.json"
DEFAULT_WEIGHT = 0.5

class FpsTarget: # pylint: disable=missing-class-docstring
    @staticmethod
    def is_better(new_result, old_result): # pylint: disable=missing-function-docstring, too-few-public-methods
        return new_result["fps"] > old_result["fps"]

    @staticmethod
    def sort_candidates(candidates): # pylint: disable=missing-function-docstring
        with open(WEIGHTS_FILE, "r", encoding="utf-8") as weights_file:
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
        scored_candidates = []

        # no power weights available yet, every candidate scores the same
        for index, candidate in enumerate(candidates):
            heapq.heappush(scored_candidates, (-1.0, index, candidate))

        return scored_candidates
