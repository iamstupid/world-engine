"""M13 gates: route travel times (mode ordering, rough symmetry), reachable
isochrone growth, describe context packs with entity resolution, news
arrival, viewshed sanity, and the M12<->civ merge working end to end.
Headless: kernel + civ + geoquery, no server, no browser."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import civ  # noqa: E402
import geoquery  # noqa: E402
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
    civ.generate_civilization(s)
    return s


@pytest.fixture(scope="module")
def geo(session):
    return geoquery.GeoIndex(session)


def _two_settlements_same_polity(session):
    setts = session.store.find("settlement")
    by_pol = {}
    for s in setts:
        by_pol.setdefault(s.get("polity_id"), []).append(s)
    for group in sorted(by_pol.values(), key=len, reverse=True):
        if len(group) >= 2:
            group.sort(key=lambda s: -(s.get("rank") == 0))
            return group[0], group[1]
    return setts[0], setts[1]


def test_route_modes_and_symmetry(session, geo):
    a, b = _two_settlements_same_polity(session)
    pa, pb = (a["lon"], a["lat"]), (b["lon"], b["lat"])
    walk = geo.route(pa, pb, "walk")
    horse = geo.route(pa, pb, "horse")
    assert walk["reachable"] and horse["reachable"]
    assert walk["days"] > horse["days"] > 0
    back = geo.route(pb, pa, "horse")
    assert back["reachable"]
    assert abs(back["days"] - horse["days"]) / horse["days"] < 0.25
    assert horse["distance_km"] > 0
    assert isinstance(horse["narrative"], list)


def test_ship_route_between_coasts(session, geo):
    ocean_cells = np.nonzero(geo.ocean)[0]
    a = geo.cell_lonlat(int(ocean_cells[10]))
    b = geo.cell_lonlat(int(ocean_cells[len(ocean_cells) // 2]))
    ship = geo.route(a, b, "ship")
    assert ship["reachable"] and ship["days"] > 0


def test_reachable_grows_with_days(session, geo):
    a, _ = _two_settlements_same_polity(session)
    r5 = geo.reachable((a["lon"], a["lat"]), 5, "horse")
    r15 = geo.reachable((a["lon"], a["lat"]), 15, "horse")
    assert r15["area_km2"] > r5["area_km2"] > 0


def test_describe_resolves_entities(session, geo):
    a, _ = _two_settlements_same_polity(session)
    d = geo.describe(a["lon"], a["lat"])
    assert d["biome"] != "ocean"
    assert "polity" in d and isinstance(d["polity"], str)
    assert "culture" in d
    assert d["nearest_settlement"]["distance_km"] < 300
    # user rename must flow through to describe
    session.store.set_attr(
        next(e for e in session.store.find("settlement")
             if e["name"] == d["nearest_settlement"]["name"])["id"],
        "name", "Renamed-by-Author")
    geo2 = geoquery.GeoIndex(session)
    d2 = geo2.describe(a["lon"], a["lat"])
    assert d2["nearest_settlement"]["name"] == "Renamed-by-Author"


def test_news_arrival(session, geo):
    a, b = _two_settlements_same_polity(session)
    news = geo.news_arrival((a["lon"], a["lat"]), (b["lon"], b["lat"]))
    assert news["reachable"]
    horse = geo.route((a["lon"], a["lat"]), (b["lon"], b["lat"]), "horse")
    assert news["days"] <= horse["days"] + 0.6  # couriers are not slower


def test_viewshed(session, geo):
    a, _ = _two_settlements_same_polity(session)
    v = geo.viewshed(a["lon"], a["lat"])
    assert v["horizon_km_median"] > 0


def test_civ_store_regeneration_merge(session):
    setts = session.store.find("settlement")
    target = setts[0]
    session.store.set_attr(target["id"], "name", "Immortal City")
    session.store.lock(target["id"], "name")
    civ.generate_civilization(session)  # full regen
    assert session.store.get(target["id"], "name") == "Immortal City"
    names = [f["properties"]["name"] for f in session.features
             if f["kind"] == "settlement"]
    assert "Immortal City" in names
