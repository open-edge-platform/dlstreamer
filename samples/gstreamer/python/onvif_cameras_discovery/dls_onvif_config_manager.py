# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""This class is responsible for managing the pipeline's definitions
and configurations. It provides methods to load, save,
and manipulate the pipeline configurations."""

import json


class DlsOnvifConfigManager:
    """This class manages the configuration for ONVIF cameras discovery."""

    def __init__(self, json_file_path: str):
        """Initialize the configuration manager with the path to the configuration file."""
        self._config_file_path = json_file_path
        self.cameras = {}
        self.verbose: bool = False
        self._load_config()

    def __del__(self):
        """Clean up resources held by the configuration manager."""
        self.cameras.clear()
        self._config_file_path = None

    def _load_config(self):
        """Load the configuration from the JSON file into the cameras dict"""
        try:
            if self._config_file_path:
                with open(self._config_file_path, "r", encoding="utf-8") as file:
                    raw = json.load(file)
                self.verbose = bool(raw.pop("verbose", False))
                self.cameras = {k: v for k, v in raw.items() if isinstance(v, dict)}
        except FileNotFoundError:
            print(
                f"Configuration file {self._config_file_path} not found. Starting "
                f"with an empty camera list."
            )
            self.cameras = {}
        except json.JSONDecodeError as e:
            print(f"JSON parsing error in {self._config_file_path}: {e}")
            self.cameras = {}

    def refresh_cameras(self):
        """Refresh the list of cameras based on the current configuration"""
        self.cameras.clear()
        self._load_config()

    def get_pipeline_definition_by_ip_port(self, ip_address: str, port: int) -> str:
        """Return the pipeline definition for a camera based on
        its IP address and port."""

        for camera in self.cameras.values():
            print(
                f"Checking camera: {camera.get('hostname')}:{camera.get('port')} looking for {ip_address}:{port}"
            )
            if camera.get("hostname") == ip_address:
                if camera.get("port") == port:
                    return camera.get("definition")
        return None
