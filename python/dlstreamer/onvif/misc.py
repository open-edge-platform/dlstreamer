# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Utility module - repository of helper functions, primarily for debug mode.

This module contains utility functions that support the main ONVIF camera
discovery sample application, mainly for debugging and diagnostic output.
"""
from typing import Any


def print_cameras(table_name: str, cameras: list[dict[str, Any]]) -> None:
    """Print the list of cameras in a tabular format."""
    if not cameras:
        print(f"No cameras found for {table_name}.")
        return

    # Collect all unique keys preserving insertion order
    headers = ["#"]
    for cam in cameras:
        for key in cam:
            if key not in headers:
                headers.append(key)

    rows = []
    for idx, cam in enumerate(cameras, 1):
        rows.append([str(idx)] + [str(cam.get(h, "-")) for h in headers[1:]])

    col_widths = [
        max(len(h), *(len(row[i]) for row in rows)) for i, h in enumerate(headers)
    ]

    sep = "+-" + "-+-".join("-" * w for w in col_widths) + "-+"
    header_line = (
        "| " + " | ".join(h.ljust(w) for h, w in zip(headers, col_widths)) + " |"
    )

    print(f"\n{table_name}:")
    print(sep)
    print(header_line)
    print(sep)
    for row in rows:
        print("| " + " | ".join(v.ljust(w) for v, w in zip(row, col_widths)) + " |")
    print(sep)
