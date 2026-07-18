# pylint: disable=missing-module-docstring
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
class FpsTarget:
    @staticmethod
    def is_better(new_result, old_result):
        return new_result["fps"] > old_result["fps"]

class PowerTarget:
    @staticmethod
    def is_better(new_result, old_result):
        return new_result["power"] < old_result["power"]