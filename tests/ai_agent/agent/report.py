"""Render the agent run into results.json and a browsable HTML report."""
from __future__ import annotations

import html
import json
from dataclasses import asdict
from pathlib import Path
from typing import Any

from .models import Verdict

_CATEGORY_COLORS = {
    "pass": "#2e7d32",
    "user-error": "#757575",
    "docs-bug": "#ef6c00",
    "product-bug": "#c62828",
    "flaky": "#6a1b9a",
}


def write_reports(output_dir: Path, summary: dict[str, Any], verdicts: list[Verdict]) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "results.json").write_text(
        json.dumps({"summary": summary, "verdicts": [asdict(v) for v in verdicts]}, indent=2),
        encoding="utf-8",
    )
    report_path = output_dir / "index.html"
    report_path.write_text(_render_html(summary, verdicts), encoding="utf-8")
    return report_path


def _counts(verdicts: list[Verdict]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for v in verdicts:
        counts[v.category] = counts.get(v.category, 0) + 1
    return counts


def _render_html(summary: dict[str, Any], verdicts: list[Verdict]) -> str:
    counts = _counts(verdicts)
    chips = " ".join(
        f'<span style="background:{_CATEGORY_COLORS.get(cat, "#333")};color:#fff;'
        f'padding:2px 8px;border-radius:10px;margin-right:6px">{html.escape(cat)}: {n}</span>'
        for cat, n in sorted(counts.items())
    )
    rows = []
    for v in verdicts:
        color = _CATEGORY_COLORS.get(v.category, "#333")
        excerpt = html.escape(str(v.evidence.get("log_excerpt", "")))[:1200]
        rows.append(
            "<tr>"
            f'<td style="vertical-align:top">{html.escape(v.scenario_id)}</td>'
            f'<td style="vertical-align:top;color:{color};font-weight:600">{html.escape(v.category)}</td>'
            f'<td style="vertical-align:top">{v.confidence:.2f}</td>'
            f'<td style="vertical-align:top">{html.escape(v.reason)}</td>'
            f'<td style="vertical-align:top"><pre style="white-space:pre-wrap;margin:0;'
            f'max-height:180px;overflow:auto">{excerpt}</pre></td>'
            "</tr>"
        )
    meta = "".join(
        f"<li><b>{html.escape(k)}:</b> {html.escape(str(val))}</li>"
        for k, val in summary.items()
    )
    return f"""<!doctype html>
<html><head><meta charset="utf-8"><title>DL Streamer AI agent report</title>
<style>body{{font-family:system-ui,Arial,sans-serif;margin:24px;color:#222}}
table{{border-collapse:collapse;width:100%;font-size:14px}}
th,td{{border:1px solid #ddd;padding:6px 8px;text-align:left}}
th{{background:#f5f5f5}}</style></head>
<body>
<h1>DL Streamer AI user-simulation agent</h1>
<p>Exploratory, varied scenarios sampled from documentation and run against a package built from
the current code. Reproduce this exact run with the logged seed.</p>
<ul>{meta}</ul>
<p>{chips or "no scenarios"}</p>
<table><thead><tr><th>Scenario</th><th>Verdict</th><th>Conf.</th><th>Reason</th><th>Evidence</th></tr></thead>
<tbody>{''.join(rows)}</tbody></table>
</body></html>"""
