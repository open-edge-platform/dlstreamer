# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os

from pipeline_runner import TestGenericPipelineRunner

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "dog_bike_car.jpg")
MODULE_PATH = os.path.join(SCRIPT_DIR, "test_files", "test_module.py")

PIPELINE_TEMPLATE = "filesrc location={} ! jpegdec ! gvapython module={} {} ! fakesink"

class TestGvaPythonParameters(unittest.TestCase):
    def generate_pipelines(self):
        additional_args = [
            [""],
            ["class=MyClass"],
            ["class=MyClass", "function=\"my_function\""],
            ["class=MyClassWithArgs", "arg=[]", "kwarg=[]"],
            ["class=MyClassWithArgs", "arg=[]"],
            ["class=MyClass", "function=\"my_functions\""],
            ["function=\"my_func\""],
            ["class=MyClass", "arg=[\"foo\"]"]
        ]
        pipelines = []
        for args in additional_args:
            arg_str = " ".join(args)
            pipelines.append(PIPELINE_TEMPLATE.format(IMAGE_PATH, MODULE_PATH, arg_str))
        return pipelines

    def test_gvapython_parameters_pipeline(self):
        pipeline_runner = TestGenericPipelineRunner()
        pipelines = self.generate_pipelines()
        expected_result = [True, True, True, True, False, False, False, False]
        actual_result = []
        for pipeline in pipelines:
            try:
                pipeline_runner.set_pipeline(pipeline)
                pipeline_runner.run_pipeline()
                actual_result.append(True)
            except Exception as e:
                actual_result.append(False)
        pipeline_runner.assertEqual(expected_result, actual_result)

if __name__ == "__main__":
    unittest.main()
