################################################################################
#  Copyright (C) 2026 Intel Corporation
#
#  SPDX-License-Identifier: MIT
################################################################################

from onvif import ONVIFCamera
import xml.etree.ElementTree as ET
from urllib.parse import urlparse
import socket
import time
import re
import json
from typing import Optional, Dict, List
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
        else:
            if verbose:
                print(f"No command found for key {key}")
            return None

    except FileNotFoundError:
        if verbose:
            print(f"File {file_path} not found.")
        return None
    except json.JSONDecodeError as e:
        if verbose:
            print(f"JSON parsing error: {e}")
        return None
    except Exception as e:
        if verbose:
            print(f"Unexpected error: {e}")
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
        else:
            return None

    except Exception as e:
        print(f"Error parsing XML: {e}")
        return None

def parse_xaddrs_url(xaddrs):
    """Parsuj URL XAddrs na komponenty"""

    parsed = urlparse(xaddrs)

    return {
        'full_url': xaddrs,
        'scheme': parsed.scheme,
        'hostname': parsed.hostname,
        'port': parsed.port,
        'path': parsed.path,
        'base_url': f"{parsed.scheme}://{parsed.netloc}"
    }


def discover_onvif_cameras():
    """Find ONVIF cameras in the local network using WS-Discovery."""

    # Multicast discovery
    MCAST_GRP = '239.255.255.250'
    MCAST_PORT = 3702

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
    sock.settimeout(5)

    try:
        cameras = []

        # Send probe
        sock.sendto(probe_message.encode(), (MCAST_GRP, MCAST_PORT))

        # Listen for responses
        start_time = time.time()
        while time.time() - start_time < 5:  # 5 seconds to respond
            try:
                data, addr = sock.recvfrom(4096)
                print(f"Response from {addr}" )
                print(f"Data: {data.decode('utf-8', errors='ignore')}" )

                response = data.decode('utf-8', errors='ignore')
                extractedXaddr = extract_xaddrs(response)

                parsed_url = parse_xaddrs_url(extractedXaddr)

                print("=== Parsed XAddrs ===")
                for key, value in parsed_url.items():
                    print(f"{key}: {value}")

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
                        camera_dict = {"hostname": ip, "port": parsed_url['port']
                    }
                        #cameras.append(json_output)
                        cameras.append(camera_dict)
                        print(json_output)

            except socket.timeout:
                continue

        return cameras

    finally:
        sock.close()



def camera_profiles(client, verbose = False):
    """Function lists media profiles of the ONVIF device."""
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
            if verbose:
                print(f"    Fixed: {profile.fixed}")

        # Video Source Configuration
        if hasattr(profile, 'VideoSourceConfiguration') and profile.VideoSourceConfiguration:
            vsc = profile.VideoSourceConfiguration
            if verbose:
                print(f"    Video Source:")
                print(f"      Name: {vsc.Name}")
                print(f"      Token: {vsc.token}")
                print(f"      SourceToken: {vsc.SourceToken}")
            if hasattr(vsc, 'Bounds') and vsc.Bounds:
                if verbose:
                    print(f"      Bounds: {vsc.Bounds.x}x{vsc.Bounds.y} "
                          f"{vsc.Bounds.width}x{vsc.Bounds.height}")

        # Video Encoder Configuration
        if hasattr(profile, 'VideoEncoderConfiguration') and profile.VideoEncoderConfiguration:
            vec = profile.VideoEncoderConfiguration
            if verbose:
                print(f"    Video Encoder:")
                print(f"      Name: {vec.Name}")
                print(f"      Token: {vec.token}")
                print(f"      Encoding: {vec.Encoding}")
            if hasattr(vec, 'Resolution') and vec.Resolution:
                if verbose:
                    print(f"      Resolution: {vec.Resolution.Width}x{vec.Resolution.Height}")
            if hasattr(vec, 'Quality'):
                if verbose:
                    print(f"      Quality: {vec.Quality}")
            if hasattr(vec, 'RateControl') and vec.RateControl:
                if verbose:
                    print(f"      FrameRate Limit: {vec.RateControl.FrameRateLimit}")
                    print(f"      Bitrate Limit: {vec.RateControl.BitrateLimit}")
                if hasattr(vec.RateControl, 'EncodingInterval'):
                    if verbose:
                        print(f"      Encoding Interval: {vec.RateControl.EncodingInterval}")
            if hasattr(vec, 'H264') and vec.H264:
                if verbose:
                    print(f"      H264 Profile: {vec.H264.H264Profile}")
                    print(f"      GOP Size: {vec.H264.GovLength}")
            elif hasattr(vec, 'MPEG4') and vec.MPEG4:
                if verbose: 
                    print(f"      MPEG4 Profile: {vec.MPEG4.Mpeg4Profile}")
                    print(f"      GOP Size: {vec.MPEG4.GovLength}")
        
        # Audio Source Configuration
        if hasattr(profile, 'AudioSourceConfiguration') and profile.AudioSourceConfiguration:
            asc = profile.AudioSourceConfiguration
            if verbose:
                print(f"      Name: {asc.Name}")
                print(f"      Token: {asc.token}")
                print(f"      SourceToken: {asc.SourceToken}")

        # Audio Encoder Configuration
        if hasattr(profile, 'AudioEncoderConfiguration') and profile.AudioEncoderConfiguration:
            aec = profile.AudioEncoderConfiguration
            if verbose:
                print(f"    Audio Encoder:")
                print(f"      Name: {aec.Name}")
                print(f"      Token: {aec.token}")
                print(f"      Encoding: {aec.Encoding}")
            if hasattr(aec, 'Bitrate'):
                if verbose:
                    print(f"      Bitrate: {aec.Bitrate}")
            if hasattr(aec, 'SampleRate'):
                if verbose:
                    print(f"      SampleRate: {aec.SampleRate}")

        # PTZ Configuration
        if hasattr(profile, 'PTZConfiguration') and profile.PTZConfiguration:
            ptz = profile.PTZConfiguration
            profile._ptz_name = ptz.Name
            profile._ptz_token = ptz.token
            profile._ptz_node_token = ptz.NodeToken
            if verbose:
                print(f"    PTZ:")
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
        except Exception as e:
            if verbose:
                print(f"    Stream URI: Error - {e}")

        if verbose:
            print("  ----------------------- ")

        onvif_profiles.append(onvif_profile)

    return onvif_profiles



