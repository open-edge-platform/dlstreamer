# ==============================================================================
# Copyright (C) 2023-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# run this script to generate rst file with dlstreamer elements(description, caps, pads)
#   args: file name to save generated doc
# ==============================================================================

#!/bin/python3
import subprocess
import re
import sys
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst # pylint: disable=no-name-in-module, wrong-import-position

import inspect

def get_elements(lib):
    elements = []

    result = subprocess.Popen(['gst-inspect-1.0', lib], stdout=subprocess.PIPE)
    while True:
        line = result.stdout.readline()
        if not line:
            break
        line = line.decode('utf-8').rstrip('\n')
        match = re.match(r"^\s+([^\s]\w+):", str(line))
        if match:
            elements.append(match.group(1))

    return elements

def get_element_description(element_name):
    factory = Gst.ElementFactory.find(element_name)
    return factory.get_metadata("description")

def get_field_info(field, value, pfx):
    str = Gst.value_serialize(value)
    return "{0:s}  {1:15s}: {2:s}".format(
        pfx, GLib.quark_to_string(field), str)

def get_caps_info(caps):
    if not caps:
        return

    if caps.is_any():
        return ["  - ANY"]

    if caps.is_empty():
        return ["  - EMPTY"]

    caps_info = []
    for i in range(caps.get_size()):
        structure = caps.get_structure(i)
        caps_info.append("  - {}".format(structure.get_name()))
        caps_info.append("   ")
        for j in range(structure.n_fields()):
            fieldname = structure.nth_field_name(j)
            value = Gst.value_serialize(structure.get_value(fieldname))
            caps_info.append("    - {}: {}".format(fieldname, value))

    return caps_info

def generate_table(table_data, table_header = ["", ""]):
    table = ""

    first_collumn_length = len(table_header[0])
    second_collumn_length = len(table_header[1])

    for first_column in table_data:
        first_collumn_length = len(first_column) if len(first_column) > first_collumn_length else first_collumn_length

        for second_column in table_data[first_column]:
            second_collumn_length = len(second_column) if len(second_column) > second_collumn_length else second_collumn_length

    if table_header[0]:
        table = "=" * first_collumn_length + "      " + "=" * second_collumn_length + "\n"
        table += table_header[0] + " " * (first_collumn_length - len(table_header[0])) + "      " + table_header[1] + "\n"

    table += "=" * first_collumn_length + "      " + "=" * second_collumn_length + "\n"

    for first_column in table_data:
        table += first_column + " " * (first_collumn_length - len(first_column)) + "      "
        table += table_data[first_column][0] + "\n"

        i = 0
        for second_column_line in table_data[first_column]: 
            i += 1
            if i == 1:
                continue

            table += " " * first_collumn_length + "      " + second_column_line + "\n"

    
    table += "=" * first_collumn_length + "      " + "=" * second_collumn_length + "\n"
    return table

def get_pad_templates_information(factory):
    
    if factory.get_num_pad_templates() == 0:
        return None

    pad_templates_info = "\nPad templates\r\n**********************\r\n"

    table = {}

    pads = factory.get_static_pad_templates()
    for pad in pads:
        padtemplate = pad.get()

        pad_direction = ""
        if pad.direction == Gst.PadDirection.SRC:
            pad_direction = "SRC template: {}".format(padtemplate.name_template)
        elif pad.direction == Gst.PadDirection.SINK:
           pad_direction = "SINK template: {}".format(padtemplate.name_template)
        else:
            pad_direction = "UNKNOWN template: {}".format(padtemplate.name_template)

        table[pad_direction] = []

        if padtemplate.presence == Gst.PadPresence.ALWAYS:
            table[pad_direction].append("- Availability: Always")
        elif padtemplate.presence == Gst.PadPresence.SOMETIMES:
            table[pad_direction].append("- Availability: Sometimes")
        elif padtemplate.presence == Gst.PadPresence.REQUEST:
            table[pad_direction].append("- Availability: On request")
        else:
            table[pad_direction].append("- Availability: UNKNOWN")

        if padtemplate.get_caps():
            table[pad_direction].append("- Capabilities:")
            table[pad_direction].append("")
            caps_info = get_caps_info(padtemplate.get_caps())

            for cap_info in caps_info:
                table[pad_direction].append(cap_info)

    return "\nCapabilities\r\n**********************\r\n" + generate_table(table) + "\n"

def get_properties_info(element):

    table = {}

    for prop in element.list_properties():
        table[prop.name] = []

        for line in prop.blurb.split("\n"):
            table[prop.name].append(line + "\n")

        default_value = str(prop.default_value).replace("__main__.", "")
        if not default_value:
            default_value = "\"\""

        table[prop.name].append("*Default: {}*\n".format(default_value))

    return "\nProperties\r\n**********************\r\n" + generate_table(table, ["Name", "Description"]) + "\n"

def generate_elements_page(page_file_name):
    page = "------------\nElements 2.0\n------------\n\n"
    
    libraries = ["dlstreamer_bins", "dlstreamer_elements", "dlstreamer_cpu", "dlstreamer_openvino", "dlstreamer_opencv", "dlstreamer_vaapi", "dlstreamer_opencl", "dlstreamer_sycl", "python"]

    for lib in libraries:
        elements = get_elements(lib)

        for element in elements:
            page += str(element) + "\n" + "#" * (len(element) + 3) + "\n\n"

            page += get_element_description(element) + "\r\n"

            factory = Gst.ElementFactory.find(element)
            el = factory.make(element)

            pad_templates = get_pad_templates_information(factory)

            if pad_templates:
                page += pad_templates + "\r\n"

            page += get_properties_info(el)

    elements_list_file = open(page_file_name, "w")
    elements_list_file.write(page)
    elements_list_file.close()

            
if __name__ == "__main__":
    Gst.init(sys.argv)

    page_file_name = "elements_page.rst"

    if len(sys.argv) > 1 and sys.argv[1]:
        page_file_name = sys.argv[1]


    generate_elements_page(page_file_name)
