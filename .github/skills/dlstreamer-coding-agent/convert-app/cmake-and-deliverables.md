# Build System & Deliverables

Used during **implementation (step 5)** and produced as part of the output of
[`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

## Build Instructions

Generate `CMakeLists.txt` from the
[CMake Application Template](../assets/cmake-app-template.cmake) — copy the
file into the application directory and substitute `{{TARGET_NAME}}` with the
converted app's name. Remove sections marked `OPTIONAL` if the app does not
need them (e.g. OpenCV).

### CMake template adjustments (mandatory)

- **`Release/lib` link directory** — the template's `link_directories()` block
  is missing `${DLSTREAMER_INSTALL_PREFIX}/Release/lib`. On many DL Streamer
  installations, `libdlstreamer_gst_meta.so` and other runtime libraries live
  under `/opt/intel/dlstreamer/Release/lib/`, not `/opt/intel/dlstreamer/lib/`.
  Without this path, the linker fails with `cannot find -ldlstreamer_gst_meta`.
  **Add `${DLSTREAMER_INSTALL_PREFIX}/Release/lib`** to the
  `link_directories()` block.

- **OpenCV is REQUIRED when using `GVA::VideoFrame`** — the template declares
  `find_package(OpenCV OPTIONAL_COMPONENTS ...)`. However, the
  `dlstreamer/gst/videoanalytics/video_frame.h` header (which provides
  `GVA::VideoFrame`, `GVA::RegionOfInterest`, `GVA::Tensor`) transitively
  includes OpenCV headers. If the converted app uses any GVA C++ API for
  metadata access (which is the recommended pattern for tensor data
  extraction), OpenCV MUST be changed from `OPTIONAL` to `REQUIRED`:

  ```cmake
  find_package(OpenCV REQUIRED COMPONENTS core imgproc)
  ```

  Without this, compilation fails with missing `cv::Mat` or
  `opencv2/core.hpp` errors.

For a real-world example of the same pattern in production use, see
[`samples/gstreamer/cpp/draw_face_attributes/CMakeLists.txt`](../../../samples/gstreamer/cpp/draw_face_attributes/CMakeLists.txt).

## Deliverables

The converted application directory MUST contain:

### 1. `CMakeLists.txt`

Generated from the template above with the mandatory adjustments applied.

### 2. Source files (`.cpp` / `.h`)

The converted C++ application. Reference implementation to study first:
[`samples/gstreamer/cpp/draw_face_attributes/`](../../../samples/gstreamer/cpp/draw_face_attributes/)
— a working example of a native DL Streamer / OpenVINO C++ application that
defines the expected code structure, `CMakeLists.txt` layout, and coding
conventions to follow.

### 3. `README.md`

See [`documentation-spec.md`](./documentation-spec.md) for the full
specification (required sections, Pipeline Comparison diagram spec, Conversion
Notes tables, traceability comments).

### 4. `run.sh`

A thin wrapper that invokes the built binary with sensible default arguments
so the user can run the app with a single command (`./run.sh`). The script
MUST:

- **Check that `build/<app_name>` exists**; if not, instruct the user to build
  first.
- **Use environment variables for overridable inputs** (`INPUT`, `MODEL`,
  `DEVICE`) with sensible defaults. Do **not** reuse env var names owned by
  `setup_dls_env.sh` (notably `MODELS_PATH` — use a unique app-specific name
  of the form `<APP>_MODELS_PATH`, e.g. `LPR_MODELS_PATH` for the LPR app,
  `PEOPLE_MODELS_PATH` for a people-detection app, etc.). See
  [`runsh-pitfalls.md`](./runsh-pitfalls.md).
- **Forward extra CLI args** (`"$@"`) to the binary so power users can override
  anything.

- **Detect whether a display is available** — check `$DISPLAY` and
  `$WAYLAND_DISPLAY` (and on Windows, the equivalent presence of an
  interactive desktop session). If neither is set (headless / SSH session /
  CI), the script MUST automatically force the application into file-output /
  no-display mode (e.g. add `--no-display` or set the equivalent env var) and
  print a one-line notice to the user explaining that the display was not
  found and the output is being written to the file sink instead. The user
  must always be able to override this auto-detection by exporting `DISPLAY`
  (or by passing an explicit flag through `"$@"`).

- **Provide a `--help` / `-h` flag** that prints, on stdout and with exit
  status `0`:
  - A one-line synopsis
    (`Usage: ./run.sh [--help] [--sink display|file|fake] [extra binary flags...]`)
  - The list of supported environment variables (`INPUT`, `DEVICE`, `OUTPUT`,
    `<APP>_MODELS_PATH`, plus any app-specific ones) with defaults and meanings.
  - The list of wrapper-level flags (at minimum `--help`, `--sink`) with
    examples.
  - A pointer telling the user that any further flag is forwarded to the
    underlying binary, and that `./build/<app_name> --help` lists every
    binary-level flag.

- **Support a `--sink` selector** (or equivalent env var,
  e.g. `SINK=display|file|fake`) that switches the output backend among three
  modes:
  1. `display` — render to an on-screen window (e.g. `autovideosink` /
     `gvawatermark` + display sink). Subject to headless auto-detection.
  2. `file` — encode and write to a file (`OUTPUT` path), no window.
  3. `fake` — discard frames via `fakesink` (useful for benchmarking and CI;
     produces no file and no window).

  The default mode is `display` when a display is present, otherwise `file`.
  The script MUST validate the chosen value and fail fast with a clear error
  message on an unknown sink. Document each mode and its default in the
  README's `Run` and `Command-Line Arguments` sections.

See [`runsh-pitfalls.md`](./runsh-pitfalls.md) for the full list of bugs to
prevent (env var clobbering, plugin path, kmssink, audio track stream
selection, GPU↔CPU transfer, transparent watermark, encoder fallback, etc.).

### 5. `export_models.sh` (if applicable)

When models must be downloaded/converted from upstream sources, ship an
executable script that performs the download + conversion idempotently. The
script MUST:

- Skip work when the target files already exist.
- Print clear progress messages.
- Exit non-zero on any failure.
- Be invoked automatically by `run.sh` (or surface a clear error pointing the
  user to it) when the model files are missing at the default path.

## Scope reminder

Convert **only** the inference and media pipeline logic. **Exclude**:

- Model training or fine-tuning code.
- GUI / visualization frameworks not available in DL Streamer (Qt, OpenCV
  HighGUI windows, etc. — replace with `gvawatermark + autovideosink` or file
  sink).
- Cloud-specific APIs (AWS, GCP, Azure SDKs).
- CUDA-specific extensions with no OpenVINO equivalent (flag these explicitly
  in the documentation).
