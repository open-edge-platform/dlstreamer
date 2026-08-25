# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest

import test_tensor
import test_region_of_interest
import test_video_frame
import test_audio_event
import test_audio_frame
import test_pipeline_color_formats
import test_pipeline_gvapython
import test_pipeline_optimizer
import test_pipeline_gvafpsthrottle
import test_pipeline_g3dradarprocess
import test_pipeline_g3dlidarparse
import test_pipeline_g3dlidarsrc
import test_pipeline_g3dinference
import test_pipeline_g3dobjectfuser

if __name__ == '__main__':
    loader = unittest.TestLoader()
    suite_gstgva = unittest.TestSuite()

    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_optimizer))
    suite_gstgva.addTests(loader.loadTestsFromModule(test_region_of_interest))
    suite_gstgva.addTests(loader.loadTestsFromModule(test_tensor))
    suite_gstgva.addTests(loader.loadTestsFromModule(test_video_frame))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_color_formats))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_audio_event))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_audio_frame))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_gvapython))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_gvafpsthrottle))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_g3dradarprocess))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_g3dlidarparse))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_g3dlidarsrc))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_g3dinference))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_g3dobjectfuser))

    runner = unittest.TextTestRunner(verbosity=3)
    result = runner.run(suite_gstgva)

    if result.wasSuccessful():
        print("GVA-python tests has passed.")
        exit(0)
    else:
        print("GVA-python tests has failed.")
        exit(1)

