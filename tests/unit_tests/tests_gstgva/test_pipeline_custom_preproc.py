# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
import os
import json
import tempfile

from pipeline_runner import TestPipelineRunner
from tests_gstgva.utils import BBox, get_model_path, to_gst_path

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
IMAGE_PATH = os.path.join(SCRIPT_DIR, "test_files", "cup.jpg")

D_MODEL_NAME = "yolo11s"
D_MODEL_PATH = get_model_path(D_MODEL_NAME)

D_OPENCV_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec
! gvadetect pre-process-backend=opencv device=CPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_VA_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! video/x-raw(memory:VAMemory)
! gvadetect pre-process-backend=va device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_VA_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! video/x-raw(memory:VAMemory)
! gvadetect pre-process-backend=va-surface-sharing device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_D3D11_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d11upload ! video/x-raw(memory:D3D11Memory),format=NV12
! gvadetect pre-process-backend=d3d11 device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_D3D11_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d11upload ! video/x-raw(memory:D3D11Memory),format=NV12
! gvadetect pre-process-backend=d3d11-surface-sharing device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_D3D12_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d12upload ! video/x-raw(memory:D3D12Memory),format=NV12
! gvadetect pre-process-backend=d3d12 device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_D3D12_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d12upload ! video/x-raw(memory:D3D12Memory),format=NV12
! gvadetect pre-process-backend=d3d12-surface-sharing device=GPU model={to_gst_path(D_MODEL_PATH)} threshold=0.9
! appsink name=mysink emit-signals=true sync=false
"""

D_GOLD_TRUE = [
    BBox(0.582991799458739, 0.3142750898057349, 0.9671368743052611, 0.710030757159208, [], class_id=41, tracker_id=None)
]

C_MODEL_NAME = "resnet50"
C_MODEL_PATH = get_model_path(C_MODEL_NAME)
LABELS_SOURCE_PATH = os.path.join(SCRIPT_DIR, "test_files", "imagenet_custom_pre_proc_resnet.json")
LABELS_FILE_PATH = os.path.join(tempfile.gettempdir(), "imagenet_custom_pre_proc_resnet_labels.txt")


def ensure_labels_file():
    if os.path.isfile(LABELS_FILE_PATH):
        return LABELS_FILE_PATH

    with open(LABELS_SOURCE_PATH, encoding="utf-8") as labels_source_file:
        labels = json.load(labels_source_file)["output_postproc"][0]["labels"]

    with open(LABELS_FILE_PATH, "w", encoding="utf-8") as labels_file:
        labels_file.write("\n".join(labels))
        labels_file.write("\n")

    return LABELS_FILE_PATH


C_OPENCV_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec
! gvaclassify inference-region=full-frame pre-process-backend=opencv device=CPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_VA_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! vapostproc ! video/x-raw(memory:VAMemory)
! gvaclassify inference-region=full-frame pre-process-backend=va device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_VA_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! vajpegdec ! vapostproc ! video/x-raw(memory:VAMemory)
! gvaclassify inference-region=full-frame pre-process-backend=va-surface-sharing device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_D3D11_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d11upload ! video/x-raw(memory:D3D11Memory),format=NV12
! gvaclassify inference-region=full-frame pre-process-backend=d3d11 device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_D3D11_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d11upload ! video/x-raw(memory:D3D11Memory),format=NV12
! gvaclassify inference-region=full-frame pre-process-backend=d3d11-surface-sharing device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_D3D12_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d12upload ! video/x-raw(memory:D3D12Memory),format=NV12
! gvaclassify inference-region=full-frame pre-process-backend=d3d12 device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_D3D12_SURFACE_SHARING_PIPELINE_STR = f"""
appsrc name=mysrc
! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=NV12
! d3d12upload ! video/x-raw(memory:D3D12Memory),format=NV12
! gvaclassify inference-region=full-frame pre-process-backend=d3d12-surface-sharing device=GPU model={to_gst_path(C_MODEL_PATH)} labels-file={to_gst_path(ensure_labels_file())}
! appsink name=mysink emit-signals=true sync=false
"""

C_GOLD_TRUE = [BBox(0, 0, 1, 1, [])]

class TestCustomPreProcPipeline(unittest.TestCase):
    def run_d3d_pipeline(self, pipeline_str, ground_truth, expected_preproc, **kwargs):
        # The inference element posts the pre-processing backend it actually selected on the
        # bus. Check it: a silent fallback (for example d3d11-surface-sharing -> d3d11) still
        # produces the gold boxes.
        selected_preprocessors = []

        def on_element_message(bus, msg):
            structure = msg.get_structure()
            if structure.get_name() == "gva-inference-pre-process":
                selected_preprocessors.append(structure.get_string("selected"))

        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(pipeline_str, IMAGE_PATH, ground_truth, **kwargs)
        pipeline_runner._bus.connect("message::element", on_element_message)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")
        self.assertEqual(set(selected_preprocessors), {expected_preproc},
                         "The element did not run the requested pre-process-backend.")

    def test_custom_opencv_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_OPENCV_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name == "nt", "VA-API is not available on Windows")
    def test_custom_va_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VA_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name == "nt", "VA-API is not available on Windows")
    def test_custom_va_surface_sharing_yolo_11_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            D_VA_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, D_GOLD_TRUE)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name != "nt", "D3D11 is only available on Windows")
    def test_custom_d3d11_yolo_11_pipeline(self):
        self.run_d3d_pipeline(D_D3D11_PIPELINE_STR, D_GOLD_TRUE, "d3d11")

    @unittest.skipIf(os.name != "nt", "D3D11 is only available on Windows")
    def test_custom_d3d11_surface_sharing_yolo_11_pipeline(self):
        self.run_d3d_pipeline(D_D3D11_SURFACE_SHARING_PIPELINE_STR, D_GOLD_TRUE, "d3d11-surface-sharing")

    @unittest.skipIf(os.name != "nt", "D3D12 is only available on Windows")
    def test_custom_d3d12_yolo_11_pipeline(self):
        self.run_d3d_pipeline(D_D3D12_PIPELINE_STR, D_GOLD_TRUE, "d3d12")

    @unittest.skipIf(os.name != "nt", "D3D12 is only available on Windows")
    def test_custom_d3d12_surface_sharing_yolo_11_pipeline(self):
        self.run_d3d_pipeline(D_D3D12_SURFACE_SHARING_PIPELINE_STR, D_GOLD_TRUE, "d3d12-surface-sharing")

    def test_custom_opencv_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_OPENCV_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name == "nt", "VA-API is not available on Windows")
    def test_custom_va_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VA_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name == "nt", "VA-API is not available on Windows")
    def test_custom_va_surface_sharing_resnet_pipeline(self):
        pipeline_runner = TestPipelineRunner()
        pipeline_runner.set_pipeline(
            C_VA_SURFACE_SHARING_PIPELINE_STR, IMAGE_PATH, C_GOLD_TRUE, check_additional_info=False)
        pipeline_runner.run_pipeline()
        for e in pipeline_runner.exceptions:
            print(e)
        pipeline_runner.assertEqual(len(pipeline_runner.exceptions), 0,
                                    "Exceptions have been caught.")

    @unittest.skipIf(os.name != "nt", "D3D11 is only available on Windows")
    def test_custom_d3d11_resnet_pipeline(self):
        self.run_d3d_pipeline(C_D3D11_PIPELINE_STR, C_GOLD_TRUE, "d3d11", check_additional_info=False)

    @unittest.skipIf(os.name != "nt", "D3D11 is only available on Windows")
    def test_custom_d3d11_surface_sharing_resnet_pipeline(self):
        self.run_d3d_pipeline(C_D3D11_SURFACE_SHARING_PIPELINE_STR, C_GOLD_TRUE, "d3d11-surface-sharing",
                              check_additional_info=False)

    @unittest.skipIf(os.name != "nt", "D3D12 is only available on Windows")
    def test_custom_d3d12_resnet_pipeline(self):
        self.run_d3d_pipeline(C_D3D12_PIPELINE_STR, C_GOLD_TRUE, "d3d12", check_additional_info=False)

    @unittest.skipIf(os.name != "nt", "D3D12 is only available on Windows")
    def test_custom_d3d12_surface_sharing_resnet_pipeline(self):
        self.run_d3d_pipeline(C_D3D12_SURFACE_SHARING_PIPELINE_STR, C_GOLD_TRUE, "d3d12-surface-sharing",
                              check_additional_info=False)


if __name__ == "__main__":
    unittest.main()

