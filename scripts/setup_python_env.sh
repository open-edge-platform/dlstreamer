#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Sets up the DL Streamer Python environment on Linux:
#   1. installs the Python requirements (requirements/*.txt) into a virtual
#      environment or into the system interpreter,
#   2. makes DL Streamer's own Python modules (gstgva, dlstreamer.onvif) and the
#      GStreamer Python overrides importable via a "dlstreamer.pth" file, so no
#      PYTHONPATH is needed,
#   3. checks that the interpreter matches the one DL Streamer's Python plugins
#      (libgstpython.so, libgvapython.so) were built against.
#
# The interpreter must be the system python3 DL Streamer was built with: Python
# elements run inside an interpreter embedded in GStreamer, which picks the
# environment of the first "python3" found on PATH. Activate the virtual
# environment (or put its bin/ directory first on PATH) before running pipelines.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: $(basename "$0") (--venv DIR | --system) [options]
       $(basename "$0") --check [--venv DIR] [options]

Targets:
  --venv DIR            Create (if needed) and use the virtual environment DIR.
  --system              Install into the system interpreter (containers only).

Options:
  --python PATH         Base interpreter (default: system python3 from PATH).
  --dlstreamer-dir DIR  DL Streamer installation or source directory
                        (default: $(cd "$SCRIPT_DIR/.." && pwd)).
  --gstreamer-dir DIR   GStreamer installation directory (default: autodetected:
                        <dlstreamer-dir>/gstreamer, <dlstreamer-dir>/build/deps/gstreamer-bin).
  --groups LIST         Comma separated requirement groups from requirements/
                        (default: runtime,onvif,optimizer; also: build, test).
  --extra-path DIR      Additional directory to add to the .pth file (repeatable).
  --no-deps             Do not install dependencies of the listed packages.
  --no-pth              Do not write the dlstreamer.pth file.
  --no-install          Only write the .pth file (and run the checks).
  --check               Only run the checks.
  -h, --help            Show this help.

Examples:
  # Native installation: create ~/python3venv and activate it
  $(basename "$0") --venv ~/python3venv && source ~/python3venv/bin/activate

  # Docker image: install into the system interpreter
  $(basename "$0") --system --dlstreamer-dir /opt/intel/dlstreamer
EOF
}

TARGET=""
VENV_DIR=""
BASE_PYTHON=""
DLS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GST_DIR=""
GROUPS_LIST="runtime,onvif,optimizer"
EXTRA_PATHS=()
NO_DEPS=0
WRITE_PTH=1
INSTALL=1
CHECK_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --venv) TARGET=venv; VENV_DIR="$2"; shift 2 ;;
        --system) TARGET=system; shift ;;
        --python) BASE_PYTHON="$2"; shift 2 ;;
        --dlstreamer-dir) DLS_DIR="$2"; shift 2 ;;
        --gstreamer-dir) GST_DIR="$2"; shift 2 ;;
        --groups) GROUPS_LIST="$2"; shift 2 ;;
        --extra-path) EXTRA_PATHS+=("$2"); shift 2 ;;
        --no-deps) NO_DEPS=1; shift ;;
        --no-pth) WRITE_PTH=0; shift ;;
        --no-install) INSTALL=0; shift ;;
        --check) CHECK_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

info() { echo "[setup_python_env] $*"; }
fail() { echo "[setup_python_env] ERROR: $*" >&2; exit 1; }

if [[ -z "$TARGET" && $CHECK_ONLY -eq 0 ]]; then
    usage >&2
    exit 2
fi

DLS_DIR="$(cd "$DLS_DIR" && pwd)"
REQ_DIR="$DLS_DIR/requirements"

if [[ -z "$GST_DIR" ]]; then
    for candidate in "$DLS_DIR/gstreamer" "$DLS_DIR/build/deps/gstreamer-bin"; do
        if [[ -d "$candidate" ]]; then
            GST_DIR="$candidate"
            break
        fi
    done
fi

# The base interpreter is the system python3, also when PATH points to an
# active virtual environment.
if [[ -z "$BASE_PYTHON" ]]; then
    for candidate in /usr/bin/python3 "$(command -v python3 || true)"; do
        if [[ -x "$candidate" ]]; then
            BASE_PYTHON="$candidate"
            break
        fi
    done
fi
[[ -n "$BASE_PYTHON" ]] || fail "python3 not found"

if [[ "$TARGET" == "venv" || ( $CHECK_ONLY -eq 1 && -n "$VENV_DIR" ) ]]; then
    VENV_DIR="$(mkdir -p "$VENV_DIR" && cd "$VENV_DIR" && pwd)"
    PYTHON="$VENV_DIR/bin/python"
elif [[ $CHECK_ONLY -eq 1 ]]; then
    # The embedded interpreter uses the first python3 on PATH, so check that one.
    PYTHON="$(command -v python3 || echo "$BASE_PYTHON")"
else
    PYTHON="$BASE_PYTHON"
fi

create_venv() {
    if [[ -x "$PYTHON" ]]; then
        info "Using existing virtual environment $VENV_DIR"
        return
    fi
    info "Creating virtual environment $VENV_DIR (base interpreter: $BASE_PYTHON)"
    if command -v uv >/dev/null 2>&1; then
        UV_PYTHON_PREFERENCE=only-system uv venv --python "$BASE_PYTHON" "$VENV_DIR"
    else
        "$BASE_PYTHON" -m venv "$VENV_DIR"
    fi
}

