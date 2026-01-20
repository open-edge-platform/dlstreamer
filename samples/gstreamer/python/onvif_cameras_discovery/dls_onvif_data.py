################################################################################
#  Copyright (C) 2026 Intel Corporation
#
#  SPDX-License-Identifier: MIT
################################################################################

VERBOSE: bool = False

class ONVIFDeviceCapabilities:
    """
    """

    def __init__(self, media: bool, ptz: bool, analytics: bool):
        self.media = media
        self.ptz = ptz
        self.analytics = analytics


class ONVIFVideoCapabilities:
    """
    """

    def __init__(self,):
        pass



class ONVIFAudioCapabilities:
    """
    """

    def __init__(self,):
        pass


class ONVIFPTZCapabilities:
    """
    """

    def __init__(self,):
        pass

class ONVIFProfile:
    """
    """

    _name = ""
    _token = ""
    _fixed = False
    _video_source_configuration = ""
    _video_encoder_configuration = ""
    _rtsp_url = ""


    _vsc_name = ""
    _vsc_token = ""
    _vsc_source_token = ""
    _vsc_bounds = {}

    _vec_name = ""
    _vec_token = ""
    _vec_encoding = ""
    _vec_resolution = {}
    _vec_quality = 0
    _vec_rate_control = {}
    _vec_multicast = {}


    _ptz_name = ""
    _ptz_token = ""
    _ptz_node_token = ""


    def __init__(self,):
        pass

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