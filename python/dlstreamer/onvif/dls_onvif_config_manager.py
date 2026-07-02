# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Manage pipeline definitions and their target-camera bindings.

Supports two population modes, freely mixed:

* **file-backed** — load definitions from a JSON file (legacy behaviour
  used by :class:`DlsOnvifDiscoveryEngine`);
* **in-memory / programmatic** — add, update, remove and enumerate
  definitions from application code via :meth:`add_pipeline_definition`,
  :meth:`remove_pipeline_definition` and :meth:`list_pipeline_definitions`.

A process-wide default instance is exposed by :func:`default_config_manager`
so callers that do not want to plumb an instance around can share one.
"""

import json
import threading
from typing import Optional


class DlsOnvifConfigManager:
    """Registry of pipeline definitions keyed by camera identity (host:port)."""

    def __init__(self, json_file_path: Optional[str] = None):
        """Create the manager.

        Passing ``None`` (or an empty string) skips file loading and yields
        a purely in-memory registry — populate it with
        :meth:`add_pipeline_definition`.
        """
        self._config_file_path = json_file_path or None
        self.cameras: dict[str, dict] = {}
        self.verbose: bool = False
        self._lock = threading.RLock()
        if self._config_file_path:
            self._load_config()

    def __del__(self):
        """Clean up resources held by the configuration manager."""
        try:
            self.cameras.clear()
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        self._config_file_path = None

    # ------------------------------------------------------------------ #
    # File-backed population
    # ------------------------------------------------------------------ #
    def _load_config(self):
        """Load the configuration from the JSON file into the cameras dict"""
        try:
            if self._config_file_path:
                with open(self._config_file_path, "r", encoding="utf-8") as file:
                    raw = json.load(file)
                self.verbose = bool(raw.pop("verbose", False))
                with self._lock:
                    self.cameras = {
                        k: v for k, v in raw.items() if isinstance(v, dict)
                    }
                    for cam_name, cam in self.cameras.items():
                        try:
                            cam["port"] = int(cam.get("port") or 80)
                        except (ValueError, TypeError):
                            print(
                                f"[WARN] Invalid port '{cam.get('port')}' "
                                f"for camera '{cam_name}', defaulting to 80"
                            )
                            cam["port"] = 80
        except FileNotFoundError:
            print(
                f"Configuration file {self._config_file_path} not found. Starting "
                f"with an empty camera list."
            )
            with self._lock:
                self.cameras = {}
        except json.JSONDecodeError as e:
            print(f"JSON parsing error in {self._config_file_path}: {e}")
            with self._lock:
                self.cameras = {}

    def refresh_cameras(self):
        """Reload from the backing JSON file (if any). In-memory-only entries are lost."""
        with self._lock:
            self.cameras.clear()
        if self._config_file_path:
            self._load_config()

    # ------------------------------------------------------------------ #
    # Lookup
    # ------------------------------------------------------------------ #
    def get_pipeline_definition_by_ip_port(
        self, ip_address: str, port: int
    ) -> Optional[str]:
        """Return the pipeline definition for a camera based on
        its IP address and port."""

        try:
            port = int(port)
        except (TypeError, ValueError):
            return None

        with self._lock:
            snapshot = list(self.cameras.values())

        for camera in snapshot:
            if self.verbose:
                print(
                    f"Checking camera: {camera.get('hostname')}:{camera.get('port')}"
                    f" looking for {ip_address}:{port}",
                    end="",
                    flush=True,
                )
            if camera.get("hostname") == ip_address:
                if camera.get("port") == port:
                    if self.verbose:
                        print(" ... found")
                    return camera.get("definition")
            if self.verbose:
                print(" ... not found")
        return None

    # ------------------------------------------------------------------ #
    # In-memory / programmatic CRUD
    # ------------------------------------------------------------------ #
    def add_pipeline_definition(
        self,
        hostname: str,
        port: int,
        definition: str,
        name: Optional[str] = None,
    ) -> str:
        """Register (or update) a pipeline definition for ``hostname:port``.

        Parameters
        ----------
        hostname:
            Camera IP or DNS name — must match the hostname reported by
            discovery for the lookup to succeed.
        port:
            ONVIF service port (usually 80).
        definition:
            Pipeline template appended after ``rtspsrc location="..."`` by
            :meth:`DlsOnvifDiscoveryEngine._create_pipelines_for_entry`.
            Typically starts with ``" ! "`` (e.g. ``" ! rtph264depay ! ..."``).
        name:
            Optional stable key used to reference the entry later.
            Defaults to ``"<hostname>_<port>"``. An existing entry with
            the same ``hostname:port`` is replaced (its old ``name`` is
            removed too).

        Returns
        -------
        The entry ``name`` (either the supplied one or the auto-generated key).
        """
        if not hostname:
            raise ValueError("hostname must be a non-empty string")
        if definition is None:
            raise ValueError("definition must not be None")

        try:
            port_int = int(port)
        except (TypeError, ValueError) as exc:
            raise ValueError(f"port must be an integer, got {port!r}") from exc

        key = name or f"{hostname}_{port_int}"
        with self._lock:
            # Drop any existing entry pointing at the same host:port under
            # a different key so lookups stay unambiguous.
            stale = [
                k for k, cam in self.cameras.items()
                if k != key
                and cam.get("hostname") == hostname
                and cam.get("port") == port_int
            ]
            for k in stale:
                self.cameras.pop(k, None)

            self.cameras[key] = {
                "hostname": hostname,
                "port": port_int,
                "definition": definition,
            }
        return key

    def remove_pipeline_definition(
        self,
        hostname: Optional[str] = None,
        port: Optional[int] = None,
        *,
        name: Optional[str] = None,
    ) -> bool:
        """Remove a definition. Match by ``name`` or by ``hostname``+``port``.

        Returns ``True`` when an entry was removed.
        """
        with self._lock:
            if name is not None:
                return self.cameras.pop(name, None) is not None

            if hostname is None or port is None:
                return False

            try:
                port_int = int(port)
            except (TypeError, ValueError):
                return False

            victims = [
                k for k, cam in self.cameras.items()
                if cam.get("hostname") == hostname and cam.get("port") == port_int
            ]
            for k in victims:
                self.cameras.pop(k, None)
            return bool(victims)

    def list_pipeline_definitions(self) -> list[dict]:
        """Snapshot of registered definitions as a list of dicts.

        Each dict has keys: ``name``, ``hostname``, ``port``, ``definition``.
        """
        with self._lock:
            return [
                {
                    "name": key,
                    "hostname": cam.get("hostname", ""),
                    "port": int(cam.get("port") or 80),
                    "definition": cam.get("definition", ""),
                }
                for key, cam in self.cameras.items()
            ]

    def clear_pipeline_definitions(self) -> None:
        """Remove every in-memory definition."""
        with self._lock:
            self.cameras.clear()


# --------------------------------------------------------------------------- #
# Process-wide default instance
# --------------------------------------------------------------------------- #
_default_lock = threading.Lock()
_default_manager: Optional[DlsOnvifConfigManager] = None


def default_config_manager() -> DlsOnvifConfigManager:
    """Return (creating on first call) the process-wide default manager.

    Convenient for callers that want to share a single in-memory
    definition registry across modules without threading an instance
    through their APIs.
    """
    global _default_manager  # pylint: disable=global-statement
    with _default_lock:
        if _default_manager is None:
            _default_manager = DlsOnvifConfigManager()
        return _default_manager


def set_default_config_manager(manager: Optional[DlsOnvifConfigManager]) -> None:
    """Install ``manager`` as the process-wide default (or reset with ``None``)."""
    global _default_manager  # pylint: disable=global-statement
    with _default_lock:
        _default_manager = manager
