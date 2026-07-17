"""M12 gates: user-over-gen precedence, temporal resolution, calendars/eras,
regeneration merge that never destroys user edits, serialization roundtrip."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "terrain_server"))

from worldstore import Calendar, GEN, WorldStore, stable_id  # noqa: E402


def test_user_beats_gen():
    s = WorldStore()
    e = s.create("settlement", stable_id("settlement", 42))
    s.set_attr(e.id, "name", "Genburg", source=GEN)
    assert s.get(e.id, "name") == "Genburg"
    s.set_attr(e.id, "name", "Authorton")
    assert s.get(e.id, "name") == "Authorton"
    # refreshed gen value still loses
    s.set_attr(e.id, "name", "Genburg2", source=GEN)
    assert s.get(e.id, "name") == "Authorton"


def test_temporal_resolution_and_eras():
    s = WorldStore()
    e = s.create("settlement")
    s.set_attr(e.id, "name", "Oldtown", valid_from=0, valid_to=1000)
    s.set_attr(e.id, "name", "Newtown", valid_from=1000)
    assert s.get(e.id, "name", as_of=500) == "Oldtown"
    assert s.get(e.id, "name", as_of=1500) == "Newtown"
    assert s.get(e.id, "name") in ("Oldtown", "Newtown")  # timeless view defined
    s.add_era("First Age", 0, 1000)
    s.add_era("Second Age", 1000, None)
    assert s.era_of(500) == "First Age"
    assert s.era_of(2500) == "Second Age"


def test_calendar_roundtrip():
    cal = Calendar("Imperial", [("Frostwane", 30), ("Suncrest", 31), ("Harvest", 30)],
                   "IE")
    day = cal.date_to_day(12, 1, 17)
    assert cal.day_to_date(day) == (12, 1, 17)
    assert "Suncrest" in cal.format_day(day)
    cal2 = Calendar.from_spec(cal.spec())
    assert cal2.year_days == cal.year_days


def test_regeneration_merge_protects_user_edits():
    s = WorldStore()
    batch1 = [
        {"kind": "settlement", "gen_key": 1, "attrs": {"name": "Ara", "pop": 100}},
        {"kind": "settlement", "gen_key": 2, "attrs": {"name": "Bel", "pop": 200}},
        {"kind": "settlement", "gen_key": 3, "attrs": {"name": "Cor", "pop": 300}},
    ]
    s.apply_generation(batch1)
    e1 = stable_id("settlement", 1)
    e2 = stable_id("settlement", 2)
    e3 = stable_id("settlement", 3)

    s.set_attr(e1, "name", "Aravel")       # user rename
    s.lock(e1, "name")                      # and lock the field
    s.lock(e2)                              # whole-entity lock
    s.set_attr(e3, "notes", "hero was born here")  # user annotation only

    batch2 = [
        {"kind": "settlement", "gen_key": 1, "attrs": {"name": "Ara2", "pop": 150}},
        {"kind": "settlement", "gen_key": 3, "attrs": {"name": "Cor2", "pop": 350}},
        {"kind": "settlement", "gen_key": 4, "attrs": {"name": "Dun", "pop": 50}},
    ]
    stats = s.apply_generation(batch2)
    assert stats["created"] == 1

    assert s.get(e1, "name") == "Aravel"    # locked field survived
    assert s.get(e1, "pop") == 150          # unlocked field refreshed
    assert s.get(e2, "name") == "Bel"       # whole lock: untouched
    assert s.get(e2, "pop") == 200
    assert s.get(e2, "_retired") is not True  # locked absentee kept
    assert s.get(e3, "name") == "Cor2"      # gen refresh fine
    assert s.get(e3, "notes") == "hero was born here"
    assert s.get(e3, "_retired") is not True  # user-touched absentee... present anyway
    # ids stable across regenerations
    assert stable_id("settlement", 1) == e1


def test_retire_absent_gen_entities():
    s = WorldStore()
    s.apply_generation([{"kind": "road", "gen_key": "a-b", "attrs": {"len": 10}}])
    rid = stable_id("road", "a-b")
    s.apply_generation([{"kind": "road", "gen_key": "c-d", "attrs": {"len": 5}}])
    assert s.get(rid, "_retired") is True
    assert all(e["id"] != rid for e in s.find("road"))
    assert any(e["id"] == rid for e in s.find("road", include_retired=True))
    # regen brings it back
    s.apply_generation([{"kind": "road", "gen_key": "a-b", "attrs": {"len": 12}}])
    assert s.get(rid, "_retired") is False


def test_serialization_roundtrip():
    s = WorldStore()
    s.set_calendar(Calendar("Cal", [("M1", 30), ("M2", 30)]))
    s.add_era("Age", 0, None)
    s.apply_generation([{"kind": "culture", "gen_key": 7,
                         "attrs": {"name": "Velic"}}])
    cid = stable_id("culture", 7)
    s.set_attr(cid, "name", "Velician")
    s.lock(cid, "name")
    rows, meta, log = s.to_rows()
    s2 = WorldStore.from_rows(rows, meta, log)
    assert s2.get(cid, "name") == "Velician"
    assert "name" in s2.entities[cid].locked
    assert s2.calendar.year_days == 60
    assert s2.era_of(10) == "Age"


def test_find_by_name():
    s = WorldStore()
    s.apply_generation([
        {"kind": "settlement", "gen_key": 1, "attrs": {"name": "Rivermouth"}},
        {"kind": "settlement", "gen_key": 2, "attrs": {"name": "Hilltop"}},
    ])
    hits = s.find("settlement", name_like="river")
    assert len(hits) == 1 and hits[0]["name"] == "Rivermouth"
