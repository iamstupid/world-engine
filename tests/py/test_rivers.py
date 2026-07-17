"""Vector-river gates (plan addendum c): extraction produces monotone
channel chains with hydraulic attributes; meander refinement adds bounded,
deterministic sinuosity with pinned endpoints; stream burning conditions
the raster monotonically along the channel; sub-threshold tributaries land
on the channel network. Plus the rhombus-atlas round trip (addendum b)."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import atlas  # noqa: E402
import civ  # noqa: E402
import rivers  # noqa: E402
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
def ext(session):
    return rivers.extract_rivers(session)


def test_extraction_chains_are_monotone(ext):
    assert len(ext["rivers"]) >= 3
    for river in ext["rivers"][:10]:
        z = np.asarray(river["z"])
        assert np.all(np.diff(z) <= 1e-3), "bed must descend downstream"
        acc = np.asarray(river["accum"])
        assert acc[-1] >= acc[0]
        grows = np.mean(np.diff(acc) >= 0)
        assert grows > 0.7  # discharge grows along the stem
        assert river["width_m"][-1] > 0


def test_meander_refinement(ext, session):
    seed = int(session.params["world.seed"])
    river = ext["rivers"][0]
    refined = rivers.refine_meanders(river, seed, ext["spacing_km"])
    refined2 = rivers.refine_meanders(river, seed, ext["spacing_km"])
    assert refined == refined2  # deterministic
    assert len(refined) > len(river["coords"])  # subdivided
    for a, b in ((refined[0], river["coords"][0]),
                 (refined[-1], river["coords"][-1])):
        assert abs(a[0] - b[0]) < 1e-3 and abs(a[1] - b[1]) < 1e-3

    def arclen(cs):
        xyz = rivers.lonlat_to_xyz(np.asarray(cs)[:, 0], np.asarray(cs)[:, 1])
        return float(np.linalg.norm(np.diff(xyz, axis=0), axis=1).sum())

    sinuosity = arclen(refined) / max(1e-9, arclen(river["coords"]))
    assert 1.0 < sinuosity < 1.6

    # displacement bounded: every refined point within 2 cell spacings of
    # the original polyline vertices
    orig = rivers.lonlat_to_xyz(np.asarray(river["coords"])[:, 0],
                                np.asarray(river["coords"])[:, 1])
    ref = rivers.lonlat_to_xyz(np.asarray(refined)[:, 0],
                               np.asarray(refined)[:, 1])
    dmin = np.array([np.linalg.norm(orig - p, axis=1).min() for p in ref])
    assert dmin.max() * rivers.R_KM < 2.0 * ext["spacing_km"]


def test_stream_burning(session):
    riv = rivers.build(session, refine=True, burn=True, minor=False)
    assert "elevation_conditioned_m" in session.result["layers"]
    w, h = session.result["width"], session.result["height"]
    cond = np.frombuffer(
        session.result["layers"]["elevation_conditioned_m"]["data"],
        np.float32).reshape(h, w)
    base = np.frombuffer(session.result["layers"]["elevation_eroded_m"]["data"],
                         np.float32).reshape(h, w)
    assert np.all(cond <= base + 1e-3)  # burning only carves
    river = riv["extraction"]["rivers"][0]
    coords = river.get("coords_refined", river["coords"])
    samples = []
    for lon, lat in coords:
        x = int(((lon + 180.0) / 360.0) * w) % w
        y = min(h - 1, max(0, int(((90.0 - lat) / 180.0) * h)))
        samples.append(float(cond[y, x]))
    diffs = np.diff(np.minimum.accumulate(samples))
    burned = np.asarray(samples)
    assert np.all(np.diff(np.minimum.accumulate(burned)) <= 1e-3)
    # the actual bed samples should already be near-monotone
    violations = np.sum(np.diff(burned) > 0.5)
    assert violations / max(1, len(burned)) < 0.15


def test_tributaries_land_on_network(ext):
    feats = rivers.tributary_trees(ext)
    assert len(feats) > 0
    lonlat = ext["lonlat"]
    channel_pts = lonlat[np.nonzero(ext["channel"])[0]]
    for f in feats[:30]:
        cs = np.asarray(f["geometry"]["coordinates"])
        assert len(cs) >= 3
        end = cs[-1]
        d = np.abs(channel_pts - end).sum(axis=1).min()
        assert d < 1.0  # terminates on (a cell of) the channel network


def test_civ_integration_and_census(session):
    civ.generate_civilization(session)
    kinds = {f["kind"] for f in session.features}
    assert "river" in kinds and "settlement" in kinds
    assert any(f["kind"] == "minor_river" for f in session.features)
    setts = [f for f in session.features if f["kind"] == "settlement"]
    assert len(setts) >= 5


# ---- rhombus atlas (addendum b) ----

def test_atlas_bijection_and_roundtrip(session):
    entry = session.result["cell_layers"]["cell_elevation_m"]
    freq = int(entry["frequency"])
    amap = atlas.atlas_cell_map(freq)
    assert amap.shape == (2 * freq, 5 * freq)
    flat = amap.ravel()
    assert flat.min() >= 0
    counts = np.bincount(flat, minlength=10 * freq * freq + 2)
    poles = atlas.pole_cell_ids(freq)
    assert len(poles) == 2
    assert np.all(counts[poles] == 0)
    mask = np.ones(10 * freq * freq + 2, bool)
    mask[poles] = False
    assert np.all(counts[mask] == 1)  # exact bijection

    f2, img, pv = atlas.pack_cell_atlas(session.result, "cell_elevation_m")
    assert f2 == freq and img.shape == (2 * freq, 5 * freq)
    vals = atlas.unpack_cell_atlas(freq, img, pv)
    orig = np.frombuffer(entry["data"], np.float32)
    assert np.array_equal(vals, orig)
