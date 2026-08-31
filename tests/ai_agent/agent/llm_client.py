"""OpenAI-backed LLM client with an offline mock and simple credit accounting."""
from __future__ import annotations

import json
import math
from typing import Any


class BudgetExceeded(RuntimeError):
    pass


class LLMClient:
    """Thin wrapper over the OpenAI chat API; falls back to a mock when offline."""

    def __init__(self, model: str, api_key: str | None, budget_credits: int, offline: bool,
                 base_url: str | None = None):
        self.model = model
        self.budget_credits = budget_credits
        self.offline = offline
        self.spent_credits = 0.0
        self.calls = 0
        self._client = None
        if not offline:
            if not api_key:
                raise ValueError("online mode requires AI_AGENT_LLM_API_KEY")
            from openai import OpenAI  # imported lazily so offline runs need no dependency

            self._client = OpenAI(api_key=api_key, base_url=base_url) if base_url else OpenAI(api_key=api_key)

    @property
    def remaining_credits(self) -> float:
        return self.budget_credits - self.spent_credits

    def _charge(self, text: str, has_image: bool) -> None:
        # 1 credit ~= 1k tokens (~4 chars/token); flat surcharge when an image is attached.
        credits = math.ceil(len(text) / 4000) + (5 if has_image else 0)
        self.spent_credits += credits
        self.calls += 1
        if self.spent_credits > self.budget_credits:
            raise BudgetExceeded(f"budget {self.budget_credits} credits exhausted")

    def complete_json(self, system: str, user: str, image_paths: list[str] | None = None) -> dict[str, Any]:
        """Return a JSON object from the model (or a mock verdict/plan when offline)."""
        image_paths = image_paths or []
        self._charge(system + user, bool(image_paths))
        if self.offline or self._client is None:
            return _mock_json(user)

        content: list[dict[str, Any]] = [{"type": "text", "text": user}]
        for path in image_paths:
            content.append({"type": "image_url", "image_url": {"url": _as_data_url(path)}})
        resp = self._client.chat.completions.create(
            model=self.model,
            response_format={"type": "json_object"},
            messages=[
                {"role": "system", "content": system},
                {"role": "user", "content": content},
            ],
        )
        return json.loads(resp.choices[0].message.content or "{}")


def _mock_json(user: str) -> dict[str, Any]:
    # Offline stand-in so the pipeline runs end-to-end without an API key.
    if "classify the outcome" in user.lower():
        return {"category": "user-error", "confidence": 0.3,
                "reason": "offline mock verdict (no LLM available)"}
    return {"assertions": ["exit 0"], "notes": "offline mock plan"}


def _as_data_url(path: str) -> str:
    import base64
    import mimetypes

    mime = mimetypes.guess_type(path)[0] or "image/png"
    with open(path, "rb") as fh:
        data = base64.b64encode(fh.read()).decode("ascii")
    return f"data:{mime};base64,{data}"
