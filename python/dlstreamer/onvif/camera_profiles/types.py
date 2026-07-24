# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
ONVIF camera profile data structure.
"""

from dataclasses import dataclass, field


@dataclass(eq=False)
class ONVIFProfile:  # pylint: disable=too-many-instance-attributes
    """
    Represents an ONVIF profile containing camera configuration details.

    This class encapsulates selected configuration information for an ONVIF camera profile,
    including video source settings, video encoder parameters, PTZ (Pan-Tilt-Zoom)
    configuration, and RTSP streaming URL.

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

    # Camera connection details
    ip_address: str = ""
    port: int = 0
    username: str = ""
    password: str = ""
    mac_address: str = ""

    # ONVIF Profile details
    name: str = ""
    token: str = ""
    fixed: bool = False
    video_source_configuration: str = ""
    video_encoder_configuration: str = ""
    rtsp_url: str = ""

    # Video Source Configuration details
    vsc_name: str = ""
    vsc_token: str = ""
    vsc_source_token: str = ""
    vsc_bounds: dict = field(default_factory=dict)

    # Video Encoder Configuration details
    vec_name: str = ""
    vec_token: str = ""
    vec_encoding: str = ""
    vec_resolution: dict = field(default_factory=dict)
    vec_quality: int = 0
    vec_rate_control: dict = field(default_factory=dict)
    vec_multicast: dict = field(default_factory=dict)
    vec_framerate_limit: int = 0
    vec_bitrate_limit: int = 0
    vec_encoding_interval: int = 0
    vec_h264_profile: str = ""
    vec_h264_gop_length: int = 0
    vec_mpeg4_profile: str = ""
    vec_mpeg4_gop_length: int = 0

    # PTZ Configuration details
    ptz_name: str = ""
    ptz_token: str = ""
    ptz_node_token: str = ""

    # Audio Source Configuration details
    asc_name: str = ""
    asc_token: str = ""
    asc_source_token: str = ""

    # Audio Encoder Configuration details
    aec_name: str = ""
    aec_token: str = ""
    aec_encoding: str = ""
    aec_bitrate: int = 0
    aec_sample_rate: int = 0

    @property
    def ip(self) -> str:
        """Alias for :attr:`ip_address` (kept for backward compatibility)."""
        return self.ip_address

    @ip.setter
    def ip(self, ip_address: str) -> None:
        self.ip_address = ip_address
