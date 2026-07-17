# pylint: disable=missing-module-docstring
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
import warnings
import requests
import sys

from requests import RequestException
from urllib.parse import urljoin
from prometheus_client.parser import text_string_to_metric_families
from preprocess import preprocess_pipeline
from processors.device import DeviceGenerator
from processors.batch import BatchGenerator
from processors.nireq import NireqGenerator
from processors.utils import add_instance_ids
from optimization_targets import FpsTarget, PowerTarget

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst # pylint: disable=no-name-in-module

DEFAULT_SEARCH_DURATION = 300

####################################### Init ######################################################

Gst.init([])
logger = logging.getLogger(__name__)
logger.debug("GStreamer initialized successfully")
gst_version = Gst.version()
logger.debug("GStreamer version: %d.%d.%d",
            gst_version.major,
            gst_version.minor,
            gst_version.micro)

####################################### Helpers ###################################################

class FaultyPipeline(Exception): # pylint: disable=missing-class-docstring
    pass

class TestHalt(Exception): # pylint: disable=missing-class-docstring
    pass

################################### Init and config ###############################################

class DLSOptimizer:
    def __init__(self):
        # configuration
        self._start_time = time.time()
        self._sample_duration = 10
        self._fps_limit = None
        self._power_limit = None
        self._enable_cross_stream_batching = False
        self._detections_error_threshold = 0.95
        self._paused = False
        self._metrics_url = None

        # internal fields
        self._initial_pipeline = []
        self._initial_result = {}
        self._optimal_pipeline = []
        self._optimal_result = {}
        self._generators = {
            "device": DeviceGenerator(),
            "batch": BatchGenerator(),
            "nireq": NireqGenerator()
        }

    def get_baseline_pipeline(self): # pylint: disable=missing-function-docstring
        return "!".join(self._initial_pipeline), self._initial_result

    def get_optimal_pipeline(self): # pylint: disable=missing-function-docstring
        return "!".join(self._optimal_pipeline), self._optimal_result

    def enable_cross_stream_batching(self, enable): # pylint: disable=missing-function-docstring
        self._enable_cross_stream_batching = enable

    def set_sample_duration(self, duration): # pylint: disable=missing-function-docstring
        self._sample_duration = duration

    def set_fps_limit(self, limit): # pylint: disable=missing-function-docstring
        self._fps_limit = limit

    def set_power_limit(self, limit): # pylint: disable=missing-function-docstring
        self._power_limit = limit

    def set_allowed_devices(self, devices): # pylint: disable=missing-function-docstring
        self._generators["device"].set_allowed_devices(devices)

    def set_batch_sizes(self, sizes): # pylint: disable=missing-function-docstring
        self._generators["batch"].set_batch_sizes(sizes)

    def set_nireq_sizes(self, sizes): # pylint: disable=missing-function-docstring
        self._generators["nireq"].set_nireq_sizes(sizes)

    def set_detections_error_threshold(self, threshold): # pylint: disable=missing-function-docstring
        self._detections_error_threshold = threshold

    def set_metrics_url(self, url):
        metrics_url = urljoin(url, "metrics")
        try:
            resp = requests.get(metrics_url)
            resp.raise_for_status()
            text_string_to_metric_families(resp.text)
            self._metrics_url = metrics_url
        except RequestException as e:
            raise RuntimeError("Couldn't establish connection with power metrics endpoint!") from e
        except ValueError as e:
            raise RuntimeError("Endpoint returned invalid Prometheus metrics!") from e


    # deprecated
    def set_search_duration(self, duration):
        warnings.warn(
            "Function set_search_duration has been deprecated. "
            "Please pass search duration when calling optimize_for_fps or optimize_for_streams instead.",
            DeprecationWarning,
            stacklevel=2
        )

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
        return self._optimize(pipeline, FpsTarget(), search_duration)

    def iter_optimize_for_fps(self, initial_pipeline): # pylint: disable=missing-function-docstring
        return self._iter_optimize(initial_pipeline, FpsTarget())

    def optimize_for_power(self, pipeline, search_duration = DEFAULT_SEARCH_DURATION): # pylint: disable=missing-function-docstring
        return self._optimize(pipeline, PowerTarget(), search_duration)

    def iter_optimize_for_power(self, initial_pipeline): # pylint: disable=missing-function-docstring
        return self._iter_optimize(initial_pipeline, PowerTarget())

    def optimize_for_streams(self, pipeline, search_duration = DEFAULT_SEARCH_DURATION):
        start_time = time.time()
        for (_, _) in self.iter_optimize_for_streams(pipeline):
            cur_time = time.time()
            if cur_time - start_time > search_duration:
                break

        pipeline, result = self.get_optimal_pipeline()
        return pipeline, result

    def iter_optimize_for_streams(self, initial_pipeline):
        # Test for tee element presence
        if re.search("[^a-zA-Z]tee[^a-zA-Z]", initial_pipeline):
            raise RuntimeError("Pipelines containing the tee element are currently not supported!")

        if self._power_limit is None and self._fps_limit is None:
            raise RuntimeError("When optimizing for streams, configure at least one limit to prevent infinite looping.")

        initial_pipeline = initial_pipeline.split("!")

        # Run pre-optimization steps
        self._establish_baseline(initial_pipeline)
        initial_pipeline = self._run_preprocessing(initial_pipeline)

        initial_pipeline = add_instance_ids(initial_pipeline)
        self._initial_result["streams"] = 1
        self._optimal_result["streams"] = 1

        # Perform optimization
        start_time = time.time()
        best_streams = 0
        for streams in range(1, 128):
            for (pipeline, result) in self._evaluate_candidates(initial_pipeline, FpsTarget(), streams):
                if result:
                    fps = result["fps"]
                    result["streams"] = streams
                    if fps > self._fps_limit and (fps > self._optimal_result["fps"] or streams > self._optimal_result["streams"]):
                        self._optimal_result = result.copy()
                        self._optimal_pipeline = pipeline

                yield "!".join(pipeline), result

    def _optimize(self, pipeline, target, search_duration = DEFAULT_SEARCH_DURATION):
        start_time = time.time()
        for (_, _) in self._iter_optimize(pipeline, target, 1):
            cur_time = time.time()
            if cur_time - start_time > search_duration:
                break

        pipeline, result = self.get_optimal_pipeline()
        return pipeline, result

    def _iter_optimize(self, initial_pipeline, target):
        # Test for tee element presence
        if re.search("[^a-zA-Z]tee[^a-zA-Z]", initial_pipeline):
            raise RuntimeError("Pipelines containing the tee element are currently not supported!")

        initial_pipeline = initial_pipeline.split("!")

        # Run pre-optimization steps
        self._establish_baseline(initial_pipeline)
        initial_pipeline = self._run_preprocessing(initial_pipeline)

        if self._enable_cross_stream_batching:
            initial_pipeline = add_instance_ids(initial_pipeline)

        # iterate over candidates and find the best one
        for (pipeline, result) in self._evaluate_candidates(initial_pipeline, target, 1):
            if result:
                if self._passes_limits(result) and target.is_better(result, self._optimal_result):
                    self._optimal_result = result.copy()
                    self._optimal_pipeline = pipeline.copy()

            yield "!".join(pipeline), result

    def _passes_limits(self, result):
        fps_pass = self._fps_limit is None or result["fps"] > self._fps_limit
        power_pass = self._power_limit is None or result["power"] > self._power_limit

        return fps_pass and power_pass

    def _establish_baseline(self, pipeline):
        # Measure the performance of the original pipeline
        try:
            logger.debug("Measuring performance of the original pipeline...")
            self._initial_pipeline = pipeline.copy()
            self._initial_result = self._sample_pipeline([pipeline], self._sample_duration)
            self._optimal_pipeline = self._initial_pipeline.copy()
            self._optimal_result = self._initial_result.copy()
        except Exception as e:
            logger.error("Pipeline failed to start, unable to measure fps: %s", e)
            raise RuntimeError("Provided pipeline is not valid") from e

        logger.debug("FPS: %.2f", self._initial_result["fps"])

    def _run_preprocessing(self, pipeline):
        # Replace elements with known better alternatives.
        try:
            preproc_pipeline = " ! ".join(pipeline)
            preproc_pipeline = preprocess_pipeline(preproc_pipeline)
            preproc_pipeline = preproc_pipeline.split(" ! ")

            if preproc_pipeline == pipeline:
                logger.debug("Pre-processing didn't manage to improve pipeline.")
                return pipeline

            logger.info("Measuring performance of the original pipeline after pre-processing optimizations...")
            self._sample_pipeline([preproc_pipeline], self._sample_duration)

            return preproc_pipeline

        except Exception:
            logger.error("Pipeline pre-processing failed, using original pipeline instead")
        
        return pipeline

    def _evaluate_candidates(self, initial_pipeline, target, streams):
        best_pipeline = initial_pipeline
        best_result = self._initial_result

        for generator in self._generators.values():
            generator.init_pipeline(best_pipeline)
            for pipeline in generator:
                try:
                    pipelines = []
                    for _ in range(0, streams):
                        pipelines.append(pipeline)

                    result = self._sample_pipeline(pipelines, self._sample_duration)

                    if self._initial_result["detections"] != 0 and result["detections"] / self._initial_result["detections"] < self._detections_error_threshold:
                        raise FaultyPipeline("Pipeline reporting detections under error margin")

                    if target.is_better(result, best_result):
                        best_result = result.copy()
                        best_pipeline = pipeline

                    logger.info(str(result))

                    yield pipeline, result

                except TestHalt:
                    logger.info("Testing process paused.")
                    while self._paused:
                        time.sleep(0.5)
                    logger.info("Testing process restarted.")
                except Exception as e:
                    logger.debug("Pipeline failed sampling: %s", e)
                    yield pipeline, None

