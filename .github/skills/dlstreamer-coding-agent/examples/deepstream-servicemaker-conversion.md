Convert the NVIDIA DeepStream sample application (https://github.com/NVIDIA-AI-IOT/deepstream_reference_apps/tree/master/pyservicemaker_sample_apps/pipeline_api/deepstream_nvdsanalytics_test_app) to an equivalent Python sample for DL Streamer, optimized for Intel Core Ultra processors.

Match the original application's functionality and scale. Do not add features, CLI arguments, or error handling absent from the original.

Include in the README:
- Pipeline diagrams comparing DeepStream and DL Streamer architectures
- Element mapping table with rationale for each substitution
- Model comparison table: original vs. replacement, format conversion steps, rationale

Test with this traffic video (1920×1080, 25 fps): https://videos.pexels.com/video-files/2431853/2431853-hd_1920_1080_25fps.mp4.
Validate with a single-stream configuration using 4 rectangular zones covering the full frame. For each zone, overlay the live car count on the video. Save the output to a file in results/.
