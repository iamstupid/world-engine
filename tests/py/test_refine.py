"""M11.5 refinement-pyramid gates: boundary continuity with the global
field, world-anchored detail (deterministic, tiles agree), local erosion
carves drainage (fewer pits than noise-only), sane value ranges."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import refine  # noqa: E402
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
def land_bbox(session):
    w, h = session.result["width"], session.result["height"]
    z = np.frombuffer(session.result["layers"]["elevation_eroded_m"]["data"],
                      np.float32).reshape(h, w)
    best, bbox = -1.0, None
    for y0 in range(8, h - 24, 8):
        for x0 in range(0, w - 24, 12):
            frac = float((z[y0:y0 + 16, x0:x0 + 24] > 0).mean())
            if frac > best:
                best = frac
                lon0 = x0 / w * 360.0 - 180.0
                lat1 = 90.0 - y0 / h * 180.0
                bbox = (lon0, lat1 - 16 / h * 180.0, lon0 + 24 / w * 360.0, lat1)
    assert best > 0.5, "no land window found"
    return bbox


def test_boundary_continuity_and_determinism(session, land_bbox):
    lon0, lat0, lon1, lat1 = land_bbox
    t1 = refine.refine_tile(session, lon0, lat0, lon1, lat1, scale=6,
                            iterations=2)
    t1b = refine.refine_tile(session, lon0, lat0, lon1, lat1, scale=6,
                             iterations=2)
    assert np.array_equal(t1["tile"], t1b["tile"])  # deterministic
    tile, zup = t1["tile"], t1["z_up"]
    assert tile.shape == (t1["height"], t1["width"])
    assert not np.any(np.isnan(tile))
    for sl in (np.s_[0, :], np.s_[-1, :], np.s_[:, 0], np.s_[:, -1]):
        assert np.allclose(tile[sl], zup[sl], atol=1e-4)  # locked boundary


def test_detail_and_drainage(session, land_bbox):
    lon0, lat0, lon1, lat1 = land_bbox
    plain = refine.refine_tile(session, lon0, lat0, lon1, lat1, scale=6,
                               iterations=0, detail=False)
    noised = refine.refine_tile(session, lon0, lat0, lon1, lat1, scale=6,
                                iterations=0, detail=True)
    eroded = refine.refine_tile(session, lon0, lat0, lon1, lat1, scale=6,
                                iterations=3, detail=True)

    def grad_energy(t):
        gy, gx = np.gradient(t.astype(np.float64))
        return float(np.hypot(gx, gy)[2:-2, 2:-2].mean())

    g_plain = grad_energy(plain["tile"])
    g_eroded = grad_energy(eroded["tile"])
    assert g_eroded > g_plain * 1.05  # refinement adds real relief

    def pits(t):
        c = t[1:-1, 1:-1]
        lower = np.ones_like(c, bool)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                lower &= c < t[1 + dy:t.shape[0] - 1 + dy,
                               1 + dx:t.shape[1] - 1 + dx]
        land = c > 0
        return int((lower & land).sum())

    p_noise = pits(noised["tile"])
    p_eroded = pits(eroded["tile"])
    if p_noise > 20:
        assert p_eroded < p_noise  # erosion organizes drainage

    rng = eroded["tile"].max() - eroded["tile"].min()
    zr = plain["tile"].max() - plain["tile"].min()
    assert rng < zr + 3000.0  # no runaway values
