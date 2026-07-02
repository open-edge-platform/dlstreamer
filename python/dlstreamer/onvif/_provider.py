# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Registration shim for the ``onvifdeviceprovider`` GStreamer plugin.

The actual provider is the sibling plugin at
``dlstreamer/onvif-plugin/python/onvifdeviceprovider.py`` (loaded as a
static plugin in this process) and / or the matching C shim
``libgstdlsonvif.so`` (loaded by ``gst-plugin-scanner`` for any
``gst-inspect`` / pure-C consumer).

This module makes sure the ``onvifdeviceprovider`` factory is present
in the default ``Gst.Registry`` before the Public API tries to spawn a
``Gst.DeviceMonitor`` against it, so callers do not have to think about
plugin paths.
"""
from __future__ import annotations

import importlib.util
import os
import sys

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst  # noqa: E402  (gi version guard must precede import)


_PROVIDER_FACTORY = "onvifdeviceprovider"
_DEVICE_CLASS = "Source/Network/ONVIF"
_state: dict[str, bool] = {"initialised": False}


def device_class() -> str:
    """Filter string consumed by ``Gst.DeviceMonitor.add_filter``."""
    return _DEVICE_CLASS


def _in_gst_plugin_scanner() -> bool:
    """Detect whether we are running inside the ``gst-plugin-scanner``
    subprocess. In that context the C shim plugin
    (``libgstdlsonvif.so``) owns the registration via
    ``register_provider_with_plugin`` — auto-registering a STATIC plugin
    here would race and steal the feature, leaving the shim with 0
    features in the binary registry cache.
    """
    try:
        with open("/proc/self/comm", "r", encoding="ascii") as fh:
            comm = fh.read().strip()
        if comm == "gst-plugin-scan" or comm.startswith("gst-plugin-scan"):
            return True
    except OSError:
        pass
    argv0 = sys.argv[0] if sys.argv else ""
    return os.path.basename(argv0) == "gst-plugin-scanner"


def _register_in_tree_provider() -> bool:
    """Load the sibling ``onvifdeviceprovider.py`` and call its
    ``register_provider()`` helper (which performs a static
    ``gst_plugin_register_static_full`` registration).
    """
    plugin_path = os.path.normpath(
        os.path.join(
            os.path.dirname(__file__), os.pardir,
            "onvif-plugin", "python", "onvifdeviceprovider.py",
        )
    )
    if not os.path.isfile(plugin_path):
        return False

    spec = importlib.util.spec_from_file_location(
        "_dlstreamer_onvifdeviceprovider", plugin_path,
    )
    if spec is None or spec.loader is None:
        return False
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception:  # pylint: disable=broad-except
        return False
    try:
        return bool(module.register_provider())
    except Exception:  # pylint: disable=broad-except
        return False


def ensure_provider_registered() -> bool:
    """Best-effort load of the ONVIF device provider plugin.

    Lookup order:

    1. Factory already in the default ``Gst.Registry`` → nothing to do.
    2. Any directory listed in the ``ONVIFCM_PLUGIN_PATH`` env var is
       scanned into the registry (POSIX ``:``-separated, Windows
       ``;``-separated).
    3. The sibling in-tree Python plugin
       (``dlstreamer/onvif-plugin/python/onvifdeviceprovider.py``) is
       loaded and registered statically.

    Idempotent and silent: missing plugin only surfaces when the
    application later calls ``monitor.start()`` and no devices are
    produced — matching the GStreamer convention of reporting plugin
    problems via the bus instead of exceptions. Skipped when running
    inside ``gst-plugin-scanner``.

    Returns ``True`` if the factory is present after the call.
    """
    if _state["initialised"]:
        return _factory_present()

    if _in_gst_plugin_scanner():
        # Let the C shim own the registration in this subprocess.
        _state["initialised"] = True
        return _factory_present()

    if not Gst.is_initialized():
        Gst.init(None)

    if not _factory_present():
        extra_path = os.environ.get("ONVIFCM_PLUGIN_PATH")
        if extra_path:
            registry = Gst.Registry.get()
            for path in extra_path.split(os.pathsep):
                if path:
                    registry.scan_path(path)

    if not _factory_present():
        try:
            _register_in_tree_provider()
        except Exception:  # pylint: disable=broad-except
            pass

    _state["initialised"] = True
    return _factory_present()


def _factory_present() -> bool:
    return Gst.Registry.get().find_feature(
        _PROVIDER_FACTORY, Gst.DeviceProviderFactory,
    ) is not None
