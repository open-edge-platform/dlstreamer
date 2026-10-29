# Python Environment

This page describes how DL Streamer manages its Python dependencies on Linux
and which Python environment to use for what.

## Two kinds of Python environments

| | DL Streamer Python environment | Tool environments |
|---|---|---|
| Used for | everything that imports `gi` or runs inside a pipeline: Python elements (`*_py`), `gvapython`, Python samples, the pipeline optimizer, `dlstreamer.onvif` | model download / export tools (`scripts/download_models`, `optimum-cli`, Ultralytics export), which bring PyTorch, Transformers, etc. |
| Interpreter | **always the system `python3`** DL Streamer was built against | any |
| Created by | `scripts/setup_python_env.sh` (Docker images: preinstalled) | a separate virtual environment per tool |

The DL Streamer Python environment must use the system `python3` because
`libgstpython.so` (GStreamer Python elements) and `libgvapython.so` are linked
against its `libpython`, and Python elements run in an interpreter embedded in
GStreamer. That interpreter uses the environment of the **first `python3`
found on `PATH`**, so keep the DL Streamer Python environment activated when
running pipelines, and deactivate tool environments after using them.

DL Streamer itself does not use the OpenVINO™ Python bindings: Python tools
list inference devices with the `dls_devices` tool installed in
`<install-dir>/bin`, which queries the same OpenVINO™ runtime the DL Streamer
elements use.

## Setting up the DL Streamer Python environment

```bash
/opt/intel/dlstreamer/scripts/setup_python_env.sh --venv ~/python3venv --dlstreamer-dir /opt/intel/dlstreamer
source ~/python3venv/bin/activate
```

The script:

1. creates the virtual environment on top of the system `python3`,
2. installs the requirement groups from `requirements/` (`runtime`, `onvif`
   and `optimizer` by default, select others with `--groups`),
3. writes a `dlstreamer.pth` file that makes `gstgva`, `dlstreamer.onvif` and
   the GStreamer Python overrides importable, so `PYTHONPATH` is not needed,
4. checks that the interpreter matches the one `libgstpython.so` and
   `libgvapython.so` were built against.

Run `setup_python_env.sh --check` to re-run the checks for the `python3` on
`PATH`. The `.deb`/`.rpm` packages install an `intel-dlstreamer.pth` file for
the system `python3`, and the Docker images install the Python dependencies
into the system `python3`, so no virtual environment is needed there.

Samples with additional dependencies create their environment with the same
script and install their own `requirements.txt` on top of it.

## Where versions are defined

| File | Content |
|---|---|
| `dependencies/versions.env` | Single source of truth for the GStreamer and OpenVINO™ versions |
| `requirements/runtime.txt` | DL Streamer Python runtime (NumPy, OpenCV, PyGObject, ...) |
| `requirements/onvif.txt` | `dlstreamer.onvif` dependencies |
| `requirements/optimizer.txt` | Pipeline optimizer dependencies |
| `requirements/build.txt` | Python build tools (`meson`, `ninja`) |
| `requirements/test.txt` | Test dependencies |
| `requirements/constraints.txt` | OpenVINO™ wheel pins, generated from `versions.env`; used with `-c` by `scripts/download_models/requirements_*.txt` |
| `requirements.txt` | Aggregate of `runtime` and `optimizer`, kept for compatibility |
| `python/pyproject.toml` | The `intel-dlstreamer` wheel (`gstgva`, `dlstreamer.onvif`) for Python environments not created with `setup_python_env.sh` |

Files that cannot read `versions.env` (Dockerfiles, CI workflows, docs,
self-contained sample requirements) keep a copy of the version.
`scripts/check_versions.py` verifies all copies in CI; after changing
`versions.env` run:

```bash
python3 scripts/check_versions.py --fix
```
