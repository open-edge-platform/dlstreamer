# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import time
import logging
import itertools
import os
import re

from preprocess import preprocess_pipeline
from processors.inference import DeviceGenerator, BatchGenerator, NireqGenerator

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

####################################### Init ######################################################

Gst.init()
logger = logging.getLogger(__name__)
logger.info("GStreamer initialized successfully")
gst_version = Gst.version()
logger.info("GStreamer version: %d.%d.%d",
            gst_version.major,
            gst_version.minor,
            gst_version.micro)

####################################### Main Logic ################################################

# Steps of pipeline optimization:
# 1. Measure the baseline pipeline's performace.
# 2. Pre-process the pipeline to cover cases where we're certain of the best alternative.
# 3. Prepare a set of generators providing alternatives for elements.
# 4. Iterate over the generators
# 5. Iterate over the suggestions from every processor
# 6. Any time a better pipeline is found, save it and its performance information.
# 7. Return the best discovered pipeline.
def get_optimized_pipeline(pipeline, search_duration = 300, sample_duration = 10):
    # Test for tee element presence
    if re.search("[^a-zA-Z]tee[^a-zA-Z]", pipeline):
        raise RuntimeError("Pipelines containing the tee element are currently not supported!")

    pipeline = pipeline.split("!")

    # Measure the performance of the original pipeline
    try:
        fps = sample_pipeline(pipeline, sample_duration)
    except Exception as e:
        logger.error("Pipeline failed to start, unable to measure fps: %s", e)
        raise RuntimeError("Provided pipeline is not valid") from e

    logger.info("FPS: %f.2", fps)

    # Make pipeline definition portable across inference devices.
    # Replace elements with known better alternatives.
    try:
        preproc_pipeline = " ! ".join(pipeline)
        preproc_pipeline = preprocess_pipeline(preproc_pipeline)
        preproc_pipeline = preproc_pipeline.split(" ! ")

        preproc_fps = sample_pipeline(preproc_pipeline, sample_duration)
        if preproc_fps > fps:
            fps = preproc_fps
            pipeline = preproc_pipeline
    except Exception:
        logger.error("Pipeline pre-processing failed, using original pipeline instead")

    generators = [
        DeviceGenerator(),
        BatchGenerator(),
        NireqGenerator()
    ]

    best_pipeline = pipeline
    best_fps = fps
    start_time = time.time()
    for generator in generators:
        generator.init_pipeline(best_pipeline)
        for pipeline in generator:
            cur_time = time.time()
            if cur_time - start_time > search_duration:
                break

            try:
                fps = sample_pipeline(pipeline, sample_duration)

                if fps > best_fps:
                    best_fps = fps
                    best_pipeline = pipeline

            except Exception as e:
                logger.debug("Pipeline failed to start: %s", e)

    # Reconstruct the pipeline as a single string and return it.
    return "!".join(best_pipeline), best_fps

##################################### Pipeline Running ############################################

def sample_pipeline(pipeline, sample_duration):
    pipeline = pipeline.copy()

    # check if there is an fps counter in the pipeline, add one otherwise
    has_fps_counter = False
    for element in pipeline:
        if "gvafpscounter" in element:
            has_fps_counter = True

    if not has_fps_counter:
        for i, element in enumerate(reversed(pipeline)):
            if "gvadetect" in element or "gvaclassify" in element:
                pipeline.insert(len(pipeline) - i, " gvafpscounter " )
                break

    pipeline = "!".join(pipeline)
    logger.debug("Testing: %s", pipeline)

    pipeline = Gst.parse_launch(pipeline)

    logger.info("Sampling for %s seconds...", str(sample_duration))
    fps_counter = next(filter(lambda element: "gvafpscounter" in element.name, reversed(pipeline.children))) # pylint: disable=line-too-long

    bus = pipeline.get_bus()

    ret = pipeline.set_state(Gst.State.PLAYING)
    _, state, _ = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    logger.debug("Pipeline state: %s, %s", state, ret)

    terminate = False
    start_time = time.time()
    while not terminate:
        time.sleep(1)

        # Incorrect pipelines sometimes get stuck in Ready state instead of failing.
        # Terminate in those cases.
        _, state, _ = pipeline.get_state(Gst.CLOCK_TIME_NONE)
        if state == Gst.State.READY:
            pipeline.set_state(Gst.State.NULL)
            process_bus(bus)
            del pipeline
            raise RuntimeError("Pipeline not healthy, terminating early")

        cur_time = time.time()
        if cur_time - start_time > sample_duration:
            terminate = True

    ret = pipeline.set_state(Gst.State.NULL)
    logger.debug("Setting pipeline to NULL: %s", ret)
    _, state, _ = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    logger.debug("Pipeline state: %s", str(state))
    process_bus(bus)

    del pipeline
    fps = fps_counter.get_property("avg-fps")
    logger.debug("Sampled fps: %f.2", fps)
    return fps

def process_bus(bus):
    # Process any messages from the bus
    message = bus.pop()
    while message is not None:
        if message.type == Gst.MessageType.ERROR:
            error, _ = message.parse_error()
            logger.error("Pipeline error: %s", error.message)
        elif message.type == Gst.MessageType.WARNING:
            warning, _ = message.parse_warning()
            logger.warning("Pipeline warning: %s", warning.message)
        elif message.type == Gst.MessageType.STATE_CHANGED:
            old, new, _ = message.parse_state_changed()
            logger.debug("State changed: %s -> %s ", old, new)
        else:
            logger.error("Other message: %s", str(message))
        message = bus.pop()
