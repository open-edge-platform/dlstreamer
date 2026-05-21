# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os

models_path = os.environ["MODELS_PATH"]
model_procs_path = os.environ["MODELS_PROC_PATH"]


def env_var_value(env_var):
    try:
        env_var_values = os.environ[env_var].split(':')
    except KeyError:
        env_var_values = None
    return env_var_values


def get_model_path(model_name, type="ir", precision="FP32"):
    if os.path.isfile(model_name) and model_name.endswith(".xml"):
        return model_name
    models_path_list = env_var_value('MODELS_PATH')

    # new models location - path/PR/name.xml, path/name.onnx
    for models_path in models_path_list:
        for path, subdirs, files in os.walk(models_path):
            for name in files:
                if type == "ir":
                    if ((precision.lower() in path.lower() and name == (model_name + ".xml")) or
                            name == "{}-{}.xml".format(model_name, precision.lower())):
                        return (os.path.join(path, name))
                if type == "onnx":
                    if (name == model_name + '.onnx'):
                        return (os.path.join(path, name))

    # old models location - path/name-pr.xml
    for models_path in models_path_list:
        for path, subdirs, files in os.walk(models_path):
            for name in files:
                if (precision == "FP32" and name == model_name + '.xml'):
                    return (os.path.join(path, name))

    raise ValueError("Model was not found. Check your MODELS_PATH={} environment variable or model's name ({}) & precision ({})".format(
        models_path_list, model_name, precision))


def get_model_proc_path(model_name):
    if os.path.isfile(model_name) and model_name.endswith(".json"):
        return model_name
    models_procs_path = env_var_value("MODELS_PROC_PATH")
    if models_procs_path:
        for model_proc_path in models_procs_path:
            for path, subdirs, files in os.walk(model_proc_path):
                for name in files:
                    if f'{model_name}.json' == name:
                        return (os.path.join(path, name))
    else:
        pwd = env_var_value("PWD")[0]
        gst_va_path = pwd[:pwd.find(
            "gst-video-analytics") + len("gst-video-analytics")]
        samples_path = gst_va_path + "/samples"
        for path, subdirs, files in os.walk(samples_path):
            for name in files:
                if f'{model_name}.json' == name:
                    return (os.path.join(path, name))
    return None

def get_video_path(video_name):
    """
    Find video file in VIDEO_EXAMPLES_DIR environment variable path.
    """
    if os.path.isfile(video_name):
        return video_name

    video_examples_dir = os.environ.get("VIDEO_EXAMPLES_DIR")
    if not video_examples_dir:
        raise ValueError("VIDEO_EXAMPLES_DIR environment variable is not set")

    video_examples_path_list = video_examples_dir.split(':')

    # Search in all paths from VIDEO_EXAMPLES_DIR
    for video_path in video_examples_path_list:
        for path, subdirs, files in os.walk(video_path):
            if video_name in files:
                full_path = os.path.join(path, video_name)
                if os.path.exists(full_path):
                    return full_path

    raise ValueError("Video file '{}' was not found. Check your VIDEO_EXAMPLES_DIR={} environment variable or video file name".format(
        video_name, video_examples_path_list))

