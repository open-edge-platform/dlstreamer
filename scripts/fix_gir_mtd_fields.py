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

It also re-attaches the metadata C functions to their record. For a
record whose name starts with a digit (3DODMtd, C prefix gst_analytics_3d_od_mtd_)
the scanner's name matching fails and the functions are left as namespace-level
free functions. We move those orphaned functions into the record here.
"""

import os
import sys

from defusedxml import ElementTree as ET

GI_CORE_NS = "http://www.gtk.org/introspection/core/1.0"
GI_C_NS = "http://www.gtk.org/introspection/c/1.0"

# Records that are typedefs to struct _GstAnalyticsMtd and need fields
MTD_RECORDS = {"3DODMtd", "ZoneMtd", "TripwireMtd"}


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


def _func_instance_is_record(func, record_name):
    """True if @func's FIRST parameter is of type @record_name.

    These are the record's own methods (gst_analytics_<x>_mtd_get_*), as opposed
    to relation_meta_{add,get}_<x>_mtd, which take the record as a later/out
    parameter and must not be used to infer the record's symbol prefix.
    """
    params = func.find(f"{{{GI_CORE_NS}}}parameters")
    if params is None:
        return False
    first = None
    for param in list(params):
        tag = param.tag.replace(f"{{{GI_CORE_NS}}}", "")
        if tag in ("parameter", "instance-parameter"):
            first = param
            break
    if first is None:
        return False
    type_el = first.find(f"{{{GI_CORE_NS}}}type")
    return type_el is not None and type_el.get("name") == record_name


def _record_symbol_prefix(root, record_name):
    """Infer the C symbol prefix shared by @record_name's functions.

    The record name -> C symbol mapping is ambiguous for digit-prefixed names,
    so we read it off the functions that already reference the record by
    parameter type. Their common c:identifier prefix, cut at the "_mtd_"
    boundary, is the record's symbol prefix, e.g. "gst_analytics_3d_od_mtd_".
    Returns None if no such function exists.
    """
    namespace = root.find(f"{{{GI_CORE_NS}}}namespace")
    if namespace is None:
        return None

    cids = [func.get(f"{{{GI_C_NS}}}identifier")
            for func in namespace.findall(f"{{{GI_CORE_NS}}}function")
            if _func_instance_is_record(func, record_name)]
    cids = [c for c in cids if c]
    if not cids:
        return None

    common = os.path.commonprefix(cids)
    marker = "_mtd_"
    pos = common.find(marker)
    if pos < 0:
        return None
    return common[: pos + len(marker)]


def reattach_functions(root, record):
    """Move namespace-level <function>s belonging to @record into the record.

    g-ir-scanner already does this for most records; digit-prefixed records
    (e.g. 3DODMtd) are missed, leaving the functions as free functions so the
    Python type has no get_mtd_type() etc. We relocate any top-level function
    whose c:identifier starts with the record's symbol prefix and rename it to
    the method-style short name (gst_analytics_3d_od_mtd_get_mtd_type ->
    get_mtd_type).
    """
    namespace = root.find(f"{{{GI_CORE_NS}}}namespace")
    if namespace is None:
        return False

    prefix = _record_symbol_prefix(root, record.get("name"))  # gst_analytics_3d_od_mtd_
    if not prefix:
        return False

    moved = False
    for func in list(namespace.findall(f"{{{GI_CORE_NS}}}function")):
        cid = func.get(f"{{{GI_C_NS}}}identifier")
        if not cid or not cid.startswith(prefix):
            continue
        func.set("name", cid[len(prefix):])
        namespace.remove(func)
        record.append(func)
        moved = True
    return moved


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
            if reattach_functions(root, record):
                modified = True
                print(f"  Re-attached functions to record: {name}")

    if modified:
        ET.indent(tree, space="  ", level=0)
        tree.write(gir_path, xml_declaration=True, encoding="unicode")
        # Add trailing newline
        with open(gir_path, "a") as f:
            f.write("\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
