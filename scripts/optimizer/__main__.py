# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import argparse
import logging
import textwrap
import sys
import time
import json

from optimizer import DLSOptimizer # pylint: disable=no-name-in-module

def _display_result(pipeline, fps):
    logger.info("============================== CANDIDATE =============================")
    logger.info("Sampled pipeline: %s", str(pipeline))
    logger.info("")
    logger.info("Recorded fps: %.2f", fps)
    logger.info("======================================================================")

def _display_summary_fps(best_pipeline, best_fps, initial_pipeline, initial_fps):
    logger.info("=============================== SUMMARY ==============================")
    if best_fps > initial_fps:
        logger.info("Optimized pipeline found with %.2f fps improvement over the original pipeline.", best_fps - initial_fps)
        logger.info("Original pipeline FPS: %.2f", initial_fps)
        logger.info("Optimized pipeline: %s", str(best_pipeline))
        logger.info("Optimized pipeline FPS: %.2f", best_fps)
    else:
        logger.info("No optimized pipeline found that outperforms the original pipeline.")
        logger.info("Original pipeline: %s", str(initial_pipeline))
        logger.info("Original pipeline FPS: %.2f", initial_fps)
    logger.info("======================================================================")

def _display_summary_streams(best_pipeline, best_fps, streams):
    full_pipeline = []
    for _ in range(0, streams):
        full_pipeline.append(best_pipeline)
    full_pipeline = " ".join(full_pipeline)

    logger.info("=============================== SUMMARY ==============================")
    logger.info("Optimized pipeline: %s", str(best_pipeline))
    logger.info("Number of streams pipeline can support: %d", streams)
    logger.info("Optimized pipeline FPS at max streams: %.2f", best_fps)
    logger.info("")
    logger.info("Full pipeline: %s", full_pipeline)
    logger.info("======================================================================")

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
parser.add_argument("-v", "--verbose", action="store_true",
                    help="Print more information about the optimization progress")
parser.add_argument("-o", "--output", nargs="?",
                    help="Save optimization results to a file in JSON format")
parser.add_argument("--search-duration", default=300, type=float,
                    help="Duration in seconds of time which should be spent searching for optimized pipelines (default: %(default)s)")
parser.add_argument("--sample-duration", default=10, type=float,
                    help="Duration in seconds of sampling individual pipelines. Longer duration should offer more stable results (default: %(default)s)")
parser.add_argument("--detection-threshold", default=0.95, type=float,
                    help="Minimum threshold of detections that tested pipelines are not allowed to cross in order to count as valid alternatives (default: %(default)s)")
parser.add_argument("--multistream-fps-limit", default=30, type=float,
                    help="Minimum amount of fps allowed when optimizing for multiple streams (default: %(default)s)")
parser.add_argument("--enable-cross-stream-batching", action="store_true",
                    help="Enable cross stream batching for inference elements in fps mode")
parser.add_argument("--log-level", default="INFO", choices=["CRITICAL", "FATAL", "ERROR" ,"WARN", "INFO", "DEBUG"],
                    help="Minimum used log level (default: %(default)s)")
parser.add_argument("--allowed-devices", nargs="+",
                    help="List of allowed devices (CPU, GPU, NPU) to be used by the optimizer. If not specified, all available, detected devices will be used.\n"\
                        "Tool does not support discrete GPU selection.\n"\
                        "eg.--allowed-devices CPU NPU,--allowed-devices GPU")

args=parser.parse_args()

logging.basicConfig(level=args.log_level, format="[%(name)s] [%(levelname)8s] - %(message)s")
logger = logging.getLogger(__name__)

json_result = {}

try:
    optimizer = DLSOptimizer()
    optimizer.set_sample_duration(args.sample_duration)
    optimizer.set_detections_error_threshold(args.detection_threshold)
    optimizer.set_multistream_fps_limit(args.multistream_fps_limit)
    optimizer.enable_cross_stream_batching(args.enable_cross_stream_batching)

    if args.allowed_devices:
        optimizer.set_allowed_devices(args.allowed_devices)

except Exception as e:
    logger.error("Failed to configure optimizer: %s", e)
    sys.exit(1)

pipeline = " ".join(args.PIPELINE)

#logger.info("\nBest found pipeline: %s \nwith fps: %.2f\nwith batches: %d", pipeline, fps, batches)
try:
    match args.mode:
        case "fps":
            json_result["mode"] = "fps"
            json_result["candidates"] = []
            start_time = time.time()
            for (pipeline, fps) in optimizer.iter_optimize_for_fps(pipeline):
                json_result["candidates"].append({"pipeline": pipeline, "fps": fps})
                if args.verbose:
                    _display_result(pipeline, fps)
                
                cur_time = time.time()
                if cur_time - start_time > args.search_duration:
                    break

            base_pipeline, base_fps, _ = optimizer.get_baseline_pipeline()
            best_pipeline, best_fps, _ = optimizer.get_optimal_pipeline()
            json_result["baseline"] = {"pipeline": base_pipeline, "fps": base_fps}
            json_result["optimal"] = {"pipeline": best_pipeline, "fps": best_fps}
            _display_summary_fps(best_pipeline, best_fps, base_pipeline, base_fps)

        case "streams":
            json_result["mode"] = "streams"
            json_result["candidates"] = {}
            start_time = time.time()
            for (pipeline, fps, streams) in optimizer.iter_optimize_for_streams(pipeline):
                try:
                    json_result["candidates"][str(streams)].append({"pipeline": pipeline, "fps": fps})
                except KeyError:
                    json_result["candidates"][str(streams)] = []
                    json_result["candidates"][str(streams)].append({"pipeline": pipeline, "fps": fps})

                full_pipeline = []
                for _ in range(0, streams):
                    full_pipeline.append(pipeline)
                full_pipeline = " ".join(full_pipeline)

                if args.verbose:
                    _display_result(full_pipeline, fps)

                cur_time = time.time()
                if cur_time - start_time > args.search_duration:
                    break

            base_pipeline, base_fps, base_streams = optimizer.get_baseline_pipeline()
            best_pipeline, best_fps, best_streams = optimizer.get_optimal_pipeline()
            json_result["baseline"] = {"pipeline": base_pipeline, "fps": base_fps, "streams": base_streams}
            json_result["optimal"] = {"pipeline": best_pipeline, "fps": best_fps, "streams": best_streams}
            _display_summary_streams(best_pipeline, best_fps, best_streams)
except RuntimeError as e: # pylint: disable=broad-exception-caught
    logger.error("Failed to optimize pipeline: %s", e)

if args.output:
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(json_result, f, ensure_ascii=False, indent=4)