#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/bin/bash
set -e

SOURCE="${1:-/sources}"
SOURCE=$(realpath --relative-to=. "$SOURCE" | sed 's:/*$::')

STYLED_DIR="./_styled"
OUTPUT_DIR="./_output"
mkdir -p "$STYLED_DIR" "$OUTPUT_DIR"

mkdir -p "$(dirname "$STYLED_DIR/$SOURCE")"
cp -R "$SOURCE" "$STYLED_DIR/$SOURCE"

find "$STYLED_DIR/$SOURCE" \
  \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' \) \
  -exec sh -c "clang-format style=file -i '{}' 2>&1 | sed '/No such file or directory/d'" \;

output=$(diff -u --recursive "$SOURCE" "$STYLED_DIR/$SOURCE" || true)

if [[ -n "$output" ]]; then
    "${DIFF2HTML_BIN:-diff2html}" -F "$OUTPUT_DIR/diff.html" -d word -s "side" -i stdin <<< "$output"
    sed -i '37d;38i<h1>Code style diff</h1>' "$OUTPUT_DIR/diff.html"
    echo "❌ There are problems with code styles"
    exit 1
else
    echo "✅ Code styles are fine"
    exit 0
fi
