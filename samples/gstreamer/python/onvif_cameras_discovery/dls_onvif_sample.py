# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Async ONVIF camera discovery sample application entry point."""
import argparse
import asyncio
import os
import sys
import dls_onvif_discovery_engine as dls_engine


async def main(cmd_line_params):
    """Main function to continuously discover ONVIF cameras."""

    engine = dls_engine.DlsOnvifDiscoveryEngine()

    engine.init_discovery(cmd_line_params)

    try:
        async for _ in engine.discover_cameras_iter():
            await asyncio.sleep(0)
    except asyncio.CancelledError:
        pass
    finally:
        await engine.release_resources_async()


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for ONVIF async discovery."""
    parser = argparse.ArgumentParser(
        description="ONVIF Camera Async Discovery and DL Streamer Pipeline Launcher"
    )
    parser.add_argument(
        "--username",
        type=str,
        default=os.environ.get("ONVIF_USER"),
        help="ONVIF camera username (or set ONVIF_USER env var)",
    )
    parser.add_argument(
        "--password",
        type=str,
        default=os.environ.get("ONVIF_PASSWORD"),
        help="ONVIF camera password (or set ONVIF_PASSWORD env var)",
    )
    parser.add_argument(
        "--refresh-rate",
        type=int,
        default=60,
        help="Seconds between discovery cycles (default: 60)",
    )
    parser.add_argument(
        "--config-file",
        type=str,
        default="config.json",
        help="Path to pipeline configuration JSON file (default: config.json)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print detailed profile information for discovered cameras",
    )

    parsed_arguments = parser.parse_args()

    if not parsed_arguments.username or not parsed_arguments.password:
        print(
            "Error: ONVIF username and password must be provided via command-line "
            "arguments or environment variables."
        )
        parser.print_help()
        sys.exit(1)

    return parsed_arguments


if __name__ == "__main__":
    parsed_args = parse_args()

    config = {
        "user": parsed_args.username,
        "password": parsed_args.password,
        "refresh_rate": parsed_args.refresh_rate,
        "config_file": parsed_args.config_file,
        "verbose": parsed_args.verbose,
    }

    try:
        asyncio.run(main(config))
    except KeyboardInterrupt:
        print("\nDiscovery stopped by user.")
