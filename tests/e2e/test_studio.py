"""Playwright end-to-end test of the WorldEngine studio (plan M8/M10/M11).

Drives the real UI against the real server: schema form, generation with SSE
progress, globe + 2D map rendering (pixel checks), layer switching, point
query, save/load round trip, uplift paint (M10), vector overlays (M11).
Saves screenshots to tests/e2e/artifacts/ for human review.

Run: pytest tests/e2e/test_studio.py -x -q  (server assumed on :8151)
"""

import re
import time
from pathlib import Path

import pytest
from playwright.sync_api import sync_playwright, expect

BASE = "http://localhost:8151"
ART = Path(__file__).parent / "artifacts"
ART.mkdir(exist_ok=True)


@pytest.fixture(scope="module")
def page():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(args=["--enable-unsafe-swiftshader"])
        pg = browser.new_page(viewport={"width": 1500, "height": 900})
        pg.goto(BASE)
        yield pg
        browser.close()


def canvas_nonblank(page, selector, min_colors=8):
    return page.evaluate(
        """(sel) => {
          const c = document.querySelector(sel);
          if (!c || c.width === 0) return 0;
          const g = c.getContext('2d') || null;
          let data;
          if (g) {
            data = g.getImageData(0, 0, Math.min(256, c.width), Math.min(256, c.height)).data;
          } else {
            const t = document.createElement('canvas');
            t.width = Math.min(256, c.width); t.height = Math.min(256, c.height);
            t.getContext('2d').drawImage(c, 0, 0);
            data = t.getContext('2d').getImageData(0, 0, t.width, t.height).data;
          }
          const seen = new Set();
          for (let i = 0; i < data.length; i += 16) {
            seen.add((data[i] >> 4) + ',' + (data[i+1] >> 4) + ',' + (data[i+2] >> 4));
          }
          return seen.size;
        }""",
        selector,
    ) >= min_colors


def set_param(page, key, value):
    page.eval_on_selector_all("details.group", "els => els.forEach(e => e.open = true)")
    inp = page.locator(f'#params input[data-key="{key}"]')
    inp.fill(str(value))
    inp.dispatch_event("change")


def test_01_schema_form_renders(page):
    expect(page.locator("#params .group")).to_have_count(6, timeout=15000)
    assert page.locator("#params input").count() >= 30
    page.screenshot(path=str(ART / "01_form.png"))


def test_02_generate_small_world(page):
    # small & fast config for CI-ish runtime
    set_param(page, "world.width", 512)
    set_param(page, "world.height", 256)
    set_param(page, "world.physics_grid_frequency", 88)
    set_param(page, "tectonics.grid_frequency", 64)
    set_param(page, "erosion.multigrid_levels", 4)
    page.click("#btn-generate")
    expect(page.locator("body")).to_have_attribute("data-ready", "1", timeout=180000)
    status = page.text_content("#status")
    assert "ready" in status
    assert re.search(r"\d+ layers", status)


def test_03_globe_renders(page):
    time.sleep(1.0)
    page.screenshot(path=str(ART / "03_globe.png"))
    assert canvas_nonblank(page, "#globe-canvas")


def test_04_map_and_layers(page):
    page.click("#tab-map")
    time.sleep(0.3)
    assert canvas_nonblank(page, "#map-canvas")
    page.screenshot(path=str(ART / "04_map_elevation.png"))
    for layer in ("biome_id", "temperature_c", "precipitation_mm_yr"):
        if page.locator(f'#layer-select option[value="{layer}"]').count():
            page.select_option("#layer-select", layer)
            time.sleep(0.3)
            assert canvas_nonblank(page, "#map-canvas"), layer
            page.screenshot(path=str(ART / f"04_map_{layer}.png"))
    page.select_option("#layer-select", "elevation_eroded_m")
    time.sleep(0.3)


def test_05_point_query(page):
    box = page.locator("#map-canvas").bounding_box()
    page.mouse.click(box["x"] + box["width"] * 0.5, box["y"] + box["height"] * 0.45)
    expect(page.locator("#query")).to_be_visible(timeout=10000)
    rows = page.locator("#query-table tr").count()
    assert rows >= 4
    page.screenshot(path=str(ART / "05_query.png"))


def test_06_overlays(page):
    page.click("#tab-map")
    time.sleep(0.3)
    for overlay in ("rivers", "settlements", "borders", "roads"):
        page.select_option("#overlay-select", overlay)
        time.sleep(0.4)
        page.screenshot(path=str(ART / f"06_overlay_{overlay}.png"))
    counts = page.evaluate(
        "() => state.features ? state.features.features.length : -1")
    assert counts > 0, "no vector features generated (M11)"


def test_07_save_load_roundtrip(page):
    page.fill("#world-name", "e2e-test")
    page.click("#btn-save")
    expect(page.locator("#status")).to_contain_text("saved", timeout=30000)
    page.click("#btn-load")
    expect(page.locator("body")).to_have_attribute("data-ready", "1", timeout=60000)
    assert canvas_nonblank(page, "#map-canvas")


def test_08_paint_uplift_changes_world(page):
    # query a mid-ocean point, paint uplift there, regenerate, expect change
    page.click("#tab-map")
    box = page.locator("#map-canvas").bounding_box()
    sid = page.get_attribute("body", "data-sid")
    import urllib.request, json as jsonlib

    def q(lat, lon):
        with urllib.request.urlopen(
                f"{BASE}/api/sessions/{sid}/query?lat={lat}&lon={lon}") as r:
            return jsonlib.loads(r.read())

    before = q(0, 0)
    page.click("#brush-toggle")
    cx, cy = box["x"] + box["width"] * 0.5, box["y"] + box["height"] * 0.5
    page.mouse.move(cx, cy)
    page.mouse.down()
    for dx in range(-30, 31, 10):
        page.mouse.move(cx + dx, cy)
    page.mouse.up()
    page.click("#brush-toggle")
    page.screenshot(path=str(ART / "08_paint.png"))
    page.click("#btn-generate")
    expect(page.locator("body")).to_have_attribute("data-ready", "1", timeout=180000)
    after = q(0, 0)
    assert after["elevation_eroded_m"] > before["elevation_eroded_m"] + 50, (
        before["elevation_eroded_m"], after["elevation_eroded_m"])
    page.screenshot(path=str(ART / "08_after_paint.png"))
