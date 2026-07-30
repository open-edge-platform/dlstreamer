# Uninstall Guide Ubuntu

## Option #1: Uninstall Deep Learning Streamer Pipeline Framework from APT repository
=======

If you installed via APT, just use its uninstall command:

```bash
sudo apt remove intel-dlstreamer
```

If you want to remove OpenVINO™ as well, please use the following
commands:

```bash
sudo apt remove -y openvino* libopenvino-* python3-openvino*
sudo apt-get autoremove
```

## Option #2: Remove Deep Learning Streamer Pipeline Framework Docker image
=======

If you used docker, you only need to remove the appropriate container and dlstreamer docker
image:

```bash
docker rm <container-name>
docker rmi dlstreamer:devel
```

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
