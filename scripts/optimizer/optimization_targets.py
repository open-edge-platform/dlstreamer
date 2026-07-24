# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
class FpsTarget: # pylint: disable=missing-class-docstring
    @staticmethod
    def is_better(new_result, old_result): # pylint: disable=missing-function-docstring, too-few-public-methods
        return new_result["fps"] > old_result["fps"]

class PowerTarget: # pylint: disable=missing-class-docstring
    @staticmethod
    def is_better(new_result, old_result): # pylint: disable=missing-function-docstring, too-few-public-methods
        return new_result["power"] < old_result["power"]