class BBox:
    def __init__(self, x_min, y_min, x_max, y_max, additional_info=None, class_id=0, tracker_id=None):
        self.x_min, self.y_min, self.x_max, self.y_max = x_min, y_min, x_max, y_max
        self.additional_info = additional_info
        self.class_id = class_id
        self.tracker_id = tracker_id

    def __repr__(self):
        return f"BBox({str(self)})"

    def __str__(self):
        return f"<({self.x_min}, {self.y_min}, {self.x_max}, {self.y_max}), {self.additional_info}, class_id={self.class_id}, tracker_id={self.tracker_id}>"

    @ staticmethod
    def IoU(bbox_1, bbox_2):
        if (bbox_1.x_max < bbox_2.x_min or
            bbox_2.x_max < bbox_1.x_min or
            bbox_1.y_max < bbox_2.y_min or
                bbox_2.y_max < bbox_1.y_min):
            return 0

        i_x_min = max(bbox_1.x_min, bbox_2.x_min)
        i_y_min = max(bbox_1.y_min, bbox_2.y_min)
        i_x_max = min(bbox_1.x_max, bbox_2.x_max)
        i_y_max = min(bbox_1.y_max, bbox_2.y_max)

        intersection_area = (i_x_max - i_x_min) * (i_y_max - i_y_min)
        bbox_1_area = (bbox_1.x_max - bbox_1.x_min) * \
            (bbox_1.y_max - bbox_1.y_min)
        bbox_2_area = (bbox_2.x_max - bbox_2.x_min) * \
            (bbox_2.y_max - bbox_2.y_min)
        union_area = bbox_1_area + bbox_2_area - intersection_area

        return (intersection_area / union_area)

    @ staticmethod
    def additional_info_is_equal(pr_info, gt_info):
        if pr_info == gt_info == None:
            return True
        truncated_pr_info = list()
        for gt_info_item in gt_info:
            layer_name = gt_info_item['layer_name']
            for pr_info_item in pr_info:
                if layer_name == pr_info_item['layer_name']:
                    truncated_pr_info.append(pr_info_item)
        pr_info = truncated_pr_info
        if len(pr_info) != len(gt_info):
            print(len(pr_info), len(gt_info))
            return False
        pr_info = sorted(pr_info, key=lambda i: i['layer_name'])
        gt_info = sorted(gt_info, key=lambda i: i['layer_name'])
        if pr_info == gt_info:
            return True

        for pr_info_item, gt_info_item in zip(pr_info, gt_info):
            if 'format' in pr_info_item and 'format' in gt_info_item:
                if pr_info_item['format'] == gt_info_item['format'] == "landmark_points":
                    for i, (pr_data, gt_data) in enumerate(zip(pr_info_item['data'], gt_info_item['data'])):
                        if abs(pr_data - gt_data) > 0.01:
                            print("landmark_points[{}]: ".format(
                                i), pr_data, gt_data)
                            return False
                if pr_info_item['format'] == gt_info_item['format'] == 'keypoints':
                    for i, (pr_data, gt_data) in enumerate(zip(pr_info_item['data'], gt_info_item['data'])):
                        if abs(pr_data - gt_data) > 0.01:
                            print("keypoints[{}]: ".format(
                                i), pr_data, gt_data)
                            return False
            if 'name' in pr_info_item and 'name' in gt_info_item:
                supported_names = ["emotion", "gender", "age",
                                   "person-attributes", "type", "color", "action"]
                pr_name = pr_info_item['name']
                if pr_name == gt_info_item['name'] and pr_name in supported_names and pr_info_item['label'] != gt_info_item['label']:
                    print(
                        f"Labels aren't equal for '{pr_name}': pr - '{pr_info_item['label']}'; gt - '{gt_info_item['label']}'")
                    return False

        return True

    @ staticmethod
    def bboxes_is_equal(pr_bboxes: list, gt_bboxes: list, only_number=False, check_additional_info=True):
        correspondence_matrix = dict()
        if len(pr_bboxes) == len(gt_bboxes):
            if only_number:
                return True
        else:
            print("Number of bboxes is not equal: pr: {}, gt: {}".format(len(pr_bboxes),
                                                                         len(gt_bboxes)))
            print("Predicted bboxes:", pr_bboxes)
            return False

        for pr_bbox in pr_bboxes:
            max_corresponde_gt_bbox = None
            max_iou = -1
            max_iou_index = None
            for i, gt_bbox in enumerate(gt_bboxes):
                iou = BBox.IoU(pr_bbox, gt_bbox)
                # Different components (OpenVINO™ Toolkit and its plugins, VAS OT, etc.) can change between releases. To track exact accuracy we have Regression Tests.
                # This IoU check is just sanity check to find out if things really got bad
                if (iou > 0.7 and iou <= 1 and iou > max_iou and gt_bbox.class_id == pr_bbox.class_id and gt_bbox.tracker_id == pr_bbox.tracker_id):
                    max_corresponde_gt_bbox = gt_bbox
                    max_iou = iou
                    max_iou_index = i
            correspondence_matrix[pr_bbox] = max_corresponde_gt_bbox
            if max_iou_index != None:
                gt_bboxes.pop(max_iou_index)
        if None in correspondence_matrix.values():
            print(correspondence_matrix)
            return False
        if check_additional_info:
            for pr_bbox, gt_bbox in correspondence_matrix.items():
                if not BBox.additional_info_is_equal(
                        pr_bbox.additional_info[:], gt_bbox.additional_info[:]):
                    return False
        return True


if __name__ == "__main__":
    bboxes_1 = [BBox(100, 50, 300, 150), BBox(100, 50, 300, 150)]
    bboxes_2 = [BBox(104, 52, 304, 152), BBox(100, 50, 300, 150)]
    print(BBox.bboxes_is_equal(bboxes_2[:], bboxes_1[:]))
    print(len(bboxes_2), len(bboxes_1))
    pass
