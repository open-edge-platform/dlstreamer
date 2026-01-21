# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import re

preprocessing_rules = {
    "vaapipostproc": "vapostproc",
    "vaapi-surface-sharing": "va-surface-sharing",
    "memory:VASurface": "memory:VAMemory",
    r"! capsfilter caps=video/x-raw\(memory:VAMemory\) !(.*(gvadetect|gvaclassify))": r"!\1",
    r"! video/x-raw\(memory:VAMemory\) !(.*(gvadetect|gvaclassify))": r"!\1",
    r"parsebin ! vah\w*dec": "decodebin3",
    r"\w*parse ! vah\w*dec": "decodebin3",
    r"\bdecodebin\b": "decodebin3",
}

def preprocess_pipeline(pipeline):
    for pattern, replacement in preprocessing_rules.items():
        if re.search(pattern, pipeline):
            pipeline = re.sub(pattern, replacement, pipeline)
    return pipeline
