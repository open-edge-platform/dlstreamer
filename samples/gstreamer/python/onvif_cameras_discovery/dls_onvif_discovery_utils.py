'''
Utility functions for discovering and managing ONVIF cameras on the network.

This module provides functionality to:
- Discover ONVIF-compliant cameras using WS-Discovery multicast protocol
- Parse and extract camera network information from discovery responses
- Query camera media profiles including video/audio encoder settings and PTZ capabilities
- Retrieve RTSP streaming URLs for camera profiles
- Load and manage camera commands from JSON configuration files
'''
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import xml.etree.ElementTree as ET
from urllib.parse import urlparse
import socket
import time
import re
import json
from typing import Optional, List
from  dls_onvif_data import ONVIFProfile

def get_commandline_by_key(file_path: str, key: str, verbose = False) -> Optional[str]:
    """
    Get command for a given IP from a JSON file.

    Args:
        file_path (str): Path to the JSON file
        key (str): Key in the JSON file, like for example ip address

    Returns:
        Optional[str]: Command for the given IP or None if not found
    """
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            commands = json.load(file)

        command = commands.get(key)
        if command:
            if verbose:
                print(f"Found command for key {key}")
            return command

    except FileNotFoundError:
        if verbose:
            print(f"File {file_path} not found.")
        return None
    except json.JSONDecodeError as e:
        if verbose:
            print(f"JSON parsing error: {e}")
        return None
    except Exception as e: # pylint: disable=broad-exception-caught
        if verbose:
            print(f"Unexpected error: {e}")
        return None

    return None



def extract_xaddrs(xml_string):
    """Find XAddrs in ONVIF discovery response"""

    try:
        # Parse XML
        root = ET.fromstring(xml_string)

        # Namespace for wsdd
        namespaces = {
            'wsdd': 'http://schemas.xmlsoap.org/ws/2005/04/discovery'
        }

        # Find XAddrs
        xaddrs_element = root.find('.//wsdd:XAddrs', namespaces)

        if xaddrs_element is not None:
            return xaddrs_element.text

    except Exception as e: # pylint: disable=broad-exception-caught
        print(f"Error parsing XML: {e}")
        return None
    return None

def parse_xaddrs_url(xaddrs):
    """Parse XAddrs URL into components"""

    parsed = urlparse(xaddrs)

    return {
        'full_url': xaddrs,
        'scheme': parsed.scheme,
        'hostname': parsed.hostname,
        'port': parsed.port,
        'path': parsed.path,
        'base_url': f"{parsed.scheme}://{parsed.netloc}"
    }


def discover_onvif_cameras( verbose: bool = False) -> List[dict]: # pylint: disable=too-many-nested-blocks,too-many-locals
    """Find ONVIF cameras in the local network using WS-Discovery."""

    # Multicast discovery
    MCAST_GRP = '239.255.255.250'   # pylint: disable=invalid-name
    MCAST_PORT = 3702               # pylint: disable=invalid-name
    SOCKET_TIMEOUT = 5              # pylint: disable=invalid-name

    # WS-Discovery Probe message
    probe_message = '''<?xml version="1.0" encoding="UTF-8"?>
    <soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope" 
                   xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing" 
                   xmlns:tns="http://schemas.xmlsoap.org/ws/2005/04/discovery">
        <soap:Header>
            <wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>
            <wsa:MessageID>uuid:probe-message</wsa:MessageID>
            <wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>
        </soap:Header>
        <soap:Body>
            <tns:Probe>
                <tns:Types>dn:NetworkVideoTransmitter</tns:Types>
            </tns:Probe>
        </soap:Body>
    </soap:Envelope>'''

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(SOCKET_TIMEOUT)

    try: # pylint: disable=too-many-nested-blocks
        cameras = []

        # Send probe
        sock.sendto(probe_message.encode(), (MCAST_GRP, MCAST_PORT))

        # Listen for responses from multiple cameras
        # WS-Discovery multicast responses come from different cameras on the network
        # We need to collect all responses within the timeout window
        start_time = time.time()
        while time.time() - start_time < SOCKET_TIMEOUT:  # time to collect all camera responses
            try:
                # Adjust remaining timeout for the socket
                remaining_time = SOCKET_TIMEOUT - (time.time() - start_time)
                if remaining_time <= 0:
                    break
                sock.settimeout(remaining_time)

                data, addr = sock.recvfrom(4096)
                if verbose:
                    print(f"Response from {addr}" )
                    print(f"Data: {data.decode('utf-8', errors='ignore')}" )

                response = data.decode('utf-8', errors='ignore')
                extracted_xaddr = extract_xaddrs(response)

                parsed_url = parse_xaddrs_url(extracted_xaddr)

                if verbose:
                    print("Parsed XAddrs URL:")
                for key, value in parsed_url.items():
                    print(f"{key}: {value}")

                if verbose:
                    print("=====================")
                if 'ProbeMatches' in response and 'XAddrs' in response:
                    # Extract IP address
                    ip_match = re.search(r'http://([0-9.]+)', response)
                    if ip_match:
                        ip = ip_match.group(1)
                        json_output = json.dumps({
                        "hostname": ip,
                        "port": parsed_url['port']
                        })
                        camera_dict = {"hostname": ip, "port": parsed_url['port']}
                        cameras.append(camera_dict)
                        if verbose:
                            print(json_output)
            except socket.timeout:
                # Timeout means no more responses are coming, exit the loop
                break

        if verbose:
            print(f"Discovery complete. Found {len(cameras)} camera(s).")
        return cameras
    finally:
        sock.close()

