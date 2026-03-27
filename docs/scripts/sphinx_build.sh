#!/bin/bash
# ==============================================================================
# Copyright (C) 2023-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

BUILD_TYPES=${1:-"html,linkcheck,spelling"}
SOURCE_DIR=${2:-"./user-guide"}

echo "Build types: $BUILD_TYPES"

EXIT_CODE=0
SUMMARY=()
IFS=',' read -ra BUILDS <<< "$BUILD_TYPES"
for btype in "${BUILDS[@]}"; do
  echo "::group::Build ${btype}"

  /usr/local/bin/sphinx-build -b "${btype}" "${SOURCE_DIR}" ./build-"${btype}"
  ec=$?

  SUMMARY+=("Build '${btype}' exit code: $ec")
  if [ $ec -ne 0 ]; then
    SUMMARY+=(" [FAILED]")
    EXIT_CODE=1
    echo "::error::Build type '$btype' has failed (exit code $ec)"
  else
    SUMMARY+=(" [OK]")
  fi
  SUMMARY+=("\n")

  echo "::endgroup::"
done

echo "::group::Sphinx build summary"
printf '%s' "${SUMMARY[*]}"
echo ""
echo "Final exit code: $EXIT_CODE"
echo "::endgroup::"

exit $EXIT_CODE
