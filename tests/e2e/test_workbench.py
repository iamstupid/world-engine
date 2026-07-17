"""Playwright test of the unified workbench skeleton: dockview panels
render (native Pipeline + iframed studios), the node graph shows the
template DAG from the registry, the inspector edits parameters, and the
frequency-mismatch one-click resample fix works end to end.

Run: pytest tests/e2e/test_workbench.py -x -q  (server on :8151, ui/app built)
"""

from pathlib import Path

import pytest
from playwright.sync_api import expect, sync_playwright

BASE = "http://localhost:8151"
ART = Path(__file__).parent / "artifacts"
ART.mkdir(exist_ok=True)


@pytest.fixture(scope="module")
def page():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(args=["--enable-unsafe-swiftshader"])
        pg = browser.new_page(viewport={"width": 1600, "height": 950})
        pg.goto(f"{BASE}/app/")
        yield pg
        browser.close()


def test_01_dock_panels_render(page):
    expect(page.locator("#session-badge")).to_contain_text("会话",
                                                           timeout=15000)
    for title in ("管线节点图", "地形工作室", "RP 工作室"):
        expect(page.get_by_text(title, exact=True).first).to_be_visible(
            timeout=10000)
    page.screenshot(path=str(ART / "wb_01_dock.png"))


def test_02_template_graph_renders(page):
    expect(page.locator(".react-flow__node")).to_have_count(6, timeout=15000)
    expect(page.locator(".react-flow__edge")).to_have_count(6)
    page.screenshot(path=str(ART / "wb_02_graph.png"))


def test_03_inspector_edits_params(page):
    page.get_by_text("phys", exact=False).first.click()
    expect(page.locator("[data-testid=inspector]")).to_be_visible(timeout=5000)
    expect(page.locator("[data-testid=inspector]")).to_contain_text("frequency")


def test_04_frequency_mismatch_and_fix(page):
    # add a noise node at a different frequency and wire it into combine.b
    page.get_by_role("button", name="+noise").click()
    page.get_by_text("noise1", exact=False).first.click()
    freq_input = page.locator("[data-testid=inspector] input").first
    freq_input.fill("88")
    freq_input.blur()
    # rewire combine.b to noise1.field via direct connect is fiddly in RF;
    # emulate by editing through the API-driven validate path: drag from
    # noise1 output handle to combine's b handle
    src = page.locator(".react-flow__node", has_text="noise1") \
              .locator(".react-flow__handle-right").first
    dst = page.locator(".react-flow__node", has_text="combine") \
              .locator(".react-flow__handle-left").nth(1)
    src.drag_to(dst)
    expect(page.locator("[data-testid=graph-error]")).to_contain_text(
        "频率不匹配", timeout=8000)
    page.screenshot(path=str(ART / "wb_03_mismatch.png"))
    page.locator("[data-testid=fix-resample]").click()
    expect(page.locator("[data-testid=graph-error]")).to_have_count(
        0, timeout=8000)
    expect(page.locator(".react-flow__node", has_text="resample")).to_have_count(
        3, timeout=5000)  # up_hi, base_hi + the inserted fix
    page.screenshot(path=str(ART / "wb_04_fixed.png"))
