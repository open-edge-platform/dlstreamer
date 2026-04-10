# Building the Wheel Package for onvif-cameras-discovery sample

Python wheel (`.whl`) for the ONVIF Camera Discovery sample.

## Prerequisites

- Python 3.10+
- `pip`, `setuptools`, `wheel` (included in most Python installations)
- System packages for GStreamer Python bindings:

```bash
sudo apt install python3-gi gir1.2-gst-1.0
```

## 1. Build

Remove old artifacts
```bash
cd build_whl
rm -rf dist build *.egg-info
```
Build package:
```
pip wheel --no-deps -w dist .
```

The generated `.whl` file appears in `dist/`:

```
dist/onvif_cameras_discovery-0.1.0-py3-none-any.whl
```

### Alternative — using build script

```bash
./build_dls_onvif_sample_whl.sh
```

## 2. Create virtual environment

GStreamer bindings (`gi`) are system packages — use `--system-site-packages`
to make them available inside the venv:

```bash
python3 -m venv --system-site-packages /tmp/onvif_whl_env
source /tmp/onvif_whl_env/bin/activate
```

## 3. Install

```bash
pip install dist/onvif_cameras_discovery-0.1.0-py3-none-any.whl
```

Verify installation:

```bash
pip show onvif-cameras-discovery
```

Expected output includes:

```
Name: onvif-cameras-discovery
Version: 0.1.0
License: MIT
Author: Intel Corporation
```

## 4. Verify modules

Run from a directory **other than** the project source (to confirm the wheel
is used, not local `.py` files):

```bash
cd /tmp
python3 -c "
import dls_onvif_camera_entry
import dls_onvif_config_manager
import dls_onvif_data
import dls_onvif_discovery_thread
import dls_onvif_discovery_engine
import dls_onvif_sample
import misc
print('All modules imported OK')
"
```

Check that modules are loaded from `site-packages`:

```bash
python3 -c "
import dls_onvif_camera_entry
print(dls_onvif_camera_entry.__file__)
"
# Expected: /tmp/onvif_whl_env/lib/python3.x/site-packages/dls_onvif_camera_entry.py
```

## 5. Uninstall

```bash
pip uninstall onvif-cameras-discovery
```

## 6. Remove virtual environment

```bash
deactivate
rm -rf /tmp/onvif_whl_env
```

## Included modules

| Module | Description |
|--------|-------------|
| `dls_onvif_camera_entry` | Camera + pipeline registry (`DlsOnvifCameraEntry`, `DlsOnvifCameraRegistry`) |
| `dls_onvif_config_manager` | Pipeline configuration loader from `config.json` |
| `dls_onvif_data` | ONVIF profile data structure (`ONVIFProfile`) |
| `dls_onvif_discovery_thread` | GStreamer pipeline lifecycle manager |
| `dls_onvif_discovery_engine` | WS-Discovery, ONVIF profiles, async orchestrator |
| `dls_onvif_sample` | Async discovery sample entry point |
| `misc` | Console output helpers |

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `ModuleNotFoundError: No module named 'gi'` | Recreate venv with `--system-site-packages` |
| `No matching distribution found for urllib` | Use `urllib3` in `requirements.txt`, not `urllib` |
| `-bash: .../bin/python3: No such file or directory` | Run `deactivate; hash -r` before recreating venv |
| Modules loaded from project dir instead of wheel | Run test from `/tmp`, not from the source directory |

---

[Back to ONVIF Camera Discovery Sample](../README.md)
