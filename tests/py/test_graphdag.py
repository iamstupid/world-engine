"""DAG pipeline gates: hard frequency validation, cycle/reference errors,
content-addressed caching (edits recompute only the downstream cone), the
IGM-native template end to end, and operator correctness (resample keeps
constants, math/mask numerics)."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import graphdag  # noqa: E402
from worldstore import WorldStore  # noqa: E402


class FakeSession:
    def __init__(self):
        self.params = {"world.seed": 909, "world.width": 256,
                       "world.height": 128}
        self.paint = {}
        self.store = WorldStore()


@pytest.fixture()
def session():
    return FakeSession()


def test_frequency_mismatch_rejected():
    spec = {"nodes": [
        {"id": "n1", "type": "noise", "params": {"frequency": 88}},
        {"id": "n2", "type": "noise", "params": {"frequency": 176}},
        {"id": "m", "type": "math",
         "inputs": {"a": "n1.field", "b": "n2.field"}}]}
    with pytest.raises(graphdag.GraphError, match="频率不匹配"):
        graphdag.infer_frequencies(spec)


def test_cycle_and_bad_refs_rejected():
    with pytest.raises(graphdag.GraphError, match="环"):
        graphdag.infer_frequencies({"nodes": [
            {"id": "a", "type": "mask", "inputs": {"src": "b.out"}},
            {"id": "b", "type": "mask", "inputs": {"src": "a.out"}}]})
    with pytest.raises(graphdag.GraphError, match="不存在的输出"):
        graphdag.infer_frequencies({"nodes": [
            {"id": "n", "type": "noise"},
            {"id": "m", "type": "mask", "inputs": {"src": "n.nope"}}]})


def test_operators_and_cache_cone(session):
    spec = {"nodes": [
        {"id": "n", "type": "noise",
         "params": {"frequency": 44, "amplitude_m": 100.0}},
        {"id": "n2", "type": "noise",
         "params": {"frequency": 44, "amplitude_m": 50.0, "seed_offset": 3}},
        {"id": "s", "type": "math", "params": {"op": "add"},
         "inputs": {"a": "n.field", "b": "n2.field"}},
        {"id": "k", "type": "mask", "params": {"threshold": 0.0},
         "inputs": {"src": "s.out"}},
        {"id": "r", "type": "resample", "params": {"frequency": 88},
         "inputs": {"src": "k.out"}},
        {"id": "e", "type": "export", "params": {"name": "test_field"},
         "inputs": {"field": "r.out"}}]}
    out1 = graphdag.execute(spec, session)
    assert set(out1["evaluated"]) == {"n", "n2", "s", "k", "r", "e"}
    assert "test_field" in out1["layers"]
    assert "cell_test_field" in out1["cell_layers"]
    mask_vals = np.frombuffer(
        out1["cell_layers"]["cell_test_field"]["data"], np.float32)
    assert 0.0 <= mask_vals.min() and mask_vals.max() <= 1.0

    out2 = graphdag.execute(spec, session)
    assert out2["evaluated"] == []          # full cache hit

    spec["nodes"][1]["params"]["amplitude_m"] = 60.0  # edit n2
    out3 = graphdag.execute(spec, session)
    assert set(out3["evaluated"]) == {"n2", "s", "k", "r", "e"}
    assert "n" not in out3["evaluated"]     # only the downstream cone


def test_resample_preserves_constant(session):
    import weterrain
    const = np.full(10 * 44 * 44 + 2, 7.5, np.float32)
    raw = weterrain.resample_cells(44, const.tobytes(), 88)
    out = np.frombuffer(raw, np.float32)
    assert out.shape[0] == 10 * 88 * 88 + 2
    assert np.allclose(out, 7.5, atol=1e-4)


def test_template_end_to_end(session):
    spec = graphdag.default_template()
    for node in spec["nodes"]:
        if node["type"] == "tectonics":
            node["params"].update({"grid_frequency": 24,
                                   "simulation_steps": 60})
        if node["type"] == "physics":
            node["params"].update({"frequency": 88,
                                   "fixed_point_iterations": 4,
                                   "multigrid_levels": 4})
        if node["type"] in ("resample", "noise"):
            node["params"]["frequency"] = 88
    out = graphdag.execute(spec, session)
    for name in ("elevation_eroded_m", "temperature_c", "biome_id"):
        assert name in out["layers"]
    for name in ("cell_elevation_m", "cell_biome", "cell_ocean"):
        assert name in out["cell_layers"]
    ocean = np.frombuffer(out["cell_layers"]["cell_ocean"]["data"], np.float32)
    frac = float((ocean > 0.5).mean())
    assert 0.15 < frac < 0.97
    # determinism: same spec on a fresh session gives the same hash
    s2 = FakeSession()
    out2 = graphdag.execute(spec, s2)
    z1 = np.frombuffer(out["cell_layers"]["cell_elevation_m"]["data"], np.float32)
    z2 = np.frombuffer(out2["cell_layers"]["cell_elevation_m"]["data"], np.float32)
    assert np.array_equal(z1, z2)
