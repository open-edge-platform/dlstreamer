#!/usr/bin/env bash
# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
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

  echo "Applying patch: $(basename "$patch_path") (idempotent)..."
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

# ExternalProject re-runs the patch step after every update step, and
# `patch --forward` cannot detect a patch whose hunks a later patch rewrote
# (e.g. 0021 then 0023), so it gets applied again with fuzz. Record the applied
# patch set in the checkout's git dir; skip if unchanged, else start clean.
stamp=""
if git_dir="$(git rev-parse --absolute-git-dir 2>/dev/null)"; then
  stamp="${git_dir}/dlstreamer-patches.sha256"
  patch_set_hash="$(cat "$@" | sha256sum | cut -d' ' -f1)"
  if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$patch_set_hash" ]; then
    echo "Patch set unchanged since last apply, skipping."
    exit 0
  fi
  echo "Resetting source tree before applying patches..."
  git reset --hard --quiet
  git clean -fdq
  rm -f "$stamp"
fi

overall_rc=0
for p in "$@"; do
  if ! apply_one "$p"; then
    overall_rc=1
  fi
done

if [ "$overall_rc" -eq 0 ] && [ -n "$stamp" ]; then
  echo "$patch_set_hash" > "$stamp"
fi

exit $overall_rc