##################################### Pipeline Running ############################################

    def _sample_pipeline(self, pipelines, sample_duration):
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
        logger.debug("Setting pipeline to PLAYING: %s", ret)

        result = None
        try:
            terminate = False
            start_time = time.time()
            power_total = 0.0
            power_samples = 0
            while not terminate:
                if self._paused:
                    raise TestHalt("Interrupt signal received, halting test run")

                message = bus.timed_pop(1 * Gst.SECOND)

                if message:
                    if message.type == Gst.MessageType.ERROR:
                        error, _ = message.parse_error()
                        logger.error("Pipeline error: %s", error.message)
                        terminate = True
                    elif message.type == Gst.MessageType.EOS:
                        terminate = True
                    elif message.type == Gst.MessageType.WARNING:
                        warning, _ = message.parse_warning()
                        logger.warning("Pipeline warning: %s", warning.message)
                    elif message.type == Gst.MessageType.STATE_CHANGED:
                        old, new, _ = message.parse_state_changed()
                        logger.debug("State changed: %s -> %s ", old, new)
                    else:
                        logger.debug("Other message: %s", str(message))

                # Incorrect pipelines sometimes get stuck in Ready state instead of failing.
                # Terminate in those cases.
                _, state, _ = pipeline.get_state(3 * Gst.SECOND)
                if state != Gst.State.PLAYING:
                    raise FaultyPipeline("Pipeline failed to start properly")

                cur_time = time.time()
                if cur_time - start_time > sample_duration:
                    terminate = True

                if self._metrics_url is not None:
                    try:
                        resp = requests.get(self._metrics_url)
                        resp.raise_for_status()
                        power_samples += 1
                        power_total += _collect_power_info(resp)
                    except RequestException as e:
                        logger.warning("Failed to collect power metrics: %s", e)
            fps = fps_counter.get_property("avg-fps")
            detections = fps_counter.get_property("detections")
            logger.debug("Sampled fps: %.2f", fps)

            if power_samples == 0:
                power_total = None
            else:
                power_total = power_total / power_samples

            result = {
                "fps": fps,
                "detections": detections,
                "power": power_total
            }

        finally:
            ret = pipeline.set_state(Gst.State.NULL)
            logger.debug("Setting pipeline to NULL: %s", ret)

            del pipeline

        return result


def _collect_power_info(resp):
    power_total = 0.0
    for family in text_string_to_metric_families(resp.text):
        if family.name == "npu_power":
            power_total += family.samples[0].value

        if family.name == "gpu_power":
            for sample in family.samples:
                if sample.labels["type"] == "pkg_cur_power":
                    power_total += sample.value

    return power_total
