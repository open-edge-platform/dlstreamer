#!/usr/bin/env bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Apply rs_driver patches idempotently. Mirrors dependencies/patches/
# apply_gst_patch.sh in behavior so that re-running CMake configure does not
# re-fail on already-applied patches.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ "$#" -eq 0 ]; then
  echo "Usage: $0 <patch-file> [patch-file ...]" >&2
  echo "Each patch file may be absolute or relative to script directory." >&2
  exit 2
fi

apply_one() {
  local patch_path="$1"

  # Resolve relative path to script dir if not absolute and not existing
  if [ ! -f "$patch_path" ]; then
    patch_path="${SCRIPT_DIR}/${patch_path}"
  fi
  if [ ! -f "$patch_path" ]; then
    echo "Patch file not found: $patch_path" >&2
    return 3
  fi

  echo "Applying rs_driver patch: $(basename "$patch_path") (idempotent)..."
  set +e
  patch --forward --batch -p1 < "$patch_path"
  local rc=$?
  set -e

  case $rc in
    0) echo "  -> applied." ;;
    1) echo "  -> already applied (skipped)." ;;
    *) echo "  -> failed rc=$rc" >&2; return $rc ;;
  esac
  return 0
}

overall_rc=0
for p in "$@"; do
  if ! apply_one "$p"; then
    overall_rc=1
  fi
done

exit $overall_rc
