# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================


"""This script simultaneously executes DL Streamer and DeepStream pipelines based on
the detected hardware configuration. Usage:
python3 ./coexistence_dls_and_ds.py <input> LPR <output> [-simultaneously]"""

import glob
import os
import re
import subprocess
import sys
import threading

# Get arguments
if len(sys.argv) not in [4,5]:
    print("Error:\nInvalid number of arguments. Usage:")
    print("python3 ./coexistence_dls_and_ds.py <input> LPR <output> [-simultaneously]\n")
    sys.exit()

user_input, pipeline, output_file = sys.argv[1], sys.argv[2], sys.argv[3]
simultaneously = False
if len(sys.argv) == 5 and sys.argv[4] == "-simultaneously":
    simultaneously = True

if output_file.endswith(".mp4"):
    output_file, _ = os.path.splitext(output_file)

# Check if input is rtsp or uri or file
if user_input.startswith("rtsp://"):
    source = f"rtspsrc location={user_input}"
elif user_input.startswith("https://"):
    source = f"urisourcebin buffer-size=4096 uri={user_input}"
else:
    source = f"filesrc location=/working_dir/{user_input}"

# Definition of pipelines
dlstreamer_pipelines={"LPR": f"""gst-launch-1.0 {source} ! decodebin3 ! vapostproc !
video/x-raw\\(memory:VAMemory\\) ! queue ! gvadetect
model=/working_dir/public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml
device=GPU pre-process-backend=va ! queue ! videoconvert ! gvaclassify
model=/working_dir/public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml device=GPU
pre-process-backend=va ! queue ! vapostproc ! gvawatermark ! gvafpscounter ! vah264enc
bitrate=2000 ! h264parse ! mp4mux ! filesink location=/working_dir/{output_file}_dls.mp4"""}
dlstreamer_pipelines["LPR"]=dlstreamer_pipelines["LPR"].replace("\n", " ")

deepstream_pipelines={"LPR": f"""gst-launch-1.0 {source} ! qtdemux ! h264parse !
nvv4l2decoder ! m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1080 
batched-push-timeout=40000 ! nvdslogger fps-measurement-interval-sec=1 ! queue ! nvvideoconvert
! video/x-raw\\(memory:NVMM\\),format=RGBA ! nvinfer config-file-path=
/working_dir/deepstream_tao_apps/configs/nvinfer/trafficcamnet_tao/pgie_trafficcamnet_config.txt
unique-id=1 ! queue ! nvinfer config-file-path=
/working_dir/deepstream_tao_apps/configs/nvinfer/LPD_us_tao/sgie_lpd_DetectNet2_us.txt
unique-id=2 ! queue ! nvinfer config-file-path=
/working_dir/deepstream_tao_apps/configs/nvinfer/lpr_us_tao/sgie_lpr_us_config.txt
unique-id=3 ! queue ! nvdsosd display-text=1 display-bbox=1 display-mask=0 process-mode=1 !
nvvideoconvert ! video/x-raw\\(memory:NVMM\\),format=NV12 ! nvv4l2h264enc bitrate=2000000 !
h264parse ! qtmux ! filesink location=/working_dir/{output_file}_ds.mp4 sync=false"""}
deepstream_pipelines["LPR"]=deepstream_pipelines["LPR"].replace("\n", " ")

if pipeline not in dlstreamer_pipelines or pipeline not in deepstream_pipelines:
    AVAILABLE_PIPELINES = " ".join(dlstreamer_pipelines.keys())
    print(f"Pipeline {pipeline} not found.\nAvailable pipelines: {AVAILABLE_PIPELINES}")
    sys.exit()

# Check if there is /dev/dri folder to run on GPU
if os.path.exists("/dev/dri"):
    group_id_dri=os.stat(glob.glob("/dev/dri/render*")[0]).st_gid
    DEVICE_DRI=f"--device /dev/dri --group-add {group_id_dri}"
else:
    DEVICE_DRI=""

# Check if there is /dev/accel folder to run on NPU
if os.path.exists("/dev/accel"):
    group_id_accel=os.stat(glob.glob("/dev/accel/accel*")[0]).st_gid
    DEVICE_ACCEL=f"--device /dev/accel --group-add {group_id_accel}"
