# ==============================================================================
# Copyright (C) 2018-2021 Intel Corporation
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
            # dima806 ViT models — all share the same output layer name
            if layer_name == '__module.classifier/aten::linear/Add' and len(data) > 0:
                index = int(numpy.argmax(data))
                if 'facial_age' in model_name or 'fairface_age' in model_name:
                    age_labels = ["01", "02", "03", "04", "05", "06-07", "08-09",
                                  "10-12", "13-15", "16-20", "21-25", "26-30", "31-35", "36-40",
                                  "41-45", "46-50", "51-55", "56-60", "61-65", "66-70", "71-80",
                                  "81-90", "90+"]
                    if index < len(age_labels):
                        tensor.set_label(age_labels[index])
                    tensor.set_name("age")
                elif 'gender' in model_name:
                    # id2label: 0=Female, 1=Male
                    tensor.set_label("M" if index == 1 else "F")
                    tensor.set_name("gender")
                elif 'emotion' in model_name:
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
