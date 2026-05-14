# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Unit tests for DLStreamerMeta.ZoneMtd and TripwireMtd Python bindings."""

import sys
import unittest

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstAnalytics', '1.0')
gi.require_version('DLStreamerMeta', '1.0')

from gi.repository import Gst, GstAnalytics, DLStreamerMeta  # pylint: disable=no-name-in-module

# Import gstgva.region_of_interest to trigger _wrap_mtd() registration
# which makes type() return proper DLStreamerMeta types during iteration
from gstgva.region_of_interest import RegionOfInterest  # noqa: F401

Gst.init(sys.argv)


class ZoneMtdTestCase(unittest.TestCase):
    """Tests for DLStreamerMeta.ZoneMtd Python bindings."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

    def test_add_zone_mtd(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'zone-A')
        self.assertTrue(ok)

    def test_get_info_round_trip(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'entrance')
        self.assertTrue(ok)

        ok, zone_id = zone.get_info()
        self.assertTrue(ok)
        self.assertEqual(zone_id, 'entrance')

    def test_get_info_empty_id(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, '')
        self.assertTrue(ok)

        ok, zone_id = zone.get_info()
        self.assertTrue(ok)
        self.assertEqual(zone_id, '')

    def test_get_info_unicode_id(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'зона-1')
        self.assertTrue(ok)

        ok, zone_id = zone.get_info()
        self.assertTrue(ok)
        self.assertEqual(zone_id, 'зона-1')

    def test_multiple_zones(self):
        names = ['zone-A', 'zone-B', 'zone-C']
        zones = []
        for name in names:
            ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, name)
            self.assertTrue(ok)
            zones.append(zone)

        for zone, expected_name in zip(zones, names):
            ok, zone_id = zone.get_info()
            self.assertTrue(ok)
            self.assertEqual(zone_id, expected_name)

    def test_get_mtd_type_via_function(self):
        zone_type = DLStreamerMeta.zone_mtd_get_mtd_type()
        self.assertIsNotNone(zone_type)

    def test_get_mtd_type_via_class(self):
        zone_type = DLStreamerMeta.ZoneMtd.get_mtd_type()
        self.assertIsNotNone(zone_type)
        self.assertEqual(zone_type, DLStreamerMeta.zone_mtd_get_mtd_type())

    def test_relation_meta_get_zone_mtd(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'restricted')
        self.assertTrue(ok)

        ok, zone2 = DLStreamerMeta.relation_meta_get_zone_mtd(self.rmeta, zone.id)
        self.assertTrue(ok)

        ok, zone_id = zone2.get_info()
        self.assertTrue(ok)
        self.assertEqual(zone_id, 'restricted')

    def test_type_distinct_from_tripwire(self):
        zone_type = DLStreamerMeta.zone_mtd_get_mtd_type()
        tripwire_type = DLStreamerMeta.tripwire_mtd_get_mtd_type()
        self.assertNotEqual(zone_type, tripwire_type)


class TripwireMtdTestCase(unittest.TestCase):
    """Tests for DLStreamerMeta.TripwireMtd Python bindings."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

    def test_add_tripwire_mtd(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'line-1', 1)
        self.assertTrue(ok)

    def test_get_info_direction_forward(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'entry', 1)
        self.assertTrue(ok)

        ok, tw_id, direction = tw.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'entry')
        self.assertEqual(direction, 1)

    def test_get_info_direction_backward(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'exit', -1)
        self.assertTrue(ok)

        ok, tw_id, direction = tw.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'exit')
        self.assertEqual(direction, -1)

    def test_get_info_direction_undefined(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'boundary', 0)
        self.assertTrue(ok)

        ok, tw_id, direction = tw.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'boundary')
        self.assertEqual(direction, 0)

    def test_id_preserved(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'tw-42', 1)
        self.assertTrue(ok)

        ok, tw_id, _ = tw.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'tw-42')

    def test_multiple_tripwires(self):
        entries = [('line-N', 1), ('line-S', -1), ('line-E', 0)]
        wires = []
        for tw_id, direction in entries:
            ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, tw_id, direction)
            self.assertTrue(ok)
            wires.append(tw)

        for tw, (expected_id, expected_dir) in zip(wires, entries):
            ok, tw_id, direction = tw.get_info()
            self.assertTrue(ok)
            self.assertEqual(tw_id, expected_id)
            self.assertEqual(direction, expected_dir)

    def test_get_mtd_type_via_function(self):
        tw_type = DLStreamerMeta.tripwire_mtd_get_mtd_type()
        self.assertIsNotNone(tw_type)

    def test_get_mtd_type_via_class(self):
        tw_type = DLStreamerMeta.TripwireMtd.get_mtd_type()
        self.assertIsNotNone(tw_type)
        self.assertEqual(tw_type, DLStreamerMeta.tripwire_mtd_get_mtd_type())

    def test_relation_meta_get_tripwire_mtd(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'door', -1)
        self.assertTrue(ok)

        ok, tw2 = DLStreamerMeta.relation_meta_get_tripwire_mtd(self.rmeta, tw.id)
        self.assertTrue(ok)

        ok, tw_id, direction = tw2.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'door')
        self.assertEqual(direction, -1)


