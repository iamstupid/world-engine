"""Reference-pack gates: skyline profiles see real mountains and stay flat
at sea, minimap/skydome PNGs render, lighting briefs match the sky, and
image-prompt assembly injects the simulated facts. Plus payload-shape
tests for both image backends (no network)."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import astro  # noqa: E402
import imagegen  # noqa: E402
import refpack  # noqa: E402
from llmpool import LLMPool  # noqa: E402
from worldstore import WorldStore  # noqa: E402


class FakeSession:
    def __init__(self):
        self.params = {
            "world.seed": 909, "world.width": 256, "world.height": 128,
            "world.physics_grid_frequency": 88,
            "tectonics.grid_frequency": 48,
            "erosion.multigrid_levels": 4, "erosion.fixed_point_iterations": 4,
        }
        self.result = None
        self.features = []
        self.entities = []
        self.store = WorldStore()


@pytest.fixture(scope="module")
def session():
    s = FakeSession()
    p = weterrain.Params()
    for k, v in s.params.items():
        p.set(k, v)
    s.result = weterrain.generate(p)
    return s


@pytest.fixture(scope="module")
def obs():
    return astro.build_universe(909, 360.0, {
        "home_companion": {"mass_ratio": 0.32, "a_au": 60.0, "e": 0.12}})[1]


def _grids(session):
    h, w = session.result["height"], session.result["width"]
    z = np.frombuffer(session.result["layers"]["elevation_eroded_m"]["data"],
                      np.float32).reshape(h, w)
    return z, w, h


def _lonlat(x, y, w, h):
    return (x + 0.5) / w * 360.0 - 180.0, 90.0 - (y + 0.5) / h * 180.0


class SynthSession:
    """Controlled geometry: a 2200 m gaussian island (sigma 1.5 px) in a
    shallow sea on a 2048x1024 grid (~19.6 km/px). The observer stands
    3 px west of the peak — the mountain must appear in the EAST."""

    def __init__(self):
        w, h = 2048, 1024
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
        cy, cx = h // 2, w // 2
        z = np.full((h, w), -60.0)
        z += 2260.0 * np.exp(-(((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * 1.5 ** 2)))
        self.result = {"width": w, "height": h, "layers": {
            "elevation_eroded_m": {"dtype": "f32",
                                   "data": z.astype(np.float32).tobytes()}}}
        self.cx, self.cy, self.w, self.h = cx, cy, w, h
        peak_lon, peak_lat = _lonlat(cx, cy, w, h)
        self.features = [{"kind": "settlement", "geometry": {
            "type": "Point", "coordinates": [peak_lon, peak_lat]},
            "properties": {}}]


def test_skyline_geometry_synthetic():
    s = SynthSession()
    lon, lat = _lonlat(s.cx - 3, s.cy, s.w, s.h)  # 3 px (~59 km) west
    sk = refpack.skyline_profile(s, lon, lat, n_az=36, max_km=160,
                                 step_km=8.0)
    angles = np.asarray(sk["elev_angle_deg"])
    az = np.asarray(sk["az_deg"])
    assert angles.max() > 1.0                     # the mountain is visible
    assert abs(az[int(angles.argmax())] - 90.0) <= 20.0  # ... in the east
    west = angles[(az >= 250) & (az <= 290)]
    assert west.max() < 0.3                       # open sea westward
    assert any("高地" in r for r in sk["report"])
    assert sk == refpack.skyline_profile(s, lon, lat, n_az=36, max_km=160,
                                         step_km=8.0)  # deterministic


def test_skyline_flat_at_open_sea(session):
    z, w, h = _grids(session)
    win = 10
    zz = z <= 0
    for y0 in range(4, h - win, 6):
        for x0 in range(0, w - win, 8):
            if zz[y0:y0 + win, x0:x0 + win].all():
                olon, olat = _lonlat(x0 + win // 2, y0 + win // 2, w, h)
                sea = refpack.skyline_profile(session, olon, olat,
                                              max_km=120)
                assert max(sea["elev_angle_deg"]) < 0.5
                assert any("开阔" in r for r in sea["report"])
                return
    pytest.skip("no open-ocean window")


def test_minimap_and_skydome_render(session, obs):
    s = SynthSession()
    lon, lat = _lonlat(s.cx - 3, s.cy, s.w, s.h)
    png = refpack.minimap_png(s, lon, lat, radius_km=120, size=96)
    assert png[:4] == b"\x89PNG"
    from PIL import Image
    import io
    img = Image.open(io.BytesIO(png))
    assert img.size == (96, 96)
    colors = set(img.getdata())
    assert len(colors) > 10                    # sea + island gradient
    assert (255, 60, 60) in colors             # settlement marker drawn
    z, w, h = _grids(session)
    my, mx = np.unravel_index(np.argmax(z), z.shape)
    lon, lat = _lonlat(mx, my, w, h)

    for hh in range(24):
        sky = obs.sky(lat, lon, 137.0 + hh / 24.0)
        if sky["period"] == "night":
            break
    dome = refpack.skydome_png(sky)
    assert dome[:4] == b"\x89PNG"
    brief = refpack.lighting_brief(sky)
    assert brief["period"] == sky["period"]
    assert "月" in brief["报告"]


def test_scene_refpack_and_prompt(session, obs):
    z, w, h = _grids(session)
    my, mx = np.unravel_index(np.argmax(z), z.shape)
    lon, lat = _lonlat((mx + 3) % w, my, w, h)
    pack = refpack.scene_refpack(session, obs, lon, lat, 137.0)
    assert pack["skyline_report"] and "lighting" in pack
    assert "skydome_png" not in pack  # text-only by default (transcript-safe)
    prompt = imagegen.scene_prompt("雨夜货栈，商队围着火塘", pack)
    assert "光照与天空" in prompt and "远景地形" in prompt


def test_image_payload_openai(monkeypatch):
    cfg = {"providers": {"p": {"protocol": "openai-image",
                               "base_url": "http://img/v1", "key": "k"}},
           "roles": {"painter": {"provider": "p", "model": "gpt-image-2"}},
           "seat_overrides": {}}
    gen = imagegen.ImageGen(LLMPool(cfg))
    seen = {}

    class R:
        status_code = 200

        def raise_for_status(self):
            pass

        def json(self):
            import base64
            return {"data": [{"b64_json": base64.b64encode(b"PNGDATA").decode()}]}

    def fake_post(url, **kw):
        seen["url"], seen["kw"] = url, kw
        return R()

    monkeypatch.setattr(imagegen.requests, "post", fake_post)
    out = gen.generate("a tower", size="512x512")
    assert out == b"PNGDATA"
    assert seen["url"].endswith("/images/generations")
    assert seen["kw"]["json"]["model"] == "gpt-image-2"

    gen.generate("edit", refs=[b"refpngbytes"])
    assert seen["url"].endswith("/images/edits")
    assert "files" in seen["kw"]


def test_image_payload_gemini(monkeypatch):
    cfg = {"providers": {"g": {"protocol": "gemini-image", "key": "gk"}},
           "roles": {"painter": {"provider": "g", "model": "nano-banana-pro"}},
           "seat_overrides": {}}
    gen = imagegen.ImageGen(LLMPool(cfg))
    seen = {}

    class R:
        status_code = 200

        def raise_for_status(self):
            pass

        def json(self):
            import base64
            return {"candidates": [{"content": {"parts": [
                {"inlineData": {"data": base64.b64encode(b"IMG").decode()}}]}}]}

    def fake_post(url, **kw):
        seen["url"], seen["kw"] = url, kw
        return R()

    monkeypatch.setattr(imagegen.requests, "post", fake_post)
    out = gen.generate("a moon", refs=[b"ref"])
    assert out == b"IMG"
    assert ":generateContent" in seen["url"]
    body = seen["kw"]["json"]
    assert body["generationConfig"]["responseModalities"] == ["IMAGE"]
    assert len(body["contents"][0]["parts"]) == 2  # text + ref image
