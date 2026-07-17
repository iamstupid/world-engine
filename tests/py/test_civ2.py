"""Civ v2 gates (batch B, docs/MAP_SUITE_DESIGN.md §1.9/§2.1): species-pack
filter DSL validation, IGM spherical-polygon masks, era-unrolled settlement
seeding with founded_* provenance, phase-DAG caching with correct dirty
cones, determinism, and the M12 merge contract surviving regeneration.
Headless: kernel + civ2, no server."""

import math
import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import civ2  # noqa: E402
from worldstore import WorldStore  # noqa: E402


class FakeSession:
    def __init__(self, result=None):
        self.params = {
            "world.seed": 909, "world.width": 256, "world.height": 128,
            "world.physics_grid_frequency": 64,
            "tectonics.grid_frequency": 40,
            "erosion.multigrid_levels": 4, "erosion.fixed_point_iterations": 3,
        }
        self.result = result
        self.features = []
        self.entities = []
        self.store = WorldStore()
        self.civ_spec = None
        self.phase_cache = {}


@pytest.fixture(scope="module")
def terrain():
    s = FakeSession()
    p = weterrain.Params()
    for k, v in s.params.items():
        p.set(k, v)
    return weterrain.generate(p)


@pytest.fixture(scope="module")
def session(terrain):
    s = FakeSession(terrain)
    civ2.generate_civilization(s)
    return s


def _spec():
    import copy
    return copy.deepcopy(civ2.DEFAULT_SPEC)


# ---------------------------------------------------------- pure geometry ----

def test_polygon_mask_winding():
    g = weterrain.geodesic_graph(32)
    n = g["cell_count"]
    centers = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
    ring = [[lon, 60.0] for lon in (0, 60, 120, 180, -120, -60)]
    inside = civ2.polygon_mask(centers, ring)
    lat = np.degrees(np.arcsin(np.clip(centers[:, 1], -1, 1)))
    assert inside[lat > 75].all(), "cells near the enclosed pole must be inside"
    assert not inside[lat < 30].any(), "cells far outside must be excluded"
    # reversed orientation encloses the same region
    assert (civ2.polygon_mask(centers, ring[::-1]) == inside).all()


def test_validate_spec_rejects_bad_vocab():
    spec = _spec()
    spec["species"][0]["filter"]["hard"].append(
        {"field": "mana_density", "op": "gt", "value": 1})
    errs = civ2.validate_spec(spec)
    assert any("mana_density" in e for e in errs)
    spec2 = _spec()
    spec2["species"][0]["filter"]["soft"].append(
        {"field": "vegetation", "fn": "sqrt", "weight": 1})
    assert civ2.validate_spec(spec2)
    assert civ2.validate_spec({"species": []})


# ------------------------------------------------------------- generation ----

def test_default_generation_outputs(session):
    setts = session.store.find("settlement")
    assert len(setts) >= 12
    for s in setts[:5]:
        assert s["species_id"].startswith("species:")
        assert s["culture_id"].startswith("culture:")
        assert s["polity_id"].startswith("polity:")
        assert s["founded_era"].startswith("纪元")
        assert s["founded_day"] >= 0
        assert s["population"] > 0
    for layer in ("cell_polity_capital", "cell_culture_hearth", "cell_road",
                  "cell_accessibility"):
        assert layer in session.result["cell_layers"], layer
    kinds = {f["kind"] for f in session.features}
    assert {"settlement", "border", "road"} <= kinds
    assert len(session.store.eras) == 4
    assert session.store.find("species")


def test_eras_are_monotonic_and_roads_exist(session):
    setts = session.store.find("settlement")
    by_era = {}
    for s in setts:
        by_era.setdefault(s["founded_era"], []).append(s["founded_day"])
    days = sorted(min(v) for v in by_era.values())
    assert days == sorted(set(days)), "era founding days must be distinct"
    assert len(by_era) >= 2, "settlements must span multiple eras"
    road = np.frombuffer(
        session.result["cell_layers"]["cell_road"]["data"], np.float32)
    assert (road > 0).sum() > 10
    acc = np.frombuffer(
        session.result["cell_layers"]["cell_accessibility"]["data"], np.float32)
    assert 0 < (acc > 0.1).sum() < acc.size


