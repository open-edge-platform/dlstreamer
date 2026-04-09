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
from processors.inference import DeviceGenerator, BatchGenerator, NireqGenerator, add_instance_ids

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst # pylint: disable=no-name-in-module

SINGLE_STREAM = 1
DEFAULT_SEARCH_DURATION = 300

####################################### Init ######################################################

Gst.init()
logger = logging.getLogger(__name__)
logger.debug("GStreamer initialized successfully")
gst_version = Gst.version()
logger.debug("GStreamer version: %d.%d.%d",
            gst_version.major,
            gst_version.minor,
            gst_version.micro)

################################### Init and config ###############################################

class DLSOptimizer:
    def __init__(self):
        # configuration
        self._start_time = time.time()
        self._sample_duration = 10
        self._multistream_fps_limit = 30
        self._enable_cross_stream_batching = False
        self._detections_error_threshold = 0.95

        # internal fields
        self._initial_detections = 0
        self._initial_fps = 0
        self._initial_pipeline = []
        self._optimal_pipeline = []
        self._optimal_fps = 0
        self._optimal_streams = SINGLE_STREAM
        self._generators = {
            "device": DeviceGenerator(),
            "batch": BatchGenerator(),
            "nireq": NireqGenerator()
        }

        # deprecated
        self._search_duration = 300

    def get_baseline_pipeline(self):
        return "!".join(self._initial_pipeline), self._initial_fps, SINGLE_STREAM

    def get_optimal_pipeline(self):
        return "!".join(self._optimal_pipeline), self._optimal_fps, self._optimal_streams

    def enable_cross_stream_batching(self, enable): # pylint: disable=missing-function-docstring
        self._enable_cross_stream_batching = enable

    def set_sample_duration(self, duration):
        self._sample_duration = duration

    def set_multistream_fps_limit(self, limit):
        self._multistream_fps_limit = limit

    def set_allowed_devices(self, devices):
        self._generators["device"].set_allowed_devices(devices)

    def set_detections_error_threshold(self, threshold):
        self._detections_error_threshold = threshold

    # deprecated
    def set_search_duration(self, duration):
        self._search_duration = duration

    ################################### Main Logic ################################################

    # Steps of pipeline optimization:
    # 1. Measure the baseline pipeline's performace.
    # 2. Pre-process the pipeline to cover cases where we're certain of the best alternative.
    # 3. Prepare a set of generators providing alternatives for elements.
    # 4. Iterate over the generators
    # 5. Iterate over the suggestions from every generator
    # 6. Any time a better pipeline is found, save it and its performance information.
    # 7. Return the best discovered pipeline.
    def optimize_for_fps(self, pipeline, search_duration = DEFAULT_SEARCH_DURATION):
        start_time = time.time()
        for (_, _) in self.iter_optimize_for_fps(pipeline):
            cur_time = time.time()
            if cur_time - start_time > search_duration:
                break

        pipeline, fps, _ = self.get_optimal_pipeline()
        return pipeline, fps

    def iter_optimize_for_fps(self, pipeline):
        # Test for tee element presence
        if re.search("[^a-zA-Z]tee[^a-zA-Z]", pipeline):
            raise RuntimeError("Pipelines containing the tee element are currently not supported!")

        pipeline = pipeline.split("!")

        # Run pre-optimization steps
        self._establish_baseline(pipeline)
        pipeline = self._run_preprocessing(pipeline)

        if self._enable_cross_stream_batching:
            pipeline = add_instance_ids(pipeline)

        # Perform optimization
        logger.debug("Starting optimization process for FPS improvements...")
        self._optimal_pipeline = pipeline.copy()
        self._optimal_fps = self._initial_fps
        for (pipeline, fps) in self._optimize_pipeline(pipeline, self._initial_fps, self._initial_detections, 1):
            if fps > self._optimal_fps:
                self._optimal_fps = fps
                self._optimal_pipeline = pipeline

            yield "!".join(pipeline), fps

    def optimize_for_streams(self, pipeline, search_duration = DEFAULT_SEARCH_DURATION):
        start_time = time.time()
        for (_, _, _) in self.iter_optimize_for_streams(pipeline):
            cur_time = time.time()
            if cur_time - start_time > search_duration:
                break

        pipeline, fps, streams = self.get_optimal_pipeline()
        return pipeline, fps, streams

    def iter_optimize_for_streams(self, initial_pipeline):
        # Test for tee element presence
        if re.search("[^a-zA-Z]tee[^a-zA-Z]", initial_pipeline):
            raise RuntimeError("Pipelines containing the tee element are currently not supported!")

        initial_pipeline = initial_pipeline.split("!")

        # Run pre-optimization steps
        self._establish_baseline(initial_pipeline)
        initial_pipeline = self._run_preprocessing(initial_pipeline)

        initial_pipeline = add_instance_ids(initial_pipeline)

        # Perform optimization
        start_time = time.time()
        self._optimal_pipeline = initial_pipeline.copy()
        self._optimal_fps = self._initial_fps
        best_streams = 0
        for streams in range(1, 128):
            for (pipeline, fps) in self._optimize_pipeline(initial_pipeline, self._initial_fps, self._initial_detections, streams):
                if fps > self._multistream_fps_limit and (fps > self._optimal_fps or streams > self._optimal_streams):
                    logger.info(f"limit: {fps > self._multistream_fps_limit}")
                    logger.info(f"fps: {fps > self._optimal_fps}")
                    logger.info(f"streams: {streams > self._optimal_streams}")
                    self._optimal_fps = fps
                    self._optimal_pipeline = pipeline
                    self._optimal_streams = streams

                yield "!".join(pipeline), fps, streams


    def _establish_baseline(self, pipeline):
        # Measure the performance of the original pipeline
        try:
            logger.debug("Measuring performance of the original pipeline...")
            self._initial_pipeline = pipeline.copy()
            self._initial_fps, self._initial_detections = sample_pipeline([pipeline], self._sample_duration)
            self._optimal_pipeline = []
            self._optimal_fps = 0
            self._optimal_streams = SINGLE_STREAM
        except Exception as e:
            logger.error("Pipeline failed to start, unable to measure fps: %s", e)
            raise RuntimeError("Provided pipeline is not valid") from e

        logger.debug("FPS: %.2f", self._initial_fps)

    def _run_preprocessing(self, pipeline):
        # Replace elements with known better alternatives.
        try:
            preproc_pipeline = " ! ".join(pipeline)
            preproc_pipeline = preprocess_pipeline(preproc_pipeline)
            preproc_pipeline = preproc_pipeline.split(" ! ")

            if preproc_pipeline != pipeline:
                logger.info("Measuring performance of the original pipeline after pre-processing optimizations...")
                sample_pipeline([preproc_pipeline], self._sample_duration)

            return preproc_pipeline

        except Exception:
            logger.error("Pipeline pre-processing failed, using original pipeline instead")
        
        return pipeline

    def _optimize_pipeline(self, initial_pipeline, initial_fps, initial_detections, streams):
        best_pipeline = initial_pipeline
        best_fps = initial_fps

        for generator in self._generators.values():
            generator.init_pipeline(best_pipeline)
            for pipeline in generator:
                try:
                    pipelines = []
                    for _ in range(0, streams):
                        pipelines.append(pipeline)

                    fps, detections = sample_pipeline(pipelines, self._sample_duration)

                    if initial_detections == 0:
                        # skip only if we still have zero detections
                        if detections == 0:
                            logger.debug("Pipeline reporting detections under error margin, skipping")
                            continue
                    elif detections / initial_detections < self._detections_error_threshold:
                        logger.debug("Pipeline reporting detections under error margin, skipping")
                        continue

                    if fps > best_fps:
                        best_fps = fps
                        best_pipeline = pipeline

                    yield pipeline, fps

                except Exception as e:
                    logger.debug("Pipeline failed to start: %s", e)

##################################### Pipeline Running ############################################

def sample_pipeline(pipelines, sample_duration):
    pipelines = pipelines.copy()

    pipeline = pipelines[0]
    # check if there is an fps counter in the pipeline, add one otherwise
    has_fps_counter = False
    for element in pipeline:
        if "gvafpscounter" in element:
            has_fps_counter = True

    if not has_fps_counter:
        for i, element in enumerate(reversed(pipeline)):
            if "gvadetect" in element or "gvaclassify" in element:
                pipeline.insert(len(pipeline) - i, " queue ! gvafpscounter " )
                break

    pipelines = list(map(lambda pipeline: "!".join(pipeline), pipelines))
    pipeline = " ".join(pipelines)
    logger.debug("Testing: %s", pipeline)

    pipeline = Gst.parse_launch(pipeline)

    logger.debug("Sampling for %s seconds...", str(sample_duration))
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
    detections = fps_counter.get_property("detections")
    logger.debug("Sampled fps: %.2f", fps)
    return fps, detections

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

