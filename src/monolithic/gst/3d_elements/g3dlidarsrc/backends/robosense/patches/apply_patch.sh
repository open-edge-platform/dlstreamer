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

  # 'patch' returns rc=1 both for "already applied" and for "some hunks failed",
  # so the exit code alone cannot distinguish an idempotent skip from a genuine
  # failure. Detect "already applied" explicitly by checking whether the patch
  # applies cleanly in reverse (--dry-run so nothing is modified). If so, skip.
  if patch --reverse --batch --dry-run -p1 < "$patch_path" >/dev/null 2>&1; then
    echo "  -> already applied (skipped)."
    return 0
  fi

  # Not applied yet: apply forward. Any failure here (missing files, failed
  # hunks) is a real error and must not be masked as an idempotent skip.
  if patch --forward --batch -p1 < "$patch_path"; then
    echo "  -> applied."
    return 0
  else
    local rc=$?
    echo "  -> failed to apply cleanly (rc=$rc)" >&2
    return "$rc"
  fi
}

overall_rc=0
for p in "$@"; do
  if ! apply_one "$p"; then
    overall_rc=1
  fi
done

exit $overall_rc