def test_determinism(terrain, session):
    s2 = FakeSession(terrain)
    civ2.generate_civilization(s2)
    key = ("name", "cell", "rank", "founded_era", "population")

    def sig(store):
        return sorted(tuple(s[k] for k in key)
                      for s in store.find("settlement"))
    assert sig(s2.store) == sig(session.store)


def test_phase_cache_and_dirty_cone(terrain):
    s = FakeSession(terrain)
    r1 = civ2.generate_civilization(s)
    assert not any(p["cached"] for p in r1["phases"])
    r2 = civ2.generate_civilization(s)
    assert all(p["cached"] for p in r2["phases"])
    # touching only the polity constraint leaves everything upstream cached
    spec = _spec()
    spec["polities"]["foreign_culture_cost"] = 6.0
    s.civ_spec = spec
    r3 = civ2.generate_civilization(s)
    state = {p["phase"]: p["cached"] for p in r3["phases"]}
    assert state["species"] and state["cultures"] and state["hierarchy"]
    assert not state["polities"] and not state["assemble"]


def test_species_mask_constrains_settlements(terrain):
    s = FakeSession(terrain)
    z = np.frombuffer(terrain["cell_layers"]["cell_elevation_m"]["data"],
                      np.float32)
    ocean = np.frombuffer(terrain["cell_layers"]["cell_ocean"]["data"],
                          np.float32) > 0.5
    thr = float(np.percentile(z[~ocean], 82))
    g = weterrain.geodesic_graph(
        terrain["cell_layers"]["cell_elevation_m"]["frequency"])
    centers = np.frombuffer(g["centers_f64"], np.float64).reshape(
        g["cell_count"], 3)
    peak = int(np.argmax(np.where(ocean, -1e9, z)))
    lon0 = math.degrees(math.atan2(centers[peak, 2], centers[peak, 0]))
    lat0 = math.degrees(math.asin(centers[peak, 1]))
    box = [[lon0 - 40, max(-85, lat0 - 30)], [lon0 + 40, max(-85, lat0 - 30)],
           [lon0 + 40, min(85, lat0 + 30)], [lon0 - 40, min(85, lat0 + 30)]]
    spec = _spec()
    spec["species"].append({
        "id": "highlander", "name": "高地人",
        "description": "雪豹兽人式：只在特定山地区域内出没。",
        "filter": {
            "hard": [{"field": "is_land", "op": "eq", "value": 1},
                     {"field": "elevation_m", "op": "ge", "value": thr}],
            "soft": [{"field": "elevation_m", "fn": "min1", "scale": 4000.0,
                      "weight": 1.0}],
        },
        "masks": [{"polygon": box, "mode": "include"}],
        "share": 3.0, "min_separation_deg": 2.0,
    })
    s.civ_spec = spec
    civ2.generate_civilization(s)
    ctx = civ2.Ctx(s)
    sp = spec["species"][1]
    assert (civ2.suitability(ctx, sp) > 0).sum() > 0, \
        "mask ∩ filter must hit somewhere for this test to be meaningful"
    from worldstore import stable_id
    hid = stable_id("species", "highlander")
    mask = civ2.polygon_mask(centers, box)
    high = [t for t in s.store.find("settlement") if t["species_id"] == hid]
    assert high, "highlander settlements must exist"
    for t in high:
        assert z[t["cell"]] >= thr
        assert mask[t["cell"]], "settlement escaped its region mask"


def test_lock_survives_regeneration(terrain):
    s = FakeSession(terrain)
    civ2.generate_civilization(s)
    target = s.store.find("settlement")[0]
    s.store.set_attr(target["id"], "name", "不灭之城")
    s.store.lock(target["id"], "name")
    s.phase_cache = {}  # force full recompute — merge must still protect
    civ2.generate_civilization(s)
    assert s.store.get(target["id"], "name") == "不灭之城"
    names = [f["properties"]["name"] for f in s.features
             if f["kind"] == "settlement"]
    assert "不灭之城" in names


def test_geoquery_compat(session):
    import geoquery
    geo = geoquery.GeoIndex(session)
    t = session.store.find("settlement")[0]
    d = geo.describe(t["lon"], t["lat"])
    assert "polity" in d and "culture" in d
    assert d["nearest_settlement"]["distance_km"] < 300
