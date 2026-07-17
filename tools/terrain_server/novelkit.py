"""Novel-suite backend skeleton (plan M14): the Project root unit, a canon
timeline of story-dated changesets (staged -> author-merged, never applied
behind the author's back), characters as first-class store entities with
location trails, and geography-aware consistency checks that use the M13
query layer (travel feasibility, news arrival).

Design contract (plan addendum b): story state = ordered changesets at
story dates on the canon calendar; AI/agents only ever write STAGED
changesets; merging is an explicit author action. All writes go through
WorldStore so locks and user-precedence semantics keep working.
"""

from __future__ import annotations

import json
import uuid

from worldstore import WorldStore

STAGED = "staged"
MERGED = "merged"


class Project:
    """Root unit binding a world session to a manuscript timeline."""

    def __init__(self, name: str, session):
        self.name = name
        self.session = session
        self.changesets: list[dict] = []

    # ---- story-state changesets ----

    def stage(self, label: str, story_day: float, ops: list[dict]) -> str:
        cs_id = uuid.uuid4().hex[:10]
        self.changesets.append({"id": cs_id, "label": label,
                                "story_day": float(story_day),
                                "ops": ops, "status": STAGED})
        return cs_id

    def _find(self, cs_id: str) -> dict:
        for cs in self.changesets:
            if cs["id"] == cs_id:
                return cs
        raise KeyError(cs_id)

    @staticmethod
    def _apply_ops(store: WorldStore, cs: dict) -> None:
        for op in cs["ops"]:
            action = op.get("action", "set_attr")
            if action == "create":
                store.create(op["kind"], op.get("entity"))
            elif action == "set_attr":
                store.set_attr(op["entity"], op["attr"], op["value"],
                               valid_from=op.get("valid_from", cs["story_day"]),
                               valid_to=op.get("valid_to"))
            elif action == "lock":
                store.lock(op["entity"], op.get("attr", "*"))
            elif action == "unlock":
                store.unlock(op["entity"], op.get("attr", "*"))

    def preview(self, cs_id: str) -> WorldStore:
        """A staged changeset applied to a CLONE of canon (never canon)."""
        cs = self._find(cs_id)
        rows, meta, log = self.session.store.to_rows()
        clone = WorldStore.from_rows(rows, meta, log)
        self._apply_ops(clone, cs)
        return clone

    def merge(self, cs_id: str) -> dict:
        """Author-approved: apply to canon and mark merged."""
        cs = self._find(cs_id)
        if cs["status"] == MERGED:
            return cs
        self._apply_ops(self.session.store, cs)
        cs["status"] = MERGED
        return cs

    def drop(self, cs_id: str) -> None:
        cs = self._find(cs_id)
        if cs["status"] == MERGED:
            raise ValueError("cannot drop a merged changeset")
        self.changesets.remove(cs)

    def timeline(self) -> list[dict]:
        merged = [cs for cs in self.changesets if cs["status"] == MERGED]
        return sorted(merged, key=lambda cs: cs["story_day"])

    # ---- characters ----

    def create_character(self, name: str, lon: float, lat: float,
                         day: float = 0.0, attrs: dict | None = None) -> str:
        ent = self.session.store.create("character")
        self.session.store.set_attr(ent.id, "name", name)
        for key, value in (attrs or {}).items():
            self.session.store.set_attr(ent.id, key, value)
        self.set_location(ent.id, day, lon, lat)
        return ent.id

    def set_location(self, char_id: str, day: float, lon: float, lat: float,
                     place: str | None = None) -> None:
        self.session.store.set_attr(char_id, "location",
                                    {"lon": float(lon), "lat": float(lat),
                                     "place": place},
                                    valid_from=float(day))

    def location_of(self, char_id: str, day: float):
        return self.session.store.get(char_id, "location", as_of=day)

    def trail(self, char_id: str) -> list[tuple[float, dict]]:
        ent = self.session.store.entities[char_id]
        out = []
        for e in ent.attrs.get("location", []):
            if e.valid_from is not None:
                out.append((float(e.valid_from), e.value))
        out.sort(key=lambda t: t[0])
        return out

    # ---- consistency checks (geography-aware) ----

    def _geo(self):
        if getattr(self.session, "geo", None) is None:
            import geoquery
            self.session.geo = geoquery.GeoIndex(self.session)
        return self.session.geo

    def check_travel(self, char_id: str, mode: str = "horse",
                     slack: float = 0.85) -> list[dict]:
        """Flag consecutive trail hops that are faster than the route allows
        (route days scaled by `slack` to forgive narrative rounding)."""
        geo = self._geo()
        trail = self.trail(char_id)
        name = self.session.store.get(char_id, "name", default=char_id)
        violations = []
        for (d0, p0), (d1, p1) in zip(trail, trail[1:]):
            r = geo.route((p0["lon"], p0["lat"]), (p1["lon"], p1["lat"]), mode)
            if not r["reachable"]:
                r_ship = geo.route((p0["lon"], p0["lat"]),
                                   (p1["lon"], p1["lat"]), "ship")
                r = r_ship if r_ship["reachable"] else r
            if not r["reachable"]:
                violations.append({"type": "unreachable", "character": name,
                                   "from_day": d0, "to_day": d1})
                continue
            needed = r["days"] * slack
            if (d1 - d0) < needed:
                violations.append({
                    "type": "too_fast", "character": name,
                    "from_day": d0, "to_day": d1,
                    "elapsed_days": round(d1 - d0, 1),
                    "needed_days": round(r["days"], 1),
                    "mode": r.get("mode", mode),
                    "distance_km": r.get("distance_km")})
        return violations

    def check_knowledge(self, events: list[dict], slack: float = 0.8) -> list[dict]:
        """events: {day, lon, lat, label, learns: [{char, day}]} — a
        character cannot know of an event before news can reach them."""
        geo = self._geo()
        violations = []
        for ev in events:
            for learn in ev.get("learns", []):
                char_id = learn["char"]
                loc = self.location_of(char_id, learn["day"])
                if loc is None:
                    continue
                news = geo.news_arrival((ev["lon"], ev["lat"]),
                                        (loc["lon"], loc["lat"]))
                if not news["reachable"]:
                    continue
                earliest = ev["day"] + news["days"] * slack
                if learn["day"] < earliest:
                    name = self.session.store.get(char_id, "name",
                                                  default=char_id)
                    violations.append({
                        "type": "knows_too_early", "character": name,
                        "event": ev.get("label", "event"),
                        "event_day": ev["day"], "learn_day": learn["day"],
                        "earliest_day": round(earliest, 1)})
        return violations

    # ---- persistence ----

    def to_dict(self) -> dict:
        return {"name": self.name, "changesets": self.changesets}

    @staticmethod
    def from_dict(data: dict, session) -> "Project":
        p = Project(data["name"], session)
        p.changesets = list(data.get("changesets", []))
        return p

    def save(self, path) -> None:
        path.write_text(json.dumps(self.to_dict(), ensure_ascii=False,
                                   indent=1), encoding="utf-8")

    @staticmethod
    def load(path, session) -> "Project":
        return Project.from_dict(
            json.loads(path.read_text(encoding="utf-8")), session)