else:
    DEVICE_ACCEL=""

# Variable for running commands from DL Streamer Docker
cwd = os.getcwd()
display = os.environ["DISPLAY"]
home_path = os.environ["HOME"]
dlstreamer_docker=f"""docker run -i --rm -v {cwd}:/working_dir {DEVICE_DRI} {DEVICE_ACCEL}
-v {home_path}/.Xauthority:/root/.Xauthority -v /tmp/.X11-unix/:/tmp/.X11-unix/ -e 
DISPLAY={display} -v /dev/bus/usb:/dev/bus/usb --env MODELS_PATH=/working_dir
intel/dlstreamer:latest /bin/bash -c"""
dlstreamer_docker=dlstreamer_docker.replace("\n", " ")

deepstream_docker=f"""docker run -i --rm --network=host --gpus all -e DISPLAY={display}
--device /dev/snd -v /tmp/.X11-unix/:/tmp/.X11-unix -v {cwd}:/working_dir -w /working_dir
nvcr.io/nvidia/deepstream:8.0-samples-multiarch /bin/bash -c"""
deepstream_docker=deepstream_docker.replace("\n", " ")

def require_model_files(platform, relative_paths):
    missing = [path for path in relative_paths if not os.path.isfile(os.path.join(cwd, path))]
    if missing:
        print(f"Error: {platform} model files are missing:")
        for path in missing:
            print(f"  {path}")
        if platform == "DL Streamer":
            print("Prepare them with scripts/download_models/download_other_models.py; see its README for setup.")
        else:
            print("Prepare the TAO artifacts and deepstream_tao_apps checkout before running.")
        sys.exit(1)

def prepare_ds_lpr_parser():
    setup_command = (
        "cd /working_dir/deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/"
        "nvinfer_custom_lpr_parser && make && "
        "cp /working_dir/deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/dict_us.txt "
        "/working_dir/dict.txt"
    )
    result = subprocess.run(f'{deepstream_docker} "{setup_command}"', shell=True, check=False)  # nosec
    if result.returncode != 0:
        print("Error: Failed to prepare the DeepStream LPR parser.")
        sys.exit(1)

# Check for Intel and Nvidia hardware
lspci_output=os.popen("lspci -nn").read().split("\n")
video_pattern = re.compile("^.*?(VGA|3D|Display).*$")
INTEL_GPU=False
NVIDIA_GPU=False
INTEL_NPU=False
INTEL_CPU=False
for pci_dev in lspci_output:
    if video_pattern.match(pci_dev) and "Intel" in pci_dev:
        INTEL_GPU=True
    elif video_pattern.match(pci_dev) and "NVIDIA" in pci_dev:
        NVIDIA_GPU=True

if os.path.exists("/dev/accel"):
    INTEL_NPU=True
lscpu_output=os.popen("lscpu").read().replace("\n", " ")
if "Intel" in lscpu_output:
    INTEL_CPU=True

def print_intel_detected(hardware):
    """Prints Intel hardware detection box"""
    print("---------------------------------------")
    print(f" Intel {hardware} detected. Using DL Streamer")
    print("---------------------------------------\n")

def print_nvidia_detected():
    """Prints NVIDIA hardware detection box"""
    print("---------------------------------------")
    print(" NVIDIA GPU detected. Using DeepStream")
    print("---------------------------------------\n")

def run_dlstreamer_pipeline():
    """Run the DL Streamer pipeline."""
    dls_pipeline = dlstreamer_pipelines[pipeline]
    print(f"DL STREAMER PIPELINE:\n{dls_pipeline}\n\n")
    with subprocess.Popen(f"{dlstreamer_docker} \"{dls_pipeline}\"", shell=True, # nosec
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1) as dls_proc: # nosec
        for line in dls_proc.stdout:
            print(f"[DL Streamer] {line.rstrip()}")

def run_deepstream_pipeline():
    """Run the DeepStream pipeline."""
    ds_pipeline = deepstream_pipelines[pipeline]
    print(f"DEEPSTREAM PIPELINE:\n{ds_pipeline}\n\n")
    with subprocess.Popen(f"{deepstream_docker} \"{ds_pipeline}\"", shell=True, # nosec
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1) as ds_proc: # nosec
        for line in ds_proc.stdout:
            print(f"[DeepStream] {line.rstrip()}")

