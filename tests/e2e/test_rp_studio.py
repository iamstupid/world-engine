"""Playwright end-to-end test of the RP studio (RP-3).

Drives the real three-panel UI against the real server: seat list and
viewer switching, scene opening with simulated lighting, three-layer
utterances, SERVER-SIDE inner-layer isolation observed through the UI
(the other seat cannot see the inner line), KP thread ledger and scene
settling. Screenshots to tests/e2e/artifacts/.

Run: pytest tests/e2e/test_rp_studio.py -x -q  (server assumed on :8151)
"""

import time
from pathlib import Path

import pytest
import requests
from playwright.sync_api import sync_playwright, expect

BASE = "http://localhost:8151"
ART = Path(__file__).parent / "artifacts"
ART.mkdir(exist_ok=True)


@pytest.fixture(scope="module")
def sid():
    s = requests.post(f"{BASE}/api/sessions").json()["id"]
    requests.put(f"{BASE}/api/sessions/{s}/params", json={
        "world.seed": 909, "world.width": 256, "world.height": 128,
        "world.physics_grid_frequency": 88, "tectonics.grid_frequency": 48,
        "erosion.multigrid_levels": 4, "erosion.fixed_point_iterations": 4})
    requests.post(f"{BASE}/api/sessions/{s}/generate")
    for _ in range(240):
        info = requests.get(f"{BASE}/api/sessions/{s}").json()
        if not info["running"]:
            break
        time.sleep(0.5)
    assert info["error"] is None
    requests.post(f"{BASE}/api/sessions/{s}/rp/init", json={"use_ai": False})
    for name, kind in (("KP", "kp"), ("凯尔", "actor"), ("赛芮", "actor")):
        requests.post(f"{BASE}/api/sessions/{s}/rp/seat",
                      json={"name": name, "kind": kind, "place": [10, 20]})
    requests.post(f"{BASE}/api/sessions/{s}/rp/scene/open", json={
        "place": [10, 20], "cast": ["凯尔", "赛芮"],
        "opening": "雨夜货栈，商队明晨出发。", "day": 137.0})
    return s


@pytest.fixture(scope="module")
def page(sid):
    with sync_playwright() as pw:
        browser = pw.chromium.launch(args=["--enable-unsafe-swiftshader"])
        pg = browser.new_page(viewport={"width": 1500, "height": 900})
        pg.goto(f"{BASE}/rp?sid={sid}")
        yield pg
        browser.close()


def set_viewer(page, name):
    page.select_option("#viewer-select", name)
    page.wait_for_timeout(400)


def test_01_panels_render(page):
    expect(page.locator("#main")).to_be_visible(timeout=10000)
    expect(page.locator(".seat")).to_have_count(3, timeout=8000)
    expect(page.locator("#scene-info")).to_contain_text("场景#1")
    expect(page.locator("#scene-info")).to_contain_text("☀")  # lighting brief
    page.screenshot(path=str(ART / "rp_01_panels.png"))


def test_02_three_layer_say(page):
    set_viewer(page, "凯尔")
    page.select_option("#layer-select", "dialogue")
    page.fill("#say-text", "「今晚检查驮鞍，明早出发。」")
    page.click("#say-btn")
    expect(page.locator(".entry.dialogue")).to_contain_text("驮鞍",
                                                            timeout=6000)
    page.select_option("#layer-select", "inner")
    page.fill("#say-text", "（其实我怕得要命。）")
    page.click("#say-btn")
    expect(page.locator(".entry.inner")).to_contain_text("怕得要命",
                                                         timeout=6000)
    page.screenshot(path=str(ART / "rp_02_kaier_view.png"))


def test_03_inner_isolated_from_other_viewer(page):
    set_viewer(page, "赛芮")
    expect(page.locator("#entries")).to_contain_text("驮鞍", timeout=6000)
    page.wait_for_timeout(600)
    assert "怕得要命" not in page.locator("#entries").inner_text()
    page.screenshot(path=str(ART / "rp_03_sairui_view.png"))


def test_04_kp_threads_and_settle(page):
    set_viewer(page, "KP")
    page.fill("#thread-desc", "赛芮的密卷来历不明")
    page.click("#thread-add")
    expect(page.locator(".thread")).to_contain_text("密卷", timeout=6000)
    page.fill("#sc-event", "商队提前动身的消息传开")
    page.select_option("#sc-vis", "public")
    page.click("#sc-close-btn")
    expect(page.locator("#scene-info")).to_contain_text("无开启的场景",
                                                        timeout=8000)
    page.screenshot(path=str(ART / "rp_04_kp_settled.png"))


def test_05_refpack_images_render(page):
    for sel in ("#minimap-img", "#skydome-img"):
        ok = page.evaluate(
            f"() => {{ const i = document.querySelector('{sel}');"
            f" return i && i.naturalWidth > 10; }}")
        assert ok, f"{sel} did not render"
