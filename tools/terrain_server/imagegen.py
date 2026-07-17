"""Image generation (RP-2): render-guided pictures for the visual bible.

Backends share the llmpool provider config:
  openai-image — /v1/images/generations (+ /edits with reference images);
                 gpt-image-* via any OpenAI-compatible base_url
  gemini-image — generateContent with IMAGE response modality
                 (nano banana pro etc.)

Prompts for scene art are assembled from the scene reference pack, so the
sky, the moons and the mountain shapes come from the simulation, not the
model's imagination.
"""

from __future__ import annotations

import base64
import json
from pathlib import Path

import requests

from llmpool import LLMPool


class ImageGen:
    def __init__(self, pool: LLMPool | None = None):
        self.pool = pool or LLMPool()

    def _spec(self, role: str) -> tuple[dict, dict]:
        spec = self.pool.resolve(role)
        return spec, spec["_provider"]

    # ---- backends ----

    def _openai_image(self, spec, prov, prompt, size, refs):
        key = self.pool._key(prov)
        if refs:
            files = [("image[]", (f"ref{i}.png", r, "image/png"))
                     for i, r in enumerate(refs)]
            r = requests.post(f"{prov['base_url']}/images/edits",
                              headers={"Authorization": f"Bearer {key}"},
                              data={"model": spec["model"], "prompt": prompt,
                                    "size": size},
                              files=files, timeout=600)
        else:
            r = requests.post(f"{prov['base_url']}/images/generations",
                              headers={"Authorization": f"Bearer {key}",
                                       "Content-Type": "application/json"},
                              json={"model": spec["model"], "prompt": prompt,
                                    "size": size}, timeout=600)
        r.raise_for_status()
        item = r.json()["data"][0]
        if "b64_json" in item:
            return base64.b64decode(item["b64_json"])
        return requests.get(item["url"], timeout=120).content

    def _gemini_image(self, spec, prov, prompt, size, refs):
        key = self.pool._key(prov)
        parts = [{"text": prompt}]
        for ref in refs or []:
            parts.append({"inline_data": {
                "mime_type": "image/png",
                "data": base64.b64encode(ref).decode()}})
        base = prov.get("base_url",
                        "https://generativelanguage.googleapis.com")
        r = requests.post(
            f"{base}/v1beta/models/{spec['model']}:generateContent",
            params={"key": key},
            headers={"Content-Type": "application/json"},
            json={"contents": [{"role": "user", "parts": parts}],
                  "generationConfig": {"responseModalities": ["IMAGE"]}},
            timeout=600)
        r.raise_for_status()
        for part in r.json()["candidates"][0]["content"]["parts"]:
            if "inlineData" in part:
                return base64.b64decode(part["inlineData"]["data"])
            if "inline_data" in part:
                return base64.b64decode(part["inline_data"]["data"])
        raise RuntimeError("no image part in gemini response")

    # ---- entry ----

    def generate(self, prompt: str, role: str = "painter",
                 size: str = "1024x1024", refs: list[bytes] | None = None,
                 save_to: Path | None = None) -> bytes:
        spec, prov = self._spec(role)
        protocol = prov["protocol"]
        if protocol in ("openai", "openai-image"):
            png = self._openai_image(spec, prov, prompt, size, refs)
        elif protocol in ("gemini", "gemini-image"):
            png = self._gemini_image(spec, prov, prompt, size, refs)
        else:
            raise ValueError(f"no image support for protocol {protocol}")
        if save_to is not None:
            Path(save_to).parent.mkdir(parents=True, exist_ok=True)
            Path(save_to).write_bytes(png)
        return png


def scene_prompt(opening: str, refpack: dict, style: str = "") -> str:
    """Compose an image prompt where every physical claim is simulated."""
    bits = [opening.strip()]
    lighting = (refpack or {}).get("lighting", {}).get("报告", "")
    if lighting:
        bits.append(f"光照与天空（须严格遵守）：{lighting}")
    skyline = (refpack or {}).get("skyline_report", [])
    if skyline:
        bits.append("远景地形（须严格遵守）：" + "；".join(skyline[:3]))
    bits.append(style or "奇幻小说插画，写实厚涂，电影感构图，无文字水印")
    return "\n".join(bits)