install_requirements() {
    local args=() group
    IFS=',' read -r -a groups <<< "$GROUPS_LIST"
    for group in "${groups[@]}"; do
        [[ -f "$REQ_DIR/$group.txt" ]] || fail "unknown requirement group '$group' ($REQ_DIR/$group.txt)"
        args+=(-r "$REQ_DIR/$group.txt")
    done
    args+=(-c "$REQ_DIR/constraints.txt")
    [[ $NO_DEPS -eq 1 ]] && args+=(--no-deps)

    info "Installing requirement groups: $GROUPS_LIST"
    local target_args=()
    if [[ "$TARGET" == "system" ]] && "$PYTHON" -c 'import os, sysconfig, sys; sys.exit(not os.path.exists(os.path.join(sysconfig.get_path("stdlib"), "EXTERNALLY-MANAGED")))'; then
        # PEP 668 marked interpreter (Ubuntu 24+); older pip versions (e.g. pip 22
        # on Ubuntu 22.04) do not know this option, so only pass it when needed.
        target_args+=(--break-system-packages)
    fi
    if command -v uv >/dev/null 2>&1; then
        target_args+=(--python "$PYTHON")
        [[ "$TARGET" == "system" ]] && target_args+=(--system)
        uv pip install --no-cache-dir "${target_args[@]}" "${args[@]}"
    else
        "$PYTHON" -m pip install --no-cache-dir "${target_args[@]}" "${args[@]}"
    fi
}

# onvif-zeep installs its WSDL files as data files under
# <data>/lib/pythonX.Y/site-packages/wsdl, but looks for them next to the
# installed package. On Debian/Ubuntu these differ for the system interpreter
# (site-packages vs dist-packages), so copy them next to the package.
fix_onvif_wsdl() {
    "$PYTHON" - <<'EOF'
import os, shutil, sys, sysconfig
paths = sysconfig.get_paths()
src = os.path.join(paths["data"], "lib", f"python{sys.version_info[0]}.{sys.version_info[1]}",
                   "site-packages", "wsdl")
dst = os.path.join(paths["purelib"], "wsdl")
if os.path.isdir(src) and not os.path.isdir(dst):
    shutil.copytree(src, dst)
    print(f"[setup_python_env] Copied onvif-zeep WSDL files to {dst}")
EOF
}

write_pth() {
    local site_packages pth
    site_packages="$("$PYTHON" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')"
    pth="$site_packages/dlstreamer.pth"
    mkdir -p "$site_packages"
    {
        echo "$DLS_DIR/python"
        if [[ -n "$GST_DIR" && -d "$GST_DIR/lib/python3/dist-packages" ]]; then
            echo "$GST_DIR/lib/python3/dist-packages"
        fi
        for path in "${EXTRA_PATHS[@]}"; do
            echo "$path"
        done
    } > "$pth"
    info "Wrote $pth:"
    sed 's/^/    /' "$pth"
}

python_lib_of() {
    ldd "$1" 2>/dev/null | grep -o 'libpython[0-9.]*\.so' | head -1 || true
}

check() {
    local status=0 version expected plugin linked
    version="$("$PYTHON" -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
    expected="libpython${version}.so"
    info "Interpreter: $PYTHON (Python $version)"

    for plugin in "$GST_DIR/lib/gstreamer-1.0/libgstpython.so" \
                  "$DLS_DIR/lib/libgvapython.so" \
                  "$DLS_DIR"/*/lib/libgvapython.so \
                  "$DLS_DIR"/build/intel64/*/lib/libgvapython.so; do
        [[ -f "$plugin" ]] || continue
        linked="$(python_lib_of "$plugin")"
        if [[ -n "$linked" && "$linked" != "$expected" ]]; then
            echo "[setup_python_env] ERROR: $plugin is linked with $linked, but the interpreter is Python $version." >&2
            echo "[setup_python_env]        Use the interpreter DL Streamer was built with (--python)." >&2
            status=1
        else
            info "OK: $(basename "$plugin") -> ${linked:-no libpython}"
        fi
    done

    if "$PYTHON" - <<'EOF'
import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst
Gst.init(None)
import gstgva  # noqa: F401
EOF
    then
        info "OK: gi, Gst and gstgva import"
    else
        echo "[setup_python_env] WARNING: importing gi/Gst/gstgva failed. Make sure the DL Streamer" >&2
        echo "[setup_python_env]          environment (GI_TYPELIB_PATH, LD_LIBRARY_PATH) is set," >&2
        echo "[setup_python_env]          e.g. source $DLS_DIR/scripts/setup_dls_env.sh" >&2
    fi
    return $status
}

if [[ $CHECK_ONLY -eq 1 ]]; then
    check
    exit $?
fi

[[ "$TARGET" == "venv" ]] && create_venv
if [[ $INSTALL -eq 1 ]]; then
    install_requirements
    [[ ",$GROUPS_LIST," == *",onvif,"* ]] && fix_onvif_wsdl
fi
[[ $WRITE_PTH -eq 1 ]] && write_pth
check || exit 1

if [[ "$TARGET" == "venv" ]]; then
    info "Done. Activate the environment before running DL Streamer Python code:"
    info "    source $VENV_DIR/bin/activate"
fi
