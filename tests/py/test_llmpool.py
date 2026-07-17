"""LLM pool gates: config resolution precedence, protocol payload shapes
(openai/anthropic/gemini) without network, retry on 5xx, mock backend and
the usage ledger."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "tools" / "terrain_server"))

import llmpool  # noqa: E402
from llmpool import LLMPool  # noqa: E402

CFG = {
    "providers": {
        "oai": {"protocol": "openai", "base_url": "http://x/v1", "key": "k"},
        "ant": {"protocol": "anthropic", "key": "ak"},
        "gem": {"protocol": "gemini", "key": "gk"},
        "mock": {"protocol": "mock"},
    },
    "roles": {
        "arbiter": {"provider": "mock", "model": "m0"},
        "actor_minor": {"provider": "oai", "model": "m1"},
        "actor_major": {"provider": "ant", "model": "m2"},
        "kp": {"provider": "gem", "model": "m3"},
    },
    "seat_overrides": {"赛芮": {"provider": "mock", "model": "override"}},
}


class FakeResp:
    def __init__(self, status, data):
        self.status_code = status
        self._data = data
        self.text = str(data)

    def json(self):
        return self._data

    def raise_for_status(self):
        if self.status_code >= 400:
            raise RuntimeError(f"http {self.status_code}")


def test_resolution_precedence():
    pool = LLMPool(CFG)
    assert pool.resolve("actor_minor")["model"] == "m1"
    assert pool.resolve("actor_minor", seat="赛芮")["model"] == "override"
    with pytest.raises(KeyError):
        pool.resolve("nonexistent")


def test_mock_and_ledger():
    pool = LLMPool(CFG)
    pool.mock.script["A"] = ["第一句", "第二句"]
    assert pool.chat("arbiter", [{"role": "user", "content": "hi"}],
                     seat="A") == "第一句"
    assert pool.chat("arbiter", [{"role": "user", "content": "hi"}],
                     seat="A") == "第二句"
    assert pool.usage["A"]["calls"] == 2


def test_openai_payload_and_retry(monkeypatch):
    pool = LLMPool(CFG)
    seen = []
    responses = [FakeResp(500, {}), FakeResp(200, {
        "choices": [{"message": {"content": "ok"}}],
        "usage": {"prompt_tokens": 5, "completion_tokens": 7}})]

    def fake_post(url, **kw):
        seen.append((url, kw))
        return responses.pop(0)

    monkeypatch.setattr(llmpool.requests, "post", fake_post)
    monkeypatch.setattr(llmpool.time, "sleep", lambda s: None)
    out = pool.chat("actor_minor", [{"role": "user", "content": "hi"}],
                    max_tokens=99)
    assert out == "ok"
    assert len(seen) == 2  # retried once after the 500
    url, kw = seen[-1]
    assert url.endswith("/chat/completions")
    assert kw["json"]["model"] == "m1"
    assert kw["json"]["max_completion_tokens"] == 99
    assert pool.usage["actor_minor"]["completion_tokens"] == 7


def test_anthropic_payload(monkeypatch):
    pool = LLMPool(CFG)
    seen = {}

    def fake_post(url, **kw):
        seen["url"], seen["kw"] = url, kw
        return FakeResp(200, {"content": [{"text": "hello "}, {"text": "there"}],
                              "usage": {"input_tokens": 3, "output_tokens": 4}})

    monkeypatch.setattr(llmpool.requests, "post", fake_post)
    out = pool.chat("actor_major", [
        {"role": "system", "content": "sys-A"},
        {"role": "user", "content": "hi"}])
    assert out == "hello there"
    assert seen["url"].endswith("/v1/messages")
    assert seen["kw"]["json"]["system"] == "sys-A"
    assert all(m["role"] != "system" for m in seen["kw"]["json"]["messages"])
    assert seen["kw"]["headers"]["x-api-key"] == "ak"


def test_gemini_payload(monkeypatch):
    pool = LLMPool(CFG)
    seen = {}

    def fake_post(url, **kw):
        seen["url"], seen["kw"] = url, kw
        return FakeResp(200, {"candidates": [{"content": {"parts": [
            {"text": "gm"}]}}], "usageMetadata": {"promptTokenCount": 2,
                                                  "candidatesTokenCount": 3}})

    monkeypatch.setattr(llmpool.requests, "post", fake_post)
    out = pool.chat("kp", [{"role": "system", "content": "S"},
                           {"role": "user", "content": "hi"},
                           {"role": "assistant", "content": "prev"},
                           {"role": "user", "content": "again"}])
    assert out == "gm"
    assert ":generateContent" in seen["url"]
    body = seen["kw"]["json"]
    assert body["systemInstruction"]["parts"][0]["text"] == "S"
    roles = [c["role"] for c in body["contents"]]
    assert roles == ["user", "model", "user"]
    assert seen["kw"]["params"]["key"] == "gk"