def replace_in_dlstreamer_pipeline(from_str, to_str):
    """Replaces strings in the DL Streamer pipeline to enable conversion for
    NPU or CPU execution."""
    dlstreamer_pipelines[pipeline] = re.sub(f"{from_str}", f"{to_str}",
    dlstreamer_pipelines[pipeline])

if INTEL_GPU or INTEL_NPU or INTEL_CPU:
    require_model_files("DL Streamer", [
        "public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml",
        "public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml",
    ])

if NVIDIA_GPU:
    require_model_files("DeepStream", [
        "deepstream_tao_apps/models/trafficcamnet/resnet18_trafficcamnet_pruned.onnx",
        "deepstream_tao_apps/models/trafficcamnet/resnet18_trafficcamnet_pruned_int8.txt",
        "deepstream_tao_apps/models/LPD_us/LPDNet_usa_pruned_tao5.onnx",
        "deepstream_tao_apps/models/LPD_us/usa_cal_10.1.0.bin",
        "deepstream_tao_apps/models/LPD_us/usa_lpd_label.txt",
        "deepstream_tao_apps/models/LPR_us/us_lprnet_baseline18_deployable.onnx",
        "deepstream_tao_apps/models/LPR_us/labels_us.txt",
        "deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/dict_us.txt",
    ])
    prepare_ds_lpr_parser()

# Run pipeline
if NVIDIA_GPU and INTEL_GPU:
    print_nvidia_detected()
    print_intel_detected("GPU")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t2 = threading.Thread(target=run_deepstream_pipeline)
    if simultaneously:
        t1.start()
        t2.start()
        t1.join()
        t2.join()
    else:
        t1.start()
        t1.join()
        t2.start()
        t2.join()
elif NVIDIA_GPU and INTEL_NPU:
    print_nvidia_detected()
    print_intel_detected("NPU")
    replace_in_dlstreamer_pipeline("GPU", "NPU")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t2 = threading.Thread(target=run_deepstream_pipeline)
    if simultaneously:
        t1.start()
        t2.start()
        t1.join()
        t2.join()
    else:
        t1.start()
        t1.join()
        t2.start()
        t2.join()
elif NVIDIA_GPU and INTEL_CPU:
    print_nvidia_detected()
    print_intel_detected("CPU")
    replace_in_dlstreamer_pipeline("GPU", "CPU")
    replace_in_dlstreamer_pipeline("vapostproc !", "")
    replace_in_dlstreamer_pipeline("pre-process-backend=va", "pre-process-backend=opencv")
    replace_in_dlstreamer_pipeline(r"video/x-raw\\\(memory:VAMemory\\\) !", "")
    replace_in_dlstreamer_pipeline("vah264enc bitrate=2000", "openh264enc bitrate=2000000")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t2 = threading.Thread(target=run_deepstream_pipeline)
    if simultaneously:
        t1.start()
        t2.start()
        t1.join()
        t2.join()
    else:
        t1.start()
        t1.join()
        t2.start()
        t2.join()
elif INTEL_GPU:
    print_intel_detected("GPU")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t1.start()
    t1.join()
elif NVIDIA_GPU:
    print_nvidia_detected()
    t1 = threading.Thread(target=run_deepstream_pipeline)
    t1.start()
    t1.join()
elif INTEL_NPU:
    print_intel_detected("NPU")
    replace_in_dlstreamer_pipeline("GPU", "NPU")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t1.start()
    t1.join()
elif INTEL_CPU:
    print_intel_detected("CPU")
    replace_in_dlstreamer_pipeline("GPU", "CPU")
    replace_in_dlstreamer_pipeline("vapostproc !", "")
    replace_in_dlstreamer_pipeline("pre-process-backend=va", "pre-process-backend=opencv")
    replace_in_dlstreamer_pipeline(r"video/x-raw\\\(memory:VAMemory\\\) !", "")
    replace_in_dlstreamer_pipeline("vah264enc bitrate=2000", "openh264enc bitrate=2000000")
    t1 = threading.Thread(target=run_dlstreamer_pipeline)
    t1.start()
    t1.join()

