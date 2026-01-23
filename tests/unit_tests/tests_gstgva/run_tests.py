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

import test_pipeline_face_detection_and_classification
import test_pipeline_face_detection_and_classification_emotion_ferplus_onnx
import test_pipeline_vehicle_pedestrian_tracker

import test_pipeline_classification_mobilenet_v2_onnx

import test_pipeline_detection_atss

import test_pipeline_gvapython
import test_pipeline_gvapython_vaapi

import test_pipeline_human_pose_estimation
import test_pipeline_action_recognition
import test_pipeline_optimizer

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
        test_pipeline_face_detection_and_classification))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_face_detection_and_classification_emotion_ferplus_onnx))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_vehicle_pedestrian_tracker))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_classification_mobilenet_v2_onnx))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_audio_event))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_audio_frame))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_gvapython))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_gvapython_vaapi))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_action_recognition))
    suite_gstgva.addTests(loader.loadTestsFromModule(
        test_pipeline_human_pose_estimation))

    runner = unittest.TextTestRunner(verbosity=3)
    result = runner.run(suite_gstgva)

    if result.wasSuccessful():
        print("GVA-python tests has passed.")
        exit(0)
    else:
        print("GVA-python tests has failed.")
        exit(1)
