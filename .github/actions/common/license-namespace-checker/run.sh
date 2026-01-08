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

pushd "${SOURCE_DIR}"

result=0
for file in "$@"; do  # Iterate over all files passed as arguments
    if [ ! -f "${file}" ]; then
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
