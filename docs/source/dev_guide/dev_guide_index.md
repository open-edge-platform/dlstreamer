# Developer Guide

- [Advanced Installation Guide](./advanced_install/advanced_install_guide_index.md)
  - [Ubuntu advanced installation - prerequisites](./advanced_install/advanced_install_guide_prerequisites.md)
  - [Advanced installation - compilation from source files](./advanced_install/advanced_install_guide_compilation.md)
  - [Ubuntu advanced installation - build Docker image](./advanced_install/advanced_build_docker_image.md)
  - [Ubuntu advanced uninstall guide](./advanced_install/advanced_uninstall_guide.md)
- [Metadata](./metadata.md)
- [Model Preparation](./model_preparation.md)
  - [1. Model file format used by OpenVINO™ Toolkit](./model_preparation.md#1-model-file-format-used-by-openvino-toolkit)
  - [2. Model pre- and post-processing](./model_preparation.md#2-model-pre--and-post-processing)
  - [3. Specify model files in GStreamer elements](./model_preparation.md#3-specify-model-files-in-gstreamer-elements)
- [OpenVINO Custom Operations](./openvino_custom_operations.md)
- [Model Info Section](./model_info_xml.md)
- [Python Bindings](./python_bindings.md)
  - [1. GStreamer Python bindings](./python_bindings.md#1-gstreamer-python-bindings)
  - [2. Video-analytics specific Python bindings](./python_bindings.md#2-video-analytics-specific-python-bindings)
  - [3. gvapython element](./python_bindings.md#3-gvapython-element)
  - [4. Performance considerations](./python_bindings.md#4-performance-considerations)
- [Custom GStreamer Plugin Installation](./custom_plugin_installation.md)
  - [1. Install custom GStreamer plugin(s)](./custom_plugin_installation.md#1-install-custom-gstreamer-plugins)
  - [2. Update GStreamer plugin(s) settings](./custom_plugin_installation.md#2-update-gstreamer-plugins-settings)
- [Custom Processing](./custom_processing.md)
  - [1. Consume tensor data and parse/convert it on application side](./custom_processing.md#1-consume-tensor-data-and-parseconvert-it-on-application-side)
  - [2. Set C/Python callback in the middle of GStreamer pipeline](./custom_processing.md#2-set-cpython-callback-in-the-middle-of-gstreamer-pipeline)
  - [3. Insert gvapython element and provide Python callback function](./custom_processing.md#3-insert-gvapython-element-and-provide-python-callback-function)
  - [4. Insert new GStreamer element implemented on C/C++](./custom_processing.md#4-insert-new-gstreamer-element-implemented-on-cc)
  - [5. Modify source code of post-processors for gvadetect/gvaclassify elements](./custom_processing.md#5-modify-source-code-of-post-processors-for-gvadetectgvaclassify-elements)
  - [6. Create custom post-processing library](./custom_processing.md#6-create-custom-post-processing-library)
- [Object Tracking](./object_tracking.md)
  - [Object tracking types](./object_tracking.md#object-tracking-types)
  - [Additional configuration](./object_tracking.md#additional-configuration)
  - [Sample](./object_tracking.md#sample)
  - [How to read object unique id](./object_tracking.md#how-to-read-object-unique-id)
  - [Performance considerations](./object_tracking.md#performance-considerations)
- [GPU device selection](./gpu_device_selection.md)
  - [1. Media (VAAPI based) elements](./gpu_device_selection.md#1-media-vaapi-based-elements)
  - [2. Inference (OpenVINO™ based) elements](./gpu_device_selection.md#2-inference-openvino-based-elements)
  - [3. Media and Inference elements for GStreamer 1.24.0 and later versions](./gpu_device_selection.md#3-media-and-inference-elements-for-gstreamer-1240-and-later-versions)
- [Performance Guide](./performance_guide.md)
  - [1. Media and AI processing (single stream)](./performance_guide.md#1-media-and-ai-processing-single-stream)
  - [2. Multi-stage pipeline with gvadetect and gvaclassify](./performance_guide.md#2-multi-stage-pipeline-with-gvadetect-and-gvaclassify)
  - [3. Multi-stream pipelines with single AI stage](./performance_guide.md#3-multi-stream-pipelines-with-single-ai-stage)
  - [4. Multi-stream pipelines with multiple AI stages](./performance_guide.md#4-multi-stream-pipelines-with-multiple-ai-stages)
  - [5. Multi-stream pipelines with meta-aggregation element](./performance_guide.md#5-multi-stream-pipelines-with-meta-aggregation-element)
  - [6. The Intel® DL Streamer Pipeline Framework performance benchmark results](./performance_guide.md#6-the-deep-learning-streamer-pipeline-framework-performance-benchmark-results)
- [Profiling with Intel VTune™](./profiling.md)
  - [1. Install VTune™](./profiling.md#1-install-vtune)
  - [2. Configure VTune™ host platform, Windows-to-Linux remote profiling method](./profiling.md#2-configure-vtune-host-platform-windows-to-linux-remote-profiling-method)
  - [3. Results Analysis](./profiling.md#3-results-analysis)
- [Converting NVIDIA DeepStream Pipelines to Intel® Deep Learning Streamer (Intel® DL Streamer) Pipeline Framework](./converting_deepstream_to_dlstreamer.md)
  - [Contents](./converting_deepstream_to_dlstreamer.md#contents)
  - [Preparing Your Model](./converting_deepstream_to_dlstreamer.md#preparing-your-model)
  - [Configuring Model for Intel® DL Streamer](./converting_deepstream_to_dlstreamer.md#configuring-model-for-deep-learning-streamer)
  - [GStreamer Pipeline Adjustments](./converting_deepstream_to_dlstreamer.md#gstreamer-pipeline-adjustments)
  - [Multiple Input Streams](./converting_deepstream_to_dlstreamer.md#multiple-input-streams)
  - [DeepStream to DL Streamer Elements Mapping Cheetsheet](./converting_deepstream_to_dlstreamer.md#deepstream-to-dlstreamer-elements-mapping-cheetsheet)
- [How to Contribute](./how_to_contribute.md)
  - [Coding Style](./coding_style.md)
- [Latency Tracer](./latency_tracer.md)
  - [Pipeline](./latency_tracer.md#pipeline)
  - [Pipeline latency flag](./latency_tracer.md#pipeline-latency-flag)
  - [Element latency flag](./latency_tracer.md#element-latency-flag)
  - [Setting interval example](./latency_tracer.md#setting-interval-example)
- [Model-proc File (legacy)](./model_proc_file.md)
  - [Contents](./model_proc_file.md#table-of-contents)
  - [Overview](./model_proc_file.md#model-proc-overview)
  - [Pre-processing description (`input_preproc`)](./model_proc_file.md#pre-processing-description)
  - [Post-processing description (`output_postproc`)](./model_proc_file.md#post-processing-description-output_postproc)
- [Pipeline Optimizer](./optimizer.md)

<!--hide_directive
:::{toctree}
:maxdepth: 2
:hidden:

advanced_install/advanced_install_guide_index
metadata
model_preparation
openvino_custom_operations
model_info_xml
python_bindings
custom_plugin_installation
custom_processing
object_tracking
gpu_device_selection
performance_guide
profiling
converting_deepstream_to_dlstreamer
how_to_contribute
latency_tracer
model_proc_file
optimizer
:::
hide_directive-->