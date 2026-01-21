# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import argparse
import logging

from optimizer import get_optimized_pipeline

parser = argparse.ArgumentParser(
    prog="DLStreamer Pipeline Optimization Tool",
    description="Use this tool to try and find versions of your pipeline that will run with increased performance." # pylint: disable=line-too-long
)
parser.add_argument("--search-duration", default=300,
                    help="Duration in seconds of time which should be spent searching for optimized pipelines (default: %(default)s)") # pylint: disable=line-too-long
parser.add_argument("--sample-duration", default=10,
                    help="Duration in seconds of sampling individual pipelines. Longer duration should offer more stable results (default: %(default)s)") # pylint: disable=line-too-long
parser.add_argument("--log-level", default="INFO", choices=["CRITICAL", "FATAL", "ERROR" ,"WARN", "INFO", "DEBUG"], # pylint: disable=line-too-long
                    help="Minimum used log level (default: %(default)s)")
parser.add_argument("pipeline", nargs="+",
                    help="Pipeline to be analyzed")
args=parser.parse_args()

logging.basicConfig(level=args.log_level, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)

pipeline = " ".join(args.pipeline)

try:
    best_pipeline, best_fps = get_optimized_pipeline(pipeline,
                                                     float(args.search_duration),
                                                     float(args.sample_duration))
    logger.info("\nBest found pipeline: %s \nwith fps: %f.2", best_pipeline, best_fps)
except Exception as e: # pylint: disable=broad-exception-caught
    logger.error("Failed to optimize pipeline: %s", e)
