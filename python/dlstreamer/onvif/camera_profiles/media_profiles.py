# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Retrieve ONVIF media profiles from a connected ONVIF client.

This module has no GStreamer dependencies — it only queries the ONVIF
media service and populates :class:`ONVIFProfile` instances.
"""
from typing import List

from .types import ONVIFProfile


def _fetch_media_profiles(  # pylint: disable=too-many-statements, too-many-locals, too-many-branches
    client,
    verbose: bool = False,
) -> List[ONVIFProfile]:
    """Query an ONVIF camera client for its available media profiles.

    Extracts detailed configuration information including video encoder
    settings, audio configurations, PTZ capabilities, and RTSP streaming URIs.

    Args:
        client: An ONVIF client instance used to communicate with the camera
            (e.g. ``onvif.ONVIFCamera``).
        verbose: When true, log each profile field to stdout.

    Returns:
        List of :class:`ONVIFProfile` objects with the extracted profile data.
    """
    media_service = client.create_media_service()

    profiles = media_service.GetProfiles()

    onvif_profiles: List[ONVIFProfile] = []

    for i, profile in enumerate(profiles, 1):
        onvif_profile: ONVIFProfile = ONVIFProfile()
        onvif_profile.name = profile.Name
        onvif_profile.token = profile.token
        if verbose:
            print(f"  Profile {i}:")
            print(f"    Name: {onvif_profile.name}")
            print(f"    Token: {onvif_profile.token}")

        # Fixed profile indicator
        if hasattr(profile, "fixed") and profile.fixed is not None:
            onvif_profile.fixed = profile.fixed

        # Video Source Configuration
        if (
            hasattr(profile, "VideoSourceConfiguration")
            and profile.VideoSourceConfiguration
        ):
            vsc = profile.VideoSourceConfiguration
            onvif_profile.vsc_name = vsc.Name
            onvif_profile.vsc_token = vsc.token
            onvif_profile.vsc_source_token = vsc.SourceToken
            if hasattr(vsc, "Bounds") and vsc.Bounds:
                onvif_profile.vsc_bounds = {
                    "x": vsc.Bounds.x,
                    "y": vsc.Bounds.y,
                    "width": vsc.Bounds.width,
                    "height": vsc.Bounds.height,
                }

        # Video Encoder Configuration
        if (
            hasattr(profile, "VideoEncoderConfiguration")
            and profile.VideoEncoderConfiguration
        ):
            vec = profile.VideoEncoderConfiguration
            onvif_profile.vec_name = vec.Name
            onvif_profile.vec_token = vec.token
            onvif_profile.vec_encoding = vec.Encoding
            if verbose:
                print("    Video Encoder:")
                print(f"      Name: {vec.Name}")
                print(f"      Token: {vec.token}")
                print(f"      Encoding: {vec.Encoding}")
            if hasattr(vec, "Resolution") and vec.Resolution:
                onvif_profile.vec_resolution = {
                    "width": vec.Resolution.Width,
                    "height": vec.Resolution.Height,
                }
                if verbose:
                    print(
                        f"      Resolution: {vec.Resolution.Width}x{vec.Resolution.Height}"
                    )
            if hasattr(vec, "Quality"):
                onvif_profile.vec_quality = vec.Quality
                if verbose:
                    print(f"      Quality: {vec.Quality}")
            if hasattr(vec, "RateControl") and vec.RateControl:
                onvif_profile.vec_framerate_limit = vec.RateControl.FrameRateLimit
                onvif_profile.vec_bitrate_limit = vec.RateControl.BitrateLimit
                if verbose:
                    print(f"      FrameRate Limit: {vec.RateControl.FrameRateLimit}")
                    print(f"      Bitrate Limit: {vec.RateControl.BitrateLimit}")
                if hasattr(vec.RateControl, "EncodingInterval"):
                    onvif_profile.vec_encoding_interval = (
                        vec.RateControl.EncodingInterval
                    )
                    if verbose:
                        print(
                            f"      Encoding Interval: "
                            f"{vec.RateControl.EncodingInterval}"
                        )
            if hasattr(vec, "H264") and vec.H264:
                onvif_profile.vec_h264_profile = vec.H264.H264Profile
                onvif_profile.vec_h264_gop_length = vec.H264.GovLength
                if verbose:
                    print(f"      H264 Profile: {vec.H264.H264Profile}")
                    print(f"      GOP Size: {vec.H264.GovLength}")
            elif hasattr(vec, "MPEG4") and vec.MPEG4:
                onvif_profile.vec_mpeg4_profile = vec.MPEG4.Mpeg4Profile
                onvif_profile.vec_mpeg4_gop_length = vec.MPEG4.GovLength
                if verbose:
                    print(f"      MPEG4 Profile: {vec.MPEG4.Mpeg4Profile}")
                    print(f"      GOP Size: {vec.MPEG4.GovLength}")

        # Audio Source Configuration
        if (
            hasattr(profile, "AudioSourceConfiguration")
            and profile.AudioSourceConfiguration
        ):
            asc = profile.AudioSourceConfiguration
            onvif_profile.asc_name = asc.Name
            onvif_profile.asc_token = asc.token
            onvif_profile.asc_source_token = asc.SourceToken
            if verbose:
                print(f"      Name: {asc.Name}")
                print(f"      Token: {asc.token}")
                print(f"      SourceToken: {asc.SourceToken}")

        # Audio Encoder Configuration
        if (
            hasattr(profile, "AudioEncoderConfiguration")
            and profile.AudioEncoderConfiguration
        ):
            aec = profile.AudioEncoderConfiguration
            onvif_profile.aec_name = aec.Name
            onvif_profile.aec_token = aec.token
            onvif_profile.aec_encoding = aec.Encoding
            if verbose:
                print("    Audio Encoder:")
                print(f"      Name: {aec.Name}")
                print(f"      Token: {aec.token}")
                print(f"      Encoding: {aec.Encoding}")
            if hasattr(aec, "Bitrate"):
                onvif_profile.aec_bitrate = aec.Bitrate
                if verbose:
                    print(f"      Bitrate: {aec.Bitrate}")
            if hasattr(aec, "SampleRate"):
                onvif_profile.aec_sample_rate = aec.SampleRate
                if verbose:
                    print(f"      SampleRate: {aec.SampleRate}")

        # PTZ Configuration
        if hasattr(profile, "PTZConfiguration") and profile.PTZConfiguration:
            ptz = profile.PTZConfiguration
            onvif_profile.ptz_name = ptz.Name
            onvif_profile.ptz_token = ptz.token
            onvif_profile.ptz_node_token = ptz.NodeToken
            if verbose:
                print("    PTZ:")
                print(f"      Name: {ptz.Name}")
                print(f"      Token: {ptz.token}")
                print(f"      NodeToken: {ptz.NodeToken}")

        # Get Stream URI for this profile
        try:
            stream_setup = {
                "Stream": "RTP-Unicast",
                "Transport": {"Protocol": "RTSP"},
            }
            rtsp_uri = media_service.GetStreamUri(
                {"StreamSetup": stream_setup, "ProfileToken": profile.token}
            )
            onvif_profile.rtsp_url = rtsp_uri.Uri
            if verbose:
                print(f"        Stream URI: {rtsp_uri.Uri}")
        except (
            AttributeError,
            KeyError,
            TimeoutError,
            ConnectionError,
        ) as e:
            print(
                f"[WARN] Failed to get Stream URI for profile "
                f"'{profile.Name}': {type(e).__name__} - {e}"
            )
        except Exception as e:  # pylint: disable=broad-exception-caught
            print(
                f"[WARN] Failed to get Stream URI for profile "
                f"'{profile.Name}': {e}"
            )
        if verbose:
            print("  ----------------------- ")

        onvif_profiles.append(onvif_profile)

    return onvif_profiles
