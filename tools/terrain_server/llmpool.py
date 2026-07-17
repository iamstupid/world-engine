"""Multi-vendor LLM pool (RP-1): one client for every seat in the
editorial room. Protocols:

  openai     — OpenAI, Grok, DeepSeek, Kimi, GLM, OpenRouter and any
               OpenAI-compatible base_url (chat/completions)
  anthropic  — native messages API
  gemini     — native generateContent
  mock       — deterministic canned responses for tests (no network)

Config resolution: seat_overrides[seat] > roles[role] > error. Keys come
from the config value ("key") or environment ("key_env"). Usage (tokens
per seat/role) is recorded for the transcript's cost ledger. 5xx and
connection errors retry with backoff (hardening inherited from the
volume-1 pipeline).
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path

import requests

DEFAULT_CONFIG = {
    "providers": {
        "local8317": {"protocol": "openai",
                      "base_url": "http://172.30.176.1:8317/v1",
                      "key": "114514"},
        "mock": {"protocol": "mock"},
    },
    "roles": {
        "arbiter": {"provider": "local8317", "model": "gpt-5.4-mini"},
        "kp": {"provider": "local8317", "model": "gpt-5.6-sol",
               "reasoning": "high"},
        "actor_major": {"provider": "local8317", "model": "gpt-5.6-luna"},
        "actor_minor": {"provider": "local8317", "model": "gpt-5.4-mini"},
        "novelist": {"provider": "local8317", "model": "gpt-5.6-sol",
                     "reasoning": "high"},
        "painter": {"provider": "local8317", "model": "gpt-image-2"},
    },
    "seat_overrides": {},
}

CONFIG_PATH = Path(__file__).resolve().parent / "ai_pool.json"


class MockBackend:
    """Deterministic test backend: scripted replies per role/seat, or a
    callable; falls back to echoing the last user message."""

    def __init__(self):
        self.script: dict[str, list[str]] = {}
        self.handler = None
        self.calls: list[dict] = []

    def reply(self, key: str, messages: list[dict]) -> str:
        self.calls.append({"key": key, "messages": messages})
        if self.handler is not None:
            return self.handler(key, messages)
        if self.script.get(key):
            return self.script[key].pop(0)
        return f"[mock:{key}] " + messages[-1]["content"][:80]


class LLMPool:
    def __init__(self, config: dict | str | Path | None = None):
        if config is None:
            config = (json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
                      if CONFIG_PATH.exists() else DEFAULT_CONFIG)
        elif isinstance(config, (str, Path)):
            config = json.loads(Path(config).read_text(encoding="utf-8"))
        self.cfg = config
        self.usage: dict[str, dict] = {}
        self.mock = MockBackend()

    # ---- resolution ----

    def resolve(self, role: str, seat: str | None = None) -> dict:
        spec = None
        if seat and seat in self.cfg.get("seat_overrides", {}):
            spec = dict(self.cfg["seat_overrides"][seat])
        elif role in self.cfg.get("roles", {}):
            spec = dict(self.cfg["roles"][role])
        if spec is None:
            raise KeyError(f"no pool role '{role}' (seat={seat})")
        prov = self.cfg["providers"][spec["provider"]]
        spec["_provider"] = prov
        return spec

    @staticmethod
    def _key(prov: dict) -> str:
        if "key" in prov:
            return prov["key"]
        if "key_env" in prov:
            key = os.environ.get(prov["key_env"], "")
            if not key:
                raise RuntimeError(f"env {prov['key_env']} not set")
            return key
        return ""

    # ---- protocols ----

    def _openai(self, spec, messages, max_tokens):
        prov = spec["_provider"]
        payload = {"model": spec["model"], "messages": messages,
                   "max_completion_tokens": max_tokens}
        if spec.get("reasoning"):
            payload["reasoning_effort"] = spec["reasoning"]
        r = requests.post(f"{prov['base_url']}/chat/completions",
                          headers={"Authorization": f"Bearer {self._key(prov)}",
                                   "Content-Type": "application/json"},
                          json=payload, timeout=spec.get("timeout", 1200))
        if r.status_code == 400 and "max_completion_tokens" in r.text:
            payload.pop("max_completion_tokens")
            payload["max_tokens"] = max_tokens
            r = requests.post(f"{prov['base_url']}/chat/completions",
                              headers={"Authorization": f"Bearer {self._key(prov)}",
                                       "Content-Type": "application/json"},
                              json=payload, timeout=spec.get("timeout", 1200))
        return r

    def _anthropic(self, spec, messages, max_tokens):
        prov = spec["_provider"]
        system = "\n".join(m["content"] for m in messages
                           if m["role"] == "system")
        rest = [m for m in messages if m["role"] != "system"]
        payload = {"model": spec["model"], "max_tokens": max_tokens,
                   "messages": rest}
        if system:
            payload["system"] = system
        return requests.post(
            prov.get("base_url", "https://api.anthropic.com") + "/v1/messages",
            headers={"x-api-key": self._key(prov),
                     "anthropic-version": "2023-06-01",
                     "Content-Type": "application/json"},
            json=payload, timeout=spec.get("timeout", 1200))

    def _gemini(self, spec, messages, max_tokens):
        prov = spec["_provider"]
        system = "\n".join(m["content"] for m in messages
                           if m["role"] == "system")
        contents = [{"role": "model" if m["role"] == "assistant" else "user",
                     "parts": [{"text": m["content"]}]}
                    for m in messages if m["role"] != "system"]
        payload = {"contents": contents,
                   "generationConfig": {"maxOutputTokens": max_tokens}}
        if system:
            payload["systemInstruction"] = {"parts": [{"text": system}]}
        base = prov.get("base_url",
                        "https://generativelanguage.googleapis.com")
        return requests.post(
            f"{base}/v1beta/models/{spec['model']}:generateContent",
            params={"key": self._key(prov)},
            headers={"Content-Type": "application/json"},
            json=payload, timeout=spec.get("timeout", 1200))

    @staticmethod
    def _extract(protocol: str, data: dict) -> tuple[str, dict]:
        if protocol == "openai":
            usage = data.get("usage", {})
            return data["choices"][0]["message"]["content"], usage
        if protocol == "anthropic":
            usage = data.get("usage", {})
            return "".join(b.get("text", "") for b in data["content"]), {
                "prompt_tokens": usage.get("input_tokens"),
                "completion_tokens": usage.get("output_tokens")}
        if protocol == "gemini":
            cand = data["candidates"][0]
            text = "".join(p.get("text", "")
                           for p in cand["content"]["parts"])
            meta = data.get("usageMetadata", {})
            return text, {"prompt_tokens": meta.get("promptTokenCount"),
                          "completion_tokens": meta.get("candidatesTokenCount")}
        raise ValueError(protocol)

    # ---- entry point ----

    def chat(self, role: str, messages: list[dict], seat: str | None = None,
             max_tokens: int = 4000, retries: int = 3) -> str:
        spec = self.resolve(role, seat)
        protocol = spec["_provider"]["protocol"]
        ledger = self.usage.setdefault(seat or role, {
            "calls": 0, "prompt_tokens": 0, "completion_tokens": 0})
        if protocol == "mock":
            text = self.mock.reply(seat or role, messages)
            ledger["calls"] += 1
            return text
        send = {"openai": self._openai, "anthropic": self._anthropic,
                "gemini": self._gemini}[protocol]
        for attempt in range(retries):
            try:
                r = send(spec, messages, max_tokens)
                if r.status_code >= 500:
                    raise requests.ConnectionError(f"server {r.status_code}")
                r.raise_for_status()
                text, usage = self._extract(protocol, r.json())
                ledger["calls"] += 1
                ledger["prompt_tokens"] += int(usage.get("prompt_tokens") or 0)
                ledger["completion_tokens"] += int(
                    usage.get("completion_tokens") or 0)
                return text
            except (requests.ConnectionError, requests.Timeout):
                if attempt == retries - 1:
                    raise
                time.sleep(8 * (attempt + 1))
        raise RuntimeError("unreachable")

    def usage_report(self) -> dict:
        return {k: dict(v) for k, v in self.usage.items()}
