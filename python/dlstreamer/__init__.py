# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Explicit subpackage import so that static analysers (e.g. pylint E0611) can
# resolve `from dlstreamer.onvif import ...` without runtime installation.
from . import onvif  # noqa: F401
