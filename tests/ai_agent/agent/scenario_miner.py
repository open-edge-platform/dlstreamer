"""Mine runnable scenarios from sample READMEs and the user guide."""
from __future__ import annotations

import re
from pathlib import Path

from .models import Scenario

_FENCE_RE = re.compile(r"```(?:bash|sh|shell|console)?\s*\n(.*?)```", re.DOTALL)
_ENV_RE = re.compile(r"\$\{?([A-Z][A-Z0-9_]+)\}?")
_PLACEHOLDER_RE = re.compile(r"<[A-Za-z][^>\n]{0,80}>")  # e.g. <path to model> template tokens
_COMMAND_MARKERS = ("gst-launch-1.0", "gst-inspect-1.0", "./", "python", "python3")

_CATEGORY_KEYWORDS = {
    "tracking": ("track", "tracking"),
    "classification": ("classif", "attribute"),
    "detection": ("detect", "yolo", "detection"),
    "audio": ("audio", "sound"),
    "vlm": ("lvm", "vlm", "clip", "genai", "caption"),
    "pose": ("pose", "keypoint"),
    "depth": ("depth",),
    "segmentation": ("segment", "mask"),
    "python": ("/python/",),
    "cpp": ("/cpp/",),
}


def _infer_category(path: str) -> str:
    low = path.lower()
    for category, keywords in _CATEGORY_KEYWORDS.items():
        if any(k in low for k in keywords):
            return category
    return "other"


def _looks_runnable(block: str) -> bool:
    return any(marker in block for marker in _COMMAND_MARKERS)


def _clean_commands(block: str) -> list[str]:
    lines = []
    for raw in block.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        line = line[1:].strip() if line.startswith("$ ") or line == "$" else line
        lines.append(line)
    return ["\n".join(lines)] if lines else []


def _extract_scenarios(md_path: Path, repo_root: Path) -> list[Scenario]:
    text = md_path.read_text(encoding="utf-8", errors="replace")
    rel = md_path.relative_to(repo_root).as_posix()
    scenarios: list[Scenario] = []
    for idx, match in enumerate(_FENCE_RE.finditer(text)):
        block = match.group(1)
        if not _looks_runnable(block):
            continue
        if _PLACEHOLDER_RE.search(block):  # skip template blocks that aren't runnable as-is
            continue
        commands = _clean_commands(block)
        if not commands:
            continue
        env_reqs = sorted({m.group(1) for m in _ENV_RE.finditer(block)})
        scenarios.append(
            Scenario(
                id=f"{rel}#{idx}",
                source=rel,
                category=_infer_category(rel),
                commands=commands,
                env_requirements=env_reqs,
                expected=["exit 0"],
                workdir=md_path.parent.relative_to(repo_root).as_posix(),
            )
        )
    return scenarios


def mine_scenarios(repo_root: Path) -> list[Scenario]:
    """Crawl all sample READMEs and user-guide pages for runnable command blocks."""
    sources: list[Path] = []
    samples = repo_root / "samples"
    if samples.is_dir():
        sources.extend(samples.rglob("README.md"))
    user_guide = repo_root / "docs" / "user-guide"
    if user_guide.is_dir():
        sources.extend(user_guide.rglob("*.md"))

    scenarios: list[Scenario] = []
    for path in sorted(sources):
        try:
            scenarios.extend(_extract_scenarios(path, repo_root))
        except OSError:
            continue
    return scenarios
