"""
ONVIF camera profile data structures and configuration management.
"""
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

class ONVIFProfile: #pylint: disable=too-many-instance-attributes, too-many-public-methods
    """
    Represents an ONVIF profile containing camera configuration details.

    This class encapsulates selected configuration information for an ONVIF camera profile,
    including video source settings, video encoder parameters, PTZ (Pan-Tilt-Zoom) 
    configuration, and RTSP streaming URL. It provides a comprehensive interface for
    accessing and managing camera profile attributes through property decorators.

    The class stores three main configuration categories:
    - ONVIF Profile: Basic profile information (name, token, fixed status, RTSP URL)
    - Video Source Configuration (VSC): Video input settings and bounds
    - Video Encoder Configuration (VEC): Encoding parameters, resolution, quality, and multicast
    - PTZ Configuration: Pan-Tilt-Zoom control settings and node information

    Attributes:
        name (str): The profile name
        token (str): Unique profile identifier token
        fixed (bool): Whether the profile configuration is fixed/immutable
        video_source_configuration (str): Video source configuration identifier
        video_encoder_configuration (str): Video encoder configuration identifier
        rtsp_url (str): RTSP streaming URL for this profile
        vsc_name (str): Video source configuration name
        vsc_token (str): Video source configuration token
        vsc_source_token (str): Source token reference
        vsc_bounds (dict): Video source boundary settings
        vec_name (str): Video encoder configuration name
        vec_token (str): Video encoder configuration token
        vec_encoding (str): Video encoding format (e.g., H264, H265)
        vec_resolution (dict): Video resolution settings (width, height)
        vec_quality (int): Video quality setting
        vec_rate_control (dict): Rate control parameters
        vec_multicast (dict): Multicast configuration settings
        ptz_name (str): PTZ configuration name
        ptz_token (str): PTZ configuration token
        ptz_node_token (str): PTZ node identifier token
    """

    def __init__(self):
        # ONVIF Profile details
        self._name = ""
        self._token = ""
        self._fixed = False
        self._video_source_configuration = ""
        self._video_encoder_configuration = ""
        self._rtsp_url = ""

        # Video Source Configuration details
        self._vsc_name = ""
        self._vsc_token = ""
        self._vsc_source_token = ""
        self._vsc_bounds = {}

        # Video Encoder Configuration details
        self._vec_name = ""
        self._vec_token = ""
        self._vec_encoding = ""
        self._vec_resolution = {}
        self._vec_quality = 0
        self._vec_rate_control = {}
        self._vec_multicast = {}

        # PTZ Configuration details
        self._ptz_name = ""
        self._ptz_token = ""
        self._ptz_node_token = ""

        # Audio Encoder Configuration details
        self._aec_name = ""
        self._aec_token = ""
        self._aec_encoding = ""
        self._aec_bitrate = 0
        self._aec_sample_rate = 0

    @property
    def name(self) -> str:
        """Get the name of the ONVIF profile."""
        return self._name

    @name.setter
    def name(self, name: str):
        """Set the name of the ONVIF profile."""
        self._name = name

    @property
    def token(self) -> str:
        """Get the token of the ONVIF profile."""
        return self._token
    @token.setter
    def token(self, token: str):
        """Set the token of the ONVIF profile."""
        self._token = token

    @property
    def fixed(self) -> bool:
        """Get if the ONVIF profile is fixed."""
        return self._fixed
    @fixed.setter
    def fixed(self, fixed: bool):
        """Set if the ONVIF profile is fixed."""
        self._fixed = fixed

    @property
    def video_source_configuration(self) -> str:
        """Get the video source configuration of the ONVIF profile."""
        return self._video_source_configuration
    @video_source_configuration.setter
    def video_source_configuration(self, video_source_configuration: str):
        """Set the video source configuration of the ONVIF profile."""
        self._video_source_configuration = video_source_configuration

    @property
    def video_encoder_configuration(self) -> str:
        """Get the video encoder configuration of the ONVIF profile."""
        return self._video_encoder_configuration
    @video_encoder_configuration.setter
    def video_encoder_configuration(self, video_encoder_configuration: str):
        """Set the video encoder configuration of the ONVIF profile."""
        self._video_encoder_configuration = video_encoder_configuration

    @property
    def rtsp_url(self) -> str:
        """Get the RTSP URL of the ONVIF profile."""
        return self._rtsp_url
    @rtsp_url.setter
    def rtsp_url(self, rtsp_url: str):
        """Set the RTSP URL of the ONVIF profile."""
        self._rtsp_url = rtsp_url

    # Video Source Configuration details
    @property
    def vsc_name(self) -> str:
        """Get the name of the Video Source Configuration."""
        return self._vsc_name
    @vsc_name.setter
    def vsc_name(self, vsc_name: str):
        """Set the name of the Video Source Configuration."""
        self._vsc_name = vsc_name

    @property
    def vsc_token(self) -> str:
        """Get the token of the Video Source Configuration."""
        return self._vsc_token
    @vsc_token.setter
    def vsc_token(self, vsc_token: str):
        """Set the token of the Video Source Configuration."""
        self._vsc_token = vsc_token

    @property
    def vsc_source_token(self) -> str:
        """Get the source token of the Video Source Configuration."""
        return self._vsc_source_token
    @vsc_source_token.setter
    def vsc_source_token(self, vsc_source_token: str):
        """Set the source token of the Video Source Configuration."""
        self._vsc_source_token = vsc_source_token

    @property
    def vsc_bounds(self) -> dict:
        """Get the bounds of the Video Source Configuration."""
        return self._vsc_bounds
    @vsc_bounds.setter
    def vsc_bounds(self, vsc_bounds: dict):
        """Set the bounds of the Video Source Configuration."""
        self._vsc_bounds = vsc_bounds

    # Video Encoder Configuration details
    @property
    def vec_name(self) -> str:
        """Get the name of the Video Encoder Configuration."""
        return self._vec_name
    @vec_name.setter
    def vec_name(self, vec_name: str):
        """Set the name of the Video Encoder Configuration."""
        self._vec_name = vec_name

    @property
    def vec_token(self) -> str:
        """Get the token of the Video Encoder Configuration."""
        return self._vec_token
    @vec_token.setter
    def vec_token(self, vec_token: str):
        """Set the token of the Video Encoder Configuration."""
        self._vec_token = vec_token

    @property
    def vec_encoding(self) -> str:
        """Get the encoding of the Video Encoder Configuration."""
        return self._vec_encoding
    @vec_encoding.setter
    def vec_encoding(self, vec_encoding: str):
        """Set the encoding of the Video Encoder Configuration."""
        self._vec_encoding = vec_encoding

    @property
    def vec_resolution(self) -> dict:
        """Get the resolution of the Video Encoder Configuration."""
        return self._vec_resolution
    @vec_resolution.setter
    def vec_resolution(self, vec_resolution: dict):
        """Set the resolution of the Video Encoder Configuration."""
        self._vec_resolution = vec_resolution

    @property
    def vec_quality(self) -> int:
        """Get the quality of the Video Encoder Configuration."""
        return self._vec_quality
    @vec_quality.setter
    def vec_quality(self, vec_quality: int):
        """Set the quality of the Video Encoder Configuration."""
        self._vec_quality = vec_quality

    @property
    def vec_rate_control(self) -> dict:
        """Get the rate control of the Video Encoder Configuration."""
        return self._vec_rate_control
    @vec_rate_control.setter
    def vec_rate_control(self, vec_rate_control: dict):
        """Set the rate control of the Video Encoder Configuration."""
        self._vec_rate_control = vec_rate_control

    @property
    def vec_multicast(self) -> dict:
        """Get the multicast of the Video Encoder Configuration."""
        return self._vec_multicast
    @vec_multicast.setter
    def vec_multicast(self, vec_multicast: dict):
        """Set the multicast of the Video Encoder Configuration."""
        self._vec_multicast = vec_multicast

    # PTZ Configuration details
    @property
    def ptz_name(self) -> str:
        """Get the name of the PTZ Configuration."""
        return self._ptz_name
    @ptz_name.setter
    def ptz_name(self, ptz_name: str):
        """Set the name of the PTZ Configuration."""
        self._ptz_name = ptz_name

    @property
    def ptz_token(self) -> str:
        """Get the token of the PTZ Configuration."""
        return self._ptz_token
    @ptz_token.setter
    def ptz_token(self, ptz_token: str):
        """Set the token of the PTZ Configuration."""
        self._ptz_token = ptz_token

    @property
    def ptz_node_token(self) -> str:
        """Get the node token of the PTZ Configuration."""
        return self._ptz_node_token
    @ptz_node_token.setter
    def ptz_node_token(self, ptz_node_token: str):
        """Set the node token of the PTZ Configuration."""
        self._ptz_node_token = ptz_node_token

    @property
    def aec_name(self) -> str:
        """Get the name of the Audio Encoder Configuration."""
        return self._aec_name
    @aec_name.setter
    def aec_name(self, aec_name: str):
        """Set the name of the Audio Encoder Configuration."""
        self._aec_name = aec_name

    @property
    def aec_token(self) -> str:
        """Get the token of the Audio Encoder Configuration."""
        return self._aec_token
    @aec_token.setter
    def aec_token(self, aec_token: str):
        """Set the token of the Audio Encoder Configuration."""
        self._aec_token = aec_token

    @property
    def aec_encoding(self) -> str:
        """Get the encoding of the Audio Encoder Configuration."""
        return self._aec_encoding
    @aec_encoding.setter
    def aec_encoding(self, aec_encoding: str):
        """Set the encoding of the Audio Encoder Configuration."""
        self._aec_encoding = aec_encoding

    @property
    def aec_bitrate(self) -> int:
        """Get the bitrate of the Audio Encoder Configuration."""
        return self._aec_bitrate
    @aec_bitrate.setter
    def aec_bitrate(self, aec_bitrate: int):
        """Set the bitrate of the Audio Encoder Configuration."""
        self._aec_bitrate = aec_bitrate

    @property
    def aec_sample_rate(self) -> int:
        """Get the sample rate of the Audio Encoder Configuration."""
        return self._aec_sample_rate
    @aec_sample_rate.setter
    def aec_sample_rate(self, aec_sample_rate: int):
        """Set the sample rate of the Audio Encoder Configuration."""
        self._aec_sample_rate = aec_sample_rate
