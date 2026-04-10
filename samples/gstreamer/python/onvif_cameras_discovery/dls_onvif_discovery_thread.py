# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""GStreamer pipeline launcher and lifecycle manager for discovered cameras."""
import threading
from gi.repository import Gst, GLib


class DlsLaunchedPipeline:  # pylint: disable=too-many-instance-attributes
    """Class representing an asynchronous GStreamer pipeline."""

    _THREAD_STOP_TIMEOUT_SEC = 2

    def __init__(self, definition, name):
        self.definition = definition
        self.name = name
        self.pipeline = None
        self.loop = GLib.MainLoop()
        self.thread = None
        self.profile = None
        self.ip_address: str = ""
        self._lifecycle_lock = threading.Lock()

    async def start(self):
        """Start the GStreamer pipeline in a separate thread."""

        if not self.definition:
            return

        with self._lifecycle_lock:
            try:
                self.pipeline = Gst.parse_launch(self.definition)
                # Each pipeline gets its own thread for the GLib
                # loop, ensuring "infinite" operation
                self.thread = threading.Thread(target=self.loop.run, daemon=True)
                self.thread.start()
                _state_change = self.pipeline.set_state(Gst.State.PLAYING)
            except Exception as error:  # pylint: disable=broad-exception-caught
                print(
                    f"[ERROR] Failed to start pipeline"
                    f" '{self.name}'"
                    f" (ip={self.ip_address}): {error}"
                )
                self._cleanup_on_failed_start()

    def _cleanup_on_failed_start(self):
        """Rollback partially initialized resources when startup fails."""
        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)

        self.loop.quit()
        self._join_thread_or_raise("startup rollback")

        self.loop = None
        self.pipeline = None
        self.thread = None

    def _join_thread_or_raise(self, context: str) -> None:
        """Join worker thread and fail loudly if it doesn't stop in time."""

        if self.thread is None:
            return

        self.thread.join(timeout=self._THREAD_STOP_TIMEOUT_SEC)
        if self.thread.is_alive():
            raise RuntimeError(
                f"Pipeline thread for '{self.name}' did not stop during {context}."
            )

    def stop(self):
        """Stop the GStreamer pipeline and quit the GLib loop."""
        with self._lifecycle_lock:
            if self.pipeline:
                self.pipeline.set_state(Gst.State.NULL)
                self.pipeline.get_state(Gst.CLOCK_TIME_NONE)

            self.loop.quit()

            self._join_thread_or_raise("stop")

            if self.thread.is_alive():
                print(
                    f"[WARNING] Pipeline thread for"
                    f" '{self.name}' is still alive"
                    f" after stop attempt."
                )

            self.pipeline = None
            self.thread = None