def camera_capabilities(client, verbose = False):
    """Function lists capabilities of the ONVIF device."""
    capabilities = client.devicemgmt.GetCapabilities({'Category': 'All'})

    # Analytics
    if hasattr(capabilities, 'Analytics') and capabilities.Analytics:
        if verbose:
            print(f"  Analytics:")
            print(f"    XAddr: {capabilities.Analytics.XAddr}")
            print(f"    RuleSupport: {capabilities.Analytics.RuleSupport}")
            print(f"    AnalyticsModuleSupport: {capabilities.Analytics.AnalyticsModuleSupport}")

    # Device
    if hasattr(capabilities, 'Device') and capabilities.Device:
        if verbose:
            print(f"  Device:")
            print(f"    XAddr: {capabilities.Device.XAddr}")
        if hasattr(capabilities.Device, 'Network'):
            if verbose:
                print(f"    Network: {capabilities.Device.Network}")
        if hasattr(capabilities.Device, 'System'):
            if verbose:
                print(f"    System: {capabilities.Device.System}")
        if hasattr(capabilities.Device, 'IO'):
            if verbose:
                print(f"    IO: {capabilities.Device.IO}")
        if hasattr(capabilities.Device, 'Security'):
            if verbose:
                print(f"    Security: {capabilities.Device.Security}")

    # Events
    if hasattr(capabilities, 'Events') and capabilities.Events:
        if verbose:
            print(f"  Events:")
            print(f"    XAddr: {capabilities.Events.XAddr}")
        if hasattr(capabilities.Events, 'WSSubscriptionPolicySupport'):
            if verbose:
                print(f"    WSSubscriptionPolicySupport: "
                      f"{capabilities.Events.WSSubscriptionPolicySupport}")
        if hasattr(capabilities.Events, 'WSPullPointSupport'):
            if verbose:
                print(f"    WSPullPointSupport: {capabilities.Events.WSPullPointSupport}")
        if hasattr(capabilities.Events, 'WSPausableSubscriptionManagerInterfaceSupport'):
            if verbose:
                print(f"    WSPausableSubscriptionManagerInterfaceSupport: "
                      f"{capabilities.Events.WSPausableSubscriptionManagerInterfaceSupport}")

    # Imaging
    if hasattr(capabilities, 'Imaging') and capabilities.Imaging:
        if verbose:
            print(f"  Imaging:")
            print(f"    XAddr: {capabilities.Imaging.XAddr}")

    # Media
    if hasattr(capabilities, 'Media') and capabilities.Media:
        if verbose:
            print(f"  Media:")
        print(f"    XAddr: {capabilities.Media.XAddr}")
        if hasattr(capabilities.Media, 'StreamingCapabilities'):
            if verbose:
                print(f"    StreamingCapabilities: {capabilities.Media.StreamingCapabilities}")

    # PTZ
    if hasattr(capabilities, 'PTZ') and capabilities.PTZ:
        if verbose:
            print(f"  PTZ:")
            print(f"    XAddr: {capabilities.PTZ.XAddr}")

    # Extension
    if hasattr(capabilities, 'Extension') and capabilities.Extension:
        if verbose:
            print(f"  Extension: {capabilities.Extension}")

    if verbose:
        print("  ----------------------- ")
