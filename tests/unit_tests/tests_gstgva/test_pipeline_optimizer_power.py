# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest
from unittest import mock

import requests

from optimizer import DLSOptimizer, _collect_power_info # pylint: disable=no-name-in-module

# Power metrics captured from the Metrics Manager 2026.3.0 Prometheus endpoint (port 9273).
METRICS_MANAGER = """\
# HELP gpu_power_power_w Telegraf collected metric
# TYPE gpu_power_power_w untyped
gpu_power_power_w{driver="i915",gpu_id="0",host="host",type="gpu_cur_power"} 0 1794560841854
gpu_power_power_w{driver="i915",gpu_id="0",host="host",type="pkg_cur_power"} 5.25 1794560841854
# HELP npu_frequency_hz Telegraf collected metric
# TYPE npu_frequency_hz untyped
npu_frequency_hz{host="host"} 0 1794560841987
# HELP npu_power_state Telegraf collected metric
# TYPE npu_power_state untyped
npu_power_state{host="host"} 3 1794560841987
# HELP npu_power_w Telegraf collected metric
# TYPE npu_power_w untyped
npu_power_w{host="host"} 0.05 1794560841987
"""

TWO_GPUS = """\
# TYPE gpu_power_power_w untyped
gpu_power_power_w{gpu_id="0",type="gpu_cur_power"} 1.5
gpu_power_power_w{gpu_id="0",type="pkg_cur_power"} 4.5
gpu_power_power_w{gpu_id="1",type="gpu_cur_power"} 2.5
gpu_power_power_w{gpu_id="1",type="pkg_cur_power"} 10.25
"""

NO_POWER_METRICS = """\
# TYPE npu_frequency_hz untyped
npu_frequency_hz{host="host"} 0
"""


def _response(text):
    resp = mock.Mock()
    resp.text = text
    return resp


class TestCollectPowerInfo(unittest.TestCase):

    def test_sums_npu_power_and_gpu_package_power(self):
        self.assertAlmostEqual(_collect_power_info(_response(METRICS_MANAGER)), 5.25 + 0.05)

    def test_sums_package_power_of_every_gpu(self):
        self.assertAlmostEqual(_collect_power_info(_response(TWO_GPUS)), 4.5 + 10.25)

    def test_no_power_metrics_reports_zero(self):
        self.assertEqual(_collect_power_info(_response(NO_POWER_METRICS)), 0.0)


class TestSetMetricsUrl(unittest.TestCase):

    def setUp(self):
        self.optimizer = DLSOptimizer()

    def _set_metrics_url(self, text):
        with mock.patch("optimizer.requests.get", return_value=_response(text)) as get:
            self.optimizer.set_metrics_url("http://metrics-manager:9273/")
        get.assert_called_once_with("http://metrics-manager:9273/metrics", timeout=1)

    def test_accepts_metrics_manager_endpoint(self):
        self._set_metrics_url(METRICS_MANAGER)
        self.assertEqual(self.optimizer._metrics_url, "http://metrics-manager:9273/metrics") # pylint: disable=protected-access

    def test_rejects_endpoint_without_power_metrics(self):
        with self.assertRaisesRegex(RuntimeError, "doesn't expose"):
            self._set_metrics_url(NO_POWER_METRICS)

    def test_rejects_empty_response(self):
        with self.assertRaisesRegex(RuntimeError, "doesn't expose"):
            self._set_metrics_url("\n")

    def test_rejects_invalid_prometheus_text(self):
        with self.assertRaisesRegex(RuntimeError, "invalid Prometheus metrics"):
            self._set_metrics_url("<html><body>Not Found</body></html>\n")

    def test_rejects_unreachable_endpoint(self):
        with mock.patch("optimizer.requests.get", side_effect=requests.ConnectionError("refused")):
            with self.assertRaisesRegex(RuntimeError, "Couldn't establish connection"):
                self.optimizer.set_metrics_url("http://metrics-manager:9273/")


if __name__ == "__main__":
    unittest.main()