class ZoneAndTripwireCoexistenceTestCase(unittest.TestCase):
    """Tests for ZoneMtd and TripwireMtd coexisting on the same buffer."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

    def test_both_on_same_buffer(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'zone-1')
        self.assertTrue(ok)

        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'line-1', 1)
        self.assertTrue(ok)

        ok, zone_id = zone.get_info()
        self.assertTrue(ok)
        self.assertEqual(zone_id, 'zone-1')

        ok, tw_id, direction = tw.get_info()
        self.assertTrue(ok)
        self.assertEqual(tw_id, 'line-1')
        self.assertEqual(direction, 1)

    def test_multiple_of_each(self):
        zone_names = ['zone-A', 'zone-B']
        tw_entries = [('line-N', 1), ('line-S', -1)]

        zones = []
        for name in zone_names:
            ok, z = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, name)
            self.assertTrue(ok)
            zones.append(z)

        wires = []
        for tw_id, direction in tw_entries:
            ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, tw_id, direction)
            self.assertTrue(ok)
            wires.append(tw)

        for z, expected_name in zip(zones, zone_names):
            ok, zone_id = z.get_info()
            self.assertTrue(ok)
            self.assertEqual(zone_id, expected_name)

        for tw, (expected_id, expected_dir) in zip(wires, tw_entries):
            ok, tw_id, direction = tw.get_info()
            self.assertTrue(ok)
            self.assertEqual(tw_id, expected_id)
            self.assertEqual(direction, expected_dir)

    def test_distinct_type_ids(self):
        zone_type = DLStreamerMeta.zone_mtd_get_mtd_type()
        tripwire_type = DLStreamerMeta.tripwire_mtd_get_mtd_type()
        self.assertNotEqual(zone_type, tripwire_type)


class WrapMtdTypeTestCase(unittest.TestCase):
    """Tests that _wrap_mtd registration makes type() return correct types
    when iterating over RelationMeta."""

    def setUp(self):
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)
        self.rmeta = GstAnalytics.buffer_add_analytics_relation_meta(self.buffer)

    def test_zone_type_during_iteration(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'zone-iter')
        self.assertTrue(ok)

        found = False
        for mtd in self.rmeta:
            if mtd.id == zone.id:
                self.assertIsInstance(mtd, DLStreamerMeta.ZoneMtd)
                found = True
                break
        self.assertTrue(found, "ZoneMtd not found during iteration")

    def test_tripwire_type_during_iteration(self):
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'tw-iter', 1)
        self.assertTrue(ok)

        found = False
        for mtd in self.rmeta:
            if mtd.id == tw.id:
                self.assertIsInstance(mtd, DLStreamerMeta.TripwireMtd)
                found = True
                break
        self.assertTrue(found, "TripwireMtd not found during iteration")

    def test_mixed_types_during_iteration(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'zone-mix')
        self.assertTrue(ok)
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 'tw-mix', -1)
        self.assertTrue(ok)

        types_found = set()
        for mtd in self.rmeta:
            types_found.add(type(mtd))

        self.assertIn(DLStreamerMeta.ZoneMtd, types_found)
        self.assertIn(DLStreamerMeta.TripwireMtd, types_found)

    def test_zone_and_tripwire_ids_are_distinct(self):
        ok, zone = DLStreamerMeta.relation_meta_add_zone_mtd(self.rmeta, 'z')
        self.assertTrue(ok)
        ok, tw = DLStreamerMeta.relation_meta_add_tripwire_mtd(self.rmeta, 't', 1)
        self.assertTrue(ok)

        self.assertNotEqual(zone.id, tw.id)


if __name__ == '__main__':
    unittest.main()