def camera_profiles(client, verbose = False): # pylint: disable=too-many-statements, too-many-locals, too-many-branches
    """
    This function queries an ONVIF camera for its available media profiles and extracts
    detailed configuration information including video encoder settings, audio configurations,
    PTZ capabilities, and RTSP streaming URIs.

    Args:
        client: An ONVIF client instance used to communicate with the camera device.
        verbose (bool, optional): If True, prints detailed profile information to stdout.
            Defaults to False.

    Returns:
        List[ONVIFProfile]: A list of ONVIFProfile objects containing the extracted profile
            information. Each profile includes:
            - Basic profile information (name, token, fixed status)
            - Video source configuration (name, token, source token, bounds)
            - Video encoder settings (resolution, quality, bitrate, framerate, codec details)
            - Audio source and encoder configurations (if available)
            - PTZ configuration (if available)
            - RTSP stream URI

    Raises:
        Exception: May raise exceptions related to ONVIF service communication failures,
            particularly when retrieving stream URIs.
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
        if hasattr(profile, 'fixed') and profile.fixed is not None:
            onvif_profile.fixed = profile.fixed

        # Video Source Configuration
        if hasattr(profile, 'VideoSourceConfiguration') and profile.VideoSourceConfiguration:
            vsc = profile.VideoSourceConfiguration
            onvif_profile.vsc_name = vsc.Name
            onvif_profile.vsc_token = vsc.token
            onvif_profile.vsc_source_token = vsc.SourceToken
            if hasattr(vsc, 'Bounds') and vsc.Bounds:
                onvif_profile.vsc_bounds = {
                    'x': vsc.Bounds.x,
                    'y': vsc.Bounds.y,
                    'width': vsc.Bounds.width,
                    'height': vsc.Bounds.height
                }

        # Video Encoder Configuration
        if hasattr(profile, 'VideoEncoderConfiguration') and profile.VideoEncoderConfiguration:
            vec = profile.VideoEncoderConfiguration
            onvif_profile.vec_name = vec.Name
            onvif_profile.vec_token = vec.token
            onvif_profile.vec_encoding = vec.Encoding
            if verbose:
                print("    Video Encoder:")
                print(f"      Name: {vec.Name}")
                print(f"      Token: {vec.token}")
                print(f"      Encoding: {vec.Encoding}")
            if hasattr(vec, 'Resolution') and vec.Resolution:
                onvif_profile.vec_resolution = {
                    'width': vec.Resolution.Width,
                    'height': vec.Resolution.Height}
                if verbose:
                    print(f"      Resolution: {vec.Resolution.Width}x{vec.Resolution.Height}")
            if hasattr(vec, 'Quality'):
                onvif_profile.vec_quality = vec.Quality
                if verbose:
                    print(f"      Quality: {vec.Quality}")
            if hasattr(vec, 'RateControl') and vec.RateControl:
                onvif_profile.vec_framerate_limit = vec.RateControl.FrameRateLimit
                onvif_profile.vec_bitrate_limit = vec.RateControl.BitrateLimit
                if verbose:
                    print(f"      FrameRate Limit: {vec.RateControl.FrameRateLimit}")
                    print(f"      Bitrate Limit: {vec.RateControl.BitrateLimit}")
                if hasattr(vec.RateControl, 'EncodingInterval'):
                    onvif_profile.vec_encoding_interval = vec.RateControl.EncodingInterval
                    if verbose:
                        print(f"      Encoding Interval: {vec.RateControl.EncodingInterval}")
            if hasattr(vec, 'H264') and vec.H264:
                onvif_profile.vec_h264_profile = vec.H264.H264Profile
                onvif_profile.vec_h264_gop_length = vec.H264.GovLength
                if verbose:
                    print(f"      H264 Profile: {vec.H264.H264Profile}")
                    print(f"      GOP Size: {vec.H264.GovLength}")
            elif hasattr(vec, 'MPEG4') and vec.MPEG4:
                onvif_profile.vec_mpeg4_profile = vec.MPEG4.Mpeg4Profile
                onvif_profile.vec_mpeg4_gop_length = vec.MPEG4.Gov
                if verbose:
                    print(f"      MPEG4 Profile: {vec.MPEG4.Mpeg4Profile}")
                    print(f"      GOP Size: {vec.MPEG4.GovLength}")

        # Audio Source Configuration
        if hasattr(profile, 'AudioSourceConfiguration') and profile.AudioSourceConfiguration:
            asc = profile.AudioSourceConfiguration
            onvif_profile.asc_name = asc.Name
            onvif_profile.asc_token = asc.token
            onvif_profile.asc_source_token = asc.SourceToken
            if verbose:
                print(f"      Name: {asc.Name}")
                print(f"      Token: {asc.token}")
                print(f"      SourceToken: {asc.SourceToken}")

        # Audio Encoder Configuration
        if hasattr(profile, 'AudioEncoderConfiguration') and profile.AudioEncoderConfiguration:
            aec = profile.AudioEncoderConfiguration
            onvif_profile.aec_name = aec.Name
            onvif_profile.aec_token = aec.token
            onvif_profile.aec_encoding = aec.Encoding
            if verbose:
                print("    Audio Encoder:")
                print(f"      Name: {aec.Name}")
                print(f"      Token: {aec.token}")
                print(f"      Encoding: {aec.Encoding}")
            if hasattr(aec, 'Bitrate'):
                onvif_profile.aec_bitrate = aec.Bitrate
                if verbose:
                    print(f"      Bitrate: {aec.Bitrate}")
            if hasattr(aec, 'SampleRate'):
                onvif_profile.aec_sample_rate = aec.SampleRate
                if verbose:
                    print(f"      SampleRate: {aec.SampleRate}")

        # PTZ Configuration
        if hasattr(profile, 'PTZConfiguration') and profile.PTZConfiguration:
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
            stream_setup = {'Stream': 'RTP-Unicast', 'Transport': {'Protocol': 'RTSP'}}
            rtsp_uri = media_service.GetStreamUri({'StreamSetup': stream_setup,
                                'ProfileToken': profile.token})
            onvif_profile.rtsp_url = rtsp_uri.Uri
            if verbose:
                print(f"        Stream URI: {rtsp_uri.Uri}")
        except AttributeError as e:
            # Profile or media service missing expected attributes
            if verbose:
                print(f"    Stream URI: AttributeError - {e}")
        except KeyError as e:
            # Missing required keys in stream setup or response
            if verbose:
                print(f"    Stream URI: KeyError - {e}")
        except TimeoutError as e:
            # Network timeout when contacting camera
            if verbose:
                print(f"    Stream URI: TimeoutError - {e}")
        except ConnectionError as e:
            # Connection issues with the camera
            if verbose:
                print(f"    Stream URI: ConnectionError - {e}")
        except Exception as e:  # pylint: disable=broad-exception-caught
            if verbose:
                print(f"    Stream URI: Error - {e}")
        if verbose:
            print("  ----------------------- ")

        onvif_profiles.append(onvif_profile)

    return onvif_profiles
