#!/bin/bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Usage: $0 SOURCE_DIR [FILES...]
# This script is used to run rules-checker.py

SOURCE_DIR=${1}
shift  # Remove SOURCE_DIR from the list of arguments, leaving only files
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

if [ -z "${SOURCE_DIR}" ]; then
    echo "ERROR: Path to source dir should be provided!"
    exit 1
fi

git config --global --add safe.directory "${SOURCE_DIR}"

# Third-party vendored files with their own (non-Intel) copyright headers.
# These contain changes from GStreamer 1.28 and 1.30 vendored temporarily.
# They will no longer be needed after upgrading to GStreamer >= 1.30.
THIRD_PARTY_FILES=(
    "include/dlstreamer/gst/metadata/gstanalyticsgroupmtd.h"
    "include/dlstreamer/gst/metadata/gstanalyticskeypointmtd.h"
    "src/gst/metadata/gstanalyticsgroupmtd.c"
    "src/gst/metadata/gstanalyticskeypointmtd.c"
)

pushd "${SOURCE_DIR}"

result=0
for file in "$@"; do  # Iterate over all files passed as arguments
    if [ ! -f "${file}" ]; then
        continue
    fi

    # Skip third-party vendored files
    skip=0
    for tp_file in "${THIRD_PARTY_FILES[@]}"; do
        if [ "${file}" = "${tp_file}" ]; then
            echo "Skipping third-party file: ${file}"
            skip=1
            break
        fi
    done
    if [ ${skip} -eq 1 ]; then
        continue
    fi
    
    commit_time=$(git log --reverse --diff-filter=A --format="%ct" -- "${file}")
    if [ ! -z "${commit_time}" ]; then
        python3 "${SCRIPT_DIR}/rules-checker.py" "${file}"
        result=$(( result || $? ))
    fi
done

popd

if [ ${result} != 0 ]; then
    echo "ERROR: Found problems!"
else
    echo "OK!"
fi

exit ${result}
