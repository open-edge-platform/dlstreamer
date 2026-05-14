#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Post-process generated GIR to add struct fields to Mtd record types.

g-ir-scanner cannot extract struct fields for typedefs that alias a struct
from a foreign namespace (e.g. typedef struct _GstAnalyticsMtd GstAnalyticsKeypointMtd).
The upstream GstAnalytics-1.0.gir has 'id' and 'meta' fields for equivalent
types (ClsMtd, ODMtd, TrackingMtd), so we inject the same fields here.

This also removes 'disguised' and 'opaque' attributes since records with
visible fields should not be marked as such.
"""

import sys
import xml.etree.ElementTree as ET

GI_CORE_NS = "http://www.gtk.org/introspection/core/1.0"
GI_C_NS = "http://www.gtk.org/introspection/c/1.0"

# Records that are typedefs to struct _GstAnalyticsMtd and need fields
MTD_RECORDS = {"GroupMtd", "KeypointMtd", "ZoneMtd", "TripwireMtd"}


def make_field(name, doc_text, type_name, c_type):
    """Create a GIR <field> element."""
    field = ET.Element("field", attrib={"name": name, "writable": "1"})
    doc = ET.SubElement(field, "doc", attrib={"xml:space": "preserve"})
    doc.text = doc_text
    type_el = ET.SubElement(
        field,
        "type",
        attrib={"name": type_name, f"{{{GI_C_NS}}}type": c_type},
    )
    return field


def fix_record(record):
    """Remove disguised/opaque and add id+meta fields if missing."""
    # Remove disguised and opaque attributes
    for attr in ("disguised", "opaque"):
        if attr in record.attrib:
            del record.attrib[attr]

    # Check if fields already exist
    existing_fields = record.findall(f"{{{GI_CORE_NS}}}field")
    if existing_fields:
        return False  # Already has fields, skip

    # Find insertion point: after <doc> and <source-position>, before first
    # <method> or <function>
    insert_idx = 0
    for i, child in enumerate(record):
        tag = child.tag.replace(f"{{{GI_CORE_NS}}}", "")
        if tag in ("doc", "source-position"):
            insert_idx = i + 1
        elif tag in ("method", "function"):
            break

    # Insert fields
    meta_field = make_field(
        "meta",
        "Instance of #GstAnalyticsRelationMeta where the analytics-metadata\n"
        "identified by @id is stored.",
        "GstAnalytics.RelationMeta",
        "GstAnalyticsRelationMeta*",
    )
    id_field = make_field("id", "Instance identifier", "guint", "guint")

    record.insert(insert_idx, meta_field)
    record.insert(insert_idx, id_field)
    return True


def main():
    """Parse a GIR file, fix Mtd records and write the result back."""
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <gir-file>", file=sys.stderr)
        sys.exit(1)

    gir_path = sys.argv[1]

    # Register namespaces to preserve them in output
    ET.register_namespace("", GI_CORE_NS)
    ET.register_namespace("c", GI_C_NS)
    ET.register_namespace("glib", "http://www.gtk.org/introspection/glib/1.0")

    tree = ET.parse(gir_path)
    root = tree.getroot()

    modified = False
    for record in root.iter(f"{{{GI_CORE_NS}}}record"):
        name = record.get("name")
        if name in MTD_RECORDS:
            if fix_record(record):
                modified = True
                print(f"  Fixed record: {name}")

    if modified:
        ET.indent(tree, space="  ", level=0)
        tree.write(gir_path, xml_declaration=True, encoding="unicode")
        # Add trailing newline
        with open(gir_path, "a") as f:
            f.write("\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
