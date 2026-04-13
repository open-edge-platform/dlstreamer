# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import unittest

from pipeline_runner import TestGenericPipelineRunner

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "../../.."))

TEST_FILES_DIR = os.path.join(SCRIPT_DIR, "test_files")
PCD_LOCATION = os.path.join(TEST_FILES_DIR, "%06d.pcd")
PCD_INDEX = 1

STRIDE = 1
FRAME_RATE = 5

PIPELINE_TEMPLATE = (
    "multifilesrc location={location} start-index={start_index} stop-index={stop_index} caps=application/octet-stream "
    "! g3dlidarparse stride={stride} frame-rate={frame_rate} ! fakesink"
)


class TestG3DLidarParsePipeline(unittest.TestCase):
    def test_g3dlidarparse_pcd_pipeline(self):
        pipeline = PIPELINE_TEMPLATE.format(
            location=PCD_LOCATION,
            start_index=PCD_INDEX,
            stop_index=PCD_INDEX,
            stride=STRIDE,
            frame_rate=FRAME_RATE,
        )

        pipeline_runner = TestGenericPipelineRunner()
        pipeline_runner.set_pipeline(pipeline)
        pipeline_runner.run_pipeline()

        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0, "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()