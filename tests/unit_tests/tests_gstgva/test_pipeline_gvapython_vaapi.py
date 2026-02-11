# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# pylint: disable=import-error
from pipeline_runner import TestGenericPipelineRunner
# pylint: enable=import-error
import os
import unittest

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "cup.jpg")
MODULE_PATH = os.path.join(SCRIPT_DIR, "test_files", "test_module.py")

PIPELINE_TEMPLATE = "filesrc location={} ! jpegparse ! vaapijpegdec ! vaapipostproc ! video/x-raw(memory:VASurface),format={} ! gvapython module={} class=MyClassVaapi function={} ! {} fakesink sync=false"
ENCODERS = ["", "vaapipostproc ! vaapijpegenc !"]
FORMATS = ["NV12", "I420"]
READ_FUNC = "read_frame_data"
WRITE_FUNC = "write_frame_data"


class TestGvapythonVaapiMap(unittest.TestCase):
    def test_gvapython_vaapi_map(self):
        for enc in ENCODERS:
            for caps_format in FORMATS:
                for func_exception in [(READ_FUNC, False), (WRITE_FUNC, True)]:
                    (func, expect_exception) = func_exception
                    pipeline_str = PIPELINE_TEMPLATE.format(
                        IMAGE_PATH, caps_format, MODULE_PATH, func, enc)
                    pipeline_runner = TestGenericPipelineRunner()
                    pipeline_runner.set_pipeline(pipeline_str)
                    pipeline_runner.run_pipeline()
                    if expect_exception:
                        pipeline_runner.assertGreater(
                            len(pipeline_runner.exceptions), 0, "No exception when mapping VAAPI buffer for writing. Expected at least one.")
                    else:
                        pipeline_runner.assertEqual(
                            len(pipeline_runner.exceptions), 0, "Exceptions have been caught.")


if __name__ == "__main__":
    unittest.main()
