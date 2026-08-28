"""Mine runnable scenarios from sample READMEs and the user guide."""
from __future__ import annotations

import re
from pathlib import Path

from .models import Scenario

_FENCE_RE = re.compile(r"```(?:bash|sh|shell|console)?\s*\n(.*?)```", re.DOTALL)
_ENV_RE = re.compile(r"\$\{?([A-Z][A-Z0-9_]+)\}?")
_PLACEHOLDER_RE = re.compile(r"<[A-Za-z][^>\n]{0,80}>")  # e.g. <path to model> template tokens
_ASSIGN_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")
_LAUNCH_BINARIES = ("gst-launch-1.0", "gst-inspect-1.0")
_SETUP_KEYWORDS = (
    "wget", "curl", "download_", "pip install", "pip3 install", "apt-get", "apt install",
    "git clone", "huggingface-cli", "omz_downloader", "model_downloader", "install.sh",
)
_PREP_HEADS = ("cd", "export", "source", ".", "set", "unset", "mkdir", "echo")

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


def _cmd_tokens(line: str) -> list[str]:
    return [t for t in line.strip().split() if not _ASSIGN_RE.match(t)]


def _is_launch_line(line: str) -> bool:
    tokens = _cmd_tokens(line)
    if not tokens:
        return False
    head = tokens[0]
    if head in _LAUNCH_BINARIES or head.startswith("./"):
        return True
    return head in ("python", "python3") and len(tokens) > 1 and tokens[1].startswith("./")


def _is_prep_line(line: str) -> bool:
    stripped = line.strip()
    if _ASSIGN_RE.match(stripped):
        return True
    tokens = stripped.split()
    return bool(tokens) and tokens[0] in _PREP_HEADS


def _classify_block(text: str) -> str | None:
    """Return 'launch' (runs the product), 'setup' (prepares prerequisites), or None (skip)."""
    lines = [ln.strip() for ln in text.splitlines()
             if ln.strip() and not ln.strip().startswith("#")]
    if not lines:
        return None
    if any(_is_launch_line(ln) for ln in lines):
        return "launch"
    if any(kw in text.lower() for kw in _SETUP_KEYWORDS):
        return "setup"
    if all(_is_prep_line(ln) for ln in lines):
        return "setup"
    return None


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
    workdir = md_path.parent.relative_to(repo_root).as_posix()

    setup_so_far: list[str] = []
    scenarios: list[Scenario] = []
    for idx, match in enumerate(_FENCE_RE.finditer(text)):
        block = match.group(1)
        if _PLACEHOLDER_RE.search(block):  # skip template blocks that aren't runnable as-is
            continue
        commands = _clean_commands(block)
        if not commands:
            continue
        kind = _classify_block(commands[0])
        if kind is None:
            continue
        if kind == "setup":
            setup_so_far.append(commands[0])
            continue
        env_text = "\n".join(setup_so_far) + "\n" + commands[0]
        env_reqs = sorted({m.group(1) for m in _ENV_RE.finditer(env_text)})
        scenarios.append(
            Scenario(
                id=f"{rel}#{idx}",
                source=rel,
                category=_infer_category(rel),
                commands=[commands[0]],
                setup=list(setup_so_far),
                env_requirements=env_reqs,
                expected=["exit 0"],
                workdir=workdir,
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
