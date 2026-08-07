# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

from gstgva import VideoFrame
import numpy


def process_frame(frame: VideoFrame) -> bool:
    for roi in frame.regions():
        for tensor in roi.tensors():
            if tensor.name() == 'detection':
                continue
            layer_name = tensor.layer_name()
            model_name = tensor.model_name()
            data = tensor.data()
            model_name = model_name.lower() if model_name else ""

            if data is None or len(data) == 0:
                continue

            # dima806 ViT models may change output layer names between model exports.
            # Use model identity + output shape as primary routing, layer name only as fallback.
            index = int(numpy.argmax(data))
            if 'facial_age' in model_name or 'fairface_age' in model_name or len(data) == 23:
                age_labels = ["01", "02", "03", "04", "05", "06-07", "08-09",
                              "10-12", "13-15", "16-20", "21-25", "26-30", "31-35", "36-40",
                              "41-45", "46-50", "51-55", "56-60", "61-65", "66-70", "71-80",
                              "81-90", "90+"]
                if index < len(age_labels):
                    tensor.set_label(age_labels[index])
                tensor.set_name("age")
                continue
            if 'gender' in model_name or len(data) == 2:
                # id2label: 0=Female, 1=Male
                tensor.set_label("M" if index == 1 else "F")
                tensor.set_name("gender")
                continue
            if 'emotion' in model_name or len(data) == 6:
                emotion_labels = ["Ahegao", "Angry", "Happy", "Neutral", "Sad", "Surprise"]
                if index < len(emotion_labels):
                    tensor.set_label(emotion_labels[index])
                tensor.set_name("emotion")
                continue
            # Legacy Intel model layer names (age-gender-recognition, emotions-recognition)
            if 'age_conv3' == layer_name:
                tensor.set_label(str(int(data[0] * 100)))
                tensor.set_name("age")
                continue
            if 'prob' == layer_name:
                tensor.set_label(" M " if data[1] > 0.5 else " F ")
                tensor.set_name("gender")
                continue
            if 'prob_emotion' == layer_name:
                emotions = ["neutral", "happy", "sad", "surprise", "anger"]
                tensor.set_label(emotions[data.index(max(data))])
                tensor.set_name("emotion")
                continue

    return True
