# GVAMETAPUBLISH

A GStreamer element to publish JSON data to a designated file, or a chosen message broker:

1. File (default)

2. MQTT broker

3. Kafka broker

## Build components

1. Build libraries either through docker or on host machine

   The Docker image built with the Dockerfile includes all necessary dependencies for Kafka/MQTT by default.
   If you are building from source according to the provided instructions, all dependencies should already be satisfied.
   You can find the source build instructions [here](../../../../../docs/source/dev_guide/advanced_install/advanced_install_guide_compilation.md).

   If you are not following the source instructions, you may need to run the [install_metapublish_dependencies.sh](https://github.com/open-edge-platform/dlstreamer/tree/master/scripts/install_metapublish_dependencies.sh) script and rebuild DL Streamer with the following parameters enabled:

   ```bash
   -DENABLE_PAHO_INSTALLATION=ON \
   -DENABLE_RDKAFKA_INSTALLATION=ON \
   ```

2. Run [metapublish](https://github.com/open-edge-platform/dlstreamer/tree/master/samples/gstreamer/gst_launch/metapublish/metapublish.sh) sample to test

3. Create your own pipeline and add gvametapublish element with the following parameters:

   ```bash
   gvametapublish method=file filepath=/root/video-example/detections_2019.json
   ```

   - Optionally provide file-format=json-lines to have raw JSON inferences written to the file, or file-format=json (default) to have the file populated as an array of JSON inferences:

     ```bash
     gvametapublish method=file filepath="/root/video-examples/detections_2019.json" file-format=json-lines
     ```

   - To publish data to mqtt broker:

     ```bash
     gvametapublish method=mqtt address=127.0.0.1:1883 mqtt-client-id=clientIdValue topic=topicName
     ```

   - To publish data to kafka broker:

     ```bash
     gvametapublish method=kafka address=127.0.0.1:9092 topic=topicName
     ```

Note: \*method is a required property of gvametapublish element.
