# Prompt-based Object Detection

This sample searches a video file for user-defined objects using open vocabulary detection model.
This sample also demostrates how to integrate a third-party model with DLStreamer pipeline

> filesrc -> decodebin3 -> gvadetect -> appsink

The individual pipeline stages implement the following functions:

* __filesrc__ element reads video stream from a local file
* __decodebin3__ element decodes video stream into individual frames
* __gvadetect__ element runs an open-vocabulary AI detection model for each frame
* __autovideosink__ element executes user-defined processing of detection results

## How It Works

### STEP 1 - Model download and prompt configuration

First, the sample creates a PyTorch YOLOE model and configures it with a user-defined detection prompt: 

    ```code
    weights = "yoloe-26s-seg"
    model = YOLO(weights+".pt")
    names = [args[2]]
    model.set_classes(names, model.get_text_pe(names))
    ```

### STEP 2 - On-the-Fly model export 

The application exports a detection model to OpenVINO format for fast inference: 

    ```code
    exported_model_path = model.export(format="openvino", dynamic=True, half=True)
    model_file = f"{exported_model_path}/{weights}.xml"
    ```

### STEP 3 - DLStramer Pipeline Construction

Finally, the application creates a GStreamer `pipeline` object configuring it with the created  detection model and an input video file. 

    ```code
    pipeline = Gst.parse_launch(
            f"filesrc location={args[1]} ! decodebin3 ! "
            f"gvadetect model={model_file} device=GPU batch-size=4 ! queue ! "
            f"appsink emit-signals=true name=appsink0"
        )
    pipeline_loop(pipeline)
    ```code

Please note the application registers a user-defined callback to process prediction results from the pipeline. 

    ```code
    appsink = pipeline.get_by_name("appsink0")
    appsink.connect("new-sample", on_new_sample, None)
    ```code

The 'on_new_sample' callback prints out frame timestamps when a reqeusted object is found. 

## Running

The sample application requires a local input video file and a network connection to download an object detection model.
Here is an example command line to download assets and execute the sample application.

```sh
cd <python/prompted_detection directory>
wget https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4
python3 ./prompted_detection.py 1192116-sd_640_360_30fps.mp4 "white car"
```

The sample outputs detection results in the termial window.

## See also
* [Samples overview](../../README.md)
