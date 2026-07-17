"""M14 skeleton gates: staged changesets never touch canon until merged,
merged attrs are story-date-aware, character location trails resolve by
day, and the consistency checks catch impossible travel and
faster-than-news knowledge while passing feasible versions."""

import sys
from pathlib import Path

import numpy as np
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))
sys.path.insert(0, str(REPO / "tools" / "terrain_server"))

import weterrain  # noqa: E402
import civ  # noqa: E402
import novelkit  # noqa: E402
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
        self.geo = None


@pytest.fixture(scope="module")
def session():
    s = FakeSession()
    p = weterrain.Params()
    for k, v in s.params.items():
        p.set(k, v)
    s.result = weterrain.generate(p)
    civ.generate_civilization(s)
    return s


@pytest.fixture()
def project(session):
    return novelkit.Project("test-novel", session)


def _two_settlements(session):
    setts = session.store.find("settlement")
    by_pol = {}
    for s in setts:
        by_pol.setdefault(s.get("polity_id"), []).append(s)
    for group in sorted(by_pol.values(), key=len, reverse=True):
        if len(group) >= 2:
            return group[0], group[1]
    return setts[0], setts[1]


def test_changeset_staging_isolation(project, session):
    sett = session.store.find("settlement")[0]
    old_name = sett["name"]
    cs = project.stage("rename the capital", 100.0, [
        {"action": "set_attr", "entity": sett["id"], "attr": "name",
         "value": "Renamed-at-100"}])
    # staged: canon unchanged, preview shows it
    assert session.store.get(sett["id"], "name") == old_name
    preview = project.preview(cs)
    assert preview.get(sett["id"], "name", as_of=150) == "Renamed-at-100"
    assert session.store.get(sett["id"], "name") == old_name
    # merged: story-date-aware in canon
    project.merge(cs)
    assert session.store.get(sett["id"], "name", as_of=150) == "Renamed-at-100"
    assert session.store.get(sett["id"], "name", as_of=50) == old_name
    assert [c["id"] for c in project.timeline()] == [cs]


def test_drop_staged_only(project, session):
    sett = session.store.find("settlement")[0]
    cs = project.stage("tentative", 5.0, [
        {"action": "set_attr", "entity": sett["id"], "attr": "mood",
         "value": "gloomy"}])
    project.drop(cs)
    assert all(c["id"] != cs for c in project.changesets)
    cs2 = project.stage("kept", 5.0, [
        {"action": "set_attr", "entity": sett["id"], "attr": "mood",
         "value": "bright"}])
    project.merge(cs2)
    with pytest.raises(ValueError):
        project.drop(cs2)


def test_character_trail(project):
    cid = project.create_character("Aris", 10.0, 20.0, day=0.0,
                                   attrs={"age": 17})
    project.set_location(cid, 30.0, 11.0, 21.0)
    assert project.location_of(cid, 15.0)["lon"] == 10.0
    assert project.location_of(cid, 40.0)["lon"] == 11.0
    days = [d for d, _ in project.trail(cid)]
    assert days == sorted(days) and len(days) == 2


def test_travel_consistency(project, session):
    a, b = _two_settlements(session)
    geo = project._geo()
    route = geo.route((a["lon"], a["lat"]), (b["lon"], b["lat"]), "horse")
    assert route["reachable"]
    days = route["days"]

    cid = project.create_character("Kestrel", a["lon"], a["lat"], day=0.0)
    project.set_location(cid, max(0.5, days * 0.3), b["lon"], b["lat"])
    violations = project.check_travel(cid)
    assert any(v["type"] == "too_fast" for v in violations)

    cid2 = project.create_character("Patient", a["lon"], a["lat"], day=0.0)
    project.set_location(cid2, days * 1.5 + 1.0, b["lon"], b["lat"])
    assert project.check_travel(cid2) == []


def test_knowledge_consistency(project, session):
    a, b = _two_settlements(session)
    geo = project._geo()
    news = geo.news_arrival((a["lon"], a["lat"]), (b["lon"], b["lat"]))
    assert news["reachable"]

    cid = project.create_character("Rumor-monger", b["lon"], b["lat"], day=0.0)
    events = [{"day": 100.0, "lon": a["lon"], "lat": a["lat"],
               "label": "the fall of the keep",
               "learns": [{"char": cid, "day": 100.0 + news["days"] * 0.3}]}]
    assert any(v["type"] == "knows_too_early"
               for v in project.check_knowledge(events))
    events[0]["learns"][0]["day"] = 100.0 + news["days"] + 2.0
    assert project.check_knowledge(events) == []


def test_project_roundtrip(project, session, tmp_path):
    sett = session.store.find("settlement")[0]
    cs = project.stage("chronicle", 7.0, [
        {"action": "set_attr", "entity": sett["id"], "attr": "note",
         "value": "founded anew"}])
    project.merge(cs)
    path = tmp_path / "novel.weproj"
    project.save(path)
    loaded = novelkit.Project.load(path, session)
    assert loaded.name == project.name
    assert [c["id"] for c in loaded.timeline()] == [cs]
