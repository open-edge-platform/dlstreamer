# Advanced Installation On Ubuntu - Build Docker Image

> **NOTE:** The easiest way to run Deep Learning Streamer in Docker is to
> [pull docker images from DockerHub](../../get_started/install/install_guide_ubuntu.md#option-2-install-docker-image-from-docker-hub-and-run-it).

The instructions below are intended for building Docker images based on
Ubuntu22/24 from Deep Learning Streamer Dockerfiles.

## Step 1: Install prerequisites

Follow the instructions in
[the prerequisites](../../get_started/install/install_guide_ubuntu.md#prerequisites) section.

## Step 2: Download Dockerfiles

All Dockerfiles are in
[DLStreamer GitHub repository](https://github.com/open-edge-platform/dlstreamer/tree/master/docker).

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu24 debian/dev Dockerfile
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  wget https://raw.githubusercontent.com/open-edge-platform/dlstreamer/master/docker/ubuntu/ubuntu24.Dockerfile
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Ubuntu22 debian/dev Dockerfile
<!--hide_directive:sync: tab2hide_directive-->

  ```bash
  wget https://raw.githubusercontent.com/open-edge-platform/dlstreamer/master/docker/ubuntu/ubuntu22.Dockerfile
  ```

<!--hide_directive:::
::::hide_directive-->

## Step 3: Build Docker image

Build a Docker image from a Dockerfile, using the template command-line, as follows:

```bash
docker build -f <Dockerfile name> -t <name for Docker image> .
```

For example, you can build a Docker debian image from the **ubuntu22.Dockerfile**, naming it
**dlstreamer-ubuntu22**, and using the current directory as the build context.

```bash
docker build -f ubuntu22.Dockerfile -t dlstreamer-ubuntu22 .
```

You can build a Docker development image from the **ubuntu22.Dockerfile**, naming it
**dlstreamer-dev-ubuntu22**, and using the current directory as the build context:

```bash
docker build --target dlstreamer-dev -f ubuntu22.Dockerfile -t dlstreamer-dev-ubuntu22 .
```
