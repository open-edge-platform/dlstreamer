# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import argparse
import logging
import textwrap

from optimizer import DLSOptimizer

parser = argparse.ArgumentParser(
    prog="DLStreamer Pipeline Optimization Tool",
    formatter_class=argparse.RawTextHelpFormatter,
    description="Use this tool to try and find versions of your pipeline that will run with increased performance." # pylint: disable=line-too-long
)
parser.add_argument("mode", choices=["fps", "streams"], metavar="MODE",
                    help=textwrap.dedent('''\
                        The type of optimization that will be performed on the pipeline.
                        Possible values are \"fps\" and \"streams\".

                        fps - the optimizer will explore possible alternatives
                              for the pipeline, trying to locate versions that
                              have increased performance measured by fps.

                        streams - the optimizer will explore possible alternatives
                                  for the pipeline, trying to locate a version which
                                  can support the most streams at once without
                                  crossing a minimum fps threshold.
                                  (check \"multistream-fps-limit for more info)

                    '''))
parser.add_argument("PIPELINE", nargs="+",
                    help="Pipeline to be analyzed")
parser.add_argument("--search-duration", default=300, type=float,
                    help="Duration in seconds of time which should be spent searching for optimized pipelines (default: %(default)s)") # pylint: disable=line-too-long
parser.add_argument("--sample-duration", default=10, type=float,
                    help="Duration in seconds of sampling individual pipelines. Longer duration should offer more stable results (default: %(default)s)") # pylint: disable=line-too-long
parser.add_argument("--multistream-fps-limit", default=30, type=float,
                    help="Minimum amount of fps allowed when optimizing for multiple streams (default: %(default)s)") # pylint: disable=line-too-long
parser.add_argument("--log-level", default="INFO", choices=["CRITICAL", "FATAL", "ERROR" ,"WARN", "INFO", "DEBUG"], # pylint: disable=line-too-long
                    help="Minimum used log level (default: %(default)s)")

args=parser.parse_args()

logging.basicConfig(level=args.log_level, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)

optimizer = DLSOptimizer()
optimizer.set_search_duration(args.search_duration)
optimizer.set_sample_duration(args.sample_duration)
optimizer.set_multistream_fps_limit(args.multistream_fps_limit)

pipeline = " ".join(args.PIPELINE)

#logger.info("\nBest found pipeline: %s \nwith fps: %.2f\nwith batches: %d", pipeline, fps, batches)
try:
    match args.mode:
        case "fps":
            best_pipeline, best_fps = optimizer.optimize_for_fps(pipeline)
            logger.info("\nBest found pipeline: %s \nwith fps: %.2f", best_pipeline, best_fps)
        case "streams":
            best_pipeline, best_fps, streams = optimizer.optimize_for_streams(pipeline)

            full_pipeline = []
            for _ in range(0, streams):
                full_pipeline.append(best_pipeline)

            full_pipeline = " ".join(full_pipeline)

            logger.info("\nBest found pipeline for multi-streams: %s \n" \
                        "with fps: %.2f\n" \
                        "max achieved streams: %d", full_pipeline, best_fps, streams)
except Exception as e: # pylint: disable=broad-exception-caught
    logger.error("Failed to optimize pipeline: %s", e)
