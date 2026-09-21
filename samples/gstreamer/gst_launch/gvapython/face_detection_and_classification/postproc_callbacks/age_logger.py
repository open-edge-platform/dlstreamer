# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

from gstgva import VideoFrame


class AgeLogger:
    def __init__(self, log_file_path):
        self.log_file = open(log_file_path, "a")

    def log_age(self, frame: VideoFrame) -> bool:
        for roi in frame.regions():
            for tensor in roi.tensors():
                if tensor.name() == 'detection':
                    continue
                # Match by tensor name (set by age_gender_classification.py) or legacy layer name
                if tensor.name() == 'age' or tensor.layer_name() == 'age_conv3':
                    self.log_file.write(tensor.label() + "\n")
                    continue
        return True

    def __del__(self):
        self.log_file.close()
