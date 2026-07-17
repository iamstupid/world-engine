"""World entity store (plan M12): stable-id entities with gen/user attribute
layers, field locks, story-calendar time (validity intervals + eras + custom
calendars), an append-only edit log, and regeneration merge semantics -
re-running a generator NEVER destroys user edits.

Backed by plain dicts for speed; serialized into the .weworld SQLite
container. The novel suite consumes this same module later.
"""

from __future__ import annotations

import hashlib
import json
import time
import uuid
from dataclasses import dataclass, field

GEN = "gen"
USER = "user"


def stable_id(kind: str, gen_key: str | int, namespace: int = 0) -> str:
    h = hashlib.sha1(f"{namespace}|{kind}|{gen_key}".encode()).hexdigest()[:16]
    return f"{kind}:{h}"


@dataclass
class AttrEntry:
    value: object
    source: str = GEN
    valid_from: float | None = None  # story-calendar absolute day; None = open
    valid_to: float | None = None
    rev: int = 0

    def matches(self, as_of: float | None) -> bool:
        if as_of is None:
            return True
        if self.valid_from is not None and as_of < self.valid_from:
            return False
        if self.valid_to is not None and as_of >= self.valid_to:
            return False
        return True

    def timeless(self) -> bool:
        return self.valid_from is None and self.valid_to is None


@dataclass
class Entity:
    id: str
    kind: str
    attrs: dict[str, list[AttrEntry]] = field(default_factory=dict)
    locked: set[str] = field(default_factory=set)  # attr names; "*" = whole entity

    def has_user_data(self) -> bool:
        return bool(self.locked) or any(
            e.source == USER for entries in self.attrs.values() for e in entries)


class Calendar:
    """Regular custom calendar: named months with day counts (no leap rules
    in v1 - fantasy calendars are usually exact)."""

    def __init__(self, name: str, months: list[tuple[str, int]],
                 epoch_label: str = "Y"):
        self.name = name
        self.months = months
        self.epoch_label = epoch_label
        self.year_days = sum(d for _, d in months)

    def day_to_date(self, day: float) -> tuple[int, int, int]:
        d = int(day)
        year, rem = divmod(d, self.year_days)
        for mi, (_, md) in enumerate(self.months):
            if rem < md:
                return year, mi, rem + 1
            rem -= md
        return year, len(self.months) - 1, self.months[-1][1]

    def date_to_day(self, year: int, month_idx: int, day_in_month: int) -> float:
        return (year * self.year_days +
                sum(d for _, d in self.months[:month_idx]) + (day_in_month - 1))

    def format_day(self, day: float) -> str:
        y, m, d = self.day_to_date(day)
        return f"{d} {self.months[m][0]}, {y} {self.epoch_label}"

    def spec(self) -> dict:
        return {"name": self.name, "months": self.months,
                "epoch_label": self.epoch_label}

    @staticmethod
    def from_spec(spec: dict) -> "Calendar":
        return Calendar(spec["name"], [tuple(m) for m in spec["months"]],
                        spec.get("epoch_label", "Y"))


class WorldStore:
    def __init__(self):
        self.entities: dict[str, Entity] = {}
        self.calendar: Calendar | None = None
        self.eras: list[dict] = []  # {name, start_day, end_day}
        self.log: list[dict] = []
        self._rev = 0

    # ---- mutation ----

    def _bump(self, op: str, payload: dict) -> int:
        self._rev += 1
        self.log.append({"rev": self._rev, "ts": time.time(), "op": op,
                         "payload": payload})
        return self._rev

    def create(self, kind: str, entity_id: str | None = None) -> Entity:
        eid = entity_id or f"{kind}:{uuid.uuid4().hex[:16]}"
        if eid in self.entities:
            return self.entities[eid]
        ent = Entity(eid, kind)
        self.entities[eid] = ent
        self._bump("create", {"id": eid, "kind": kind})
        return ent

    def set_attr(self, entity_id: str, name: str, value, source: str = USER,
                 valid_from: float | None = None, valid_to: float | None = None):
        ent = self.entities[entity_id]
        rev = self._bump("set_attr", {"id": entity_id, "name": name,
                                      "value": value, "source": source,
                                      "valid_from": valid_from,
                                      "valid_to": valid_to})
        entries = ent.attrs.setdefault(name, [])
        if source == GEN:
            # A generator owns exactly one timeless entry per attr.
            entries[:] = [e for e in entries
                          if not (e.source == GEN and e.timeless())] if (
                valid_from is None and valid_to is None) else entries
        entries.append(AttrEntry(value, source, valid_from, valid_to, rev))

    def lock(self, entity_id: str, name: str = "*"):
        self.entities[entity_id].locked.add(name)
        self._bump("lock", {"id": entity_id, "name": name})

    def unlock(self, entity_id: str, name: str = "*"):
        self.entities[entity_id].locked.discard(name)
        self._bump("unlock", {"id": entity_id, "name": name})

    # ---- resolution ----

    def get(self, entity_id: str, name: str, as_of: float | None = None,
            default=None):
        ent = self.entities.get(entity_id)
        if ent is None or name not in ent.attrs:
            return default
        candidates = [e for e in ent.attrs[name] if e.matches(as_of)]
        if not candidates:
            return default
        # user beats gen; later intervals beat timeless; higher rev breaks ties
        def rank(e: AttrEntry):
            return (e.source == USER, not e.timeless(),
                    e.valid_from if e.valid_from is not None else -1e18, e.rev)
        return max(candidates, key=rank).value

    def snapshot(self, entity_id: str, as_of: float | None = None) -> dict:
        ent = self.entities[entity_id]
        out = {"id": ent.id, "kind": ent.kind,
               "locked": sorted(ent.locked)}
        for name in ent.attrs:
            out[name] = self.get(entity_id, name, as_of)
        return out

    def find(self, kind: str | None = None, name_like: str | None = None,
             as_of: float | None = None, include_retired: bool = False):
        out = []
        for ent in self.entities.values():
            if kind is not None and ent.kind != kind:
                continue
            if not include_retired and self.get(ent.id, "_retired") is True:
                continue
            if name_like is not None:
                nm = str(self.get(ent.id, "name", as_of, ""))
                if name_like.lower() not in nm.lower():
                    continue
            out.append(self.snapshot(ent.id, as_of))
        return out

    # ---- time ----

    def set_calendar(self, cal: Calendar):
        self.calendar = cal
        self._bump("set_calendar", cal.spec())

    def add_era(self, name: str, start_day: float, end_day: float | None = None):
        self.eras.append({"name": name, "start_day": start_day, "end_day": end_day})
        self._bump("add_era", self.eras[-1])

    def era_of(self, day: float) -> str | None:
        for era in self.eras:
            if day >= era["start_day"] and (era["end_day"] is None or
                                            day < era["end_day"]):
                return era["name"]
        return None

    # ---- regeneration merge (the contract that keeps user edits safe) ----

    def apply_generation(self, records: list[dict], namespace: int = 0) -> dict:
        """records: {kind, gen_key, attrs: {...}}. Gen entities get stable ids
        from (namespace, kind, gen_key). Rules:
          - whole-entity lock ("*"): generator cannot touch it at all
          - field lock: that field keeps its value
          - user attr entries always outrank refreshed gen entries
          - gen entities absent from the new batch are retired unless they
            carry user data — scoped to the kinds present in THIS batch, so
            independent generators (civ, astro, ...) never retire each
            other's entities
        """
        seen = set()
        batch_kinds = {rec["kind"] for rec in records}
        created = updated = 0
        for rec in records:
            eid = stable_id(rec["kind"], rec["gen_key"], namespace)
            seen.add(eid)
            ent = self.entities.get(eid)
            if ent is None:
                ent = self.create(rec["kind"], eid)
                created += 1
            elif "*" in ent.locked:
                continue
            else:
                updated += 1
            for name, value in rec["attrs"].items():
                if name in ent.locked:
                    continue
                self.set_attr(eid, name, value, source=GEN)
            if self.get(eid, "_retired") is True:
                self.set_attr(eid, "_retired", False, source=GEN)
        retired = 0
        for ent in self.entities.values():
            if ent.kind not in batch_kinds:
                continue
            is_gen = any(e.source == GEN for es in ent.attrs.values() for e in es)
            if (is_gen and ent.id not in seen and "*" not in ent.locked and
                    not ent.has_user_data() and
                    self.get(ent.id, "_retired") is not True):
                self.set_attr(ent.id, "_retired", True, source=GEN)
                retired += 1
        self._bump("apply_generation", {"count": len(records),
                                        "created": created, "updated": updated,
                                        "retired": retired})
        return {"created": created, "updated": updated, "retired": retired}

    # ---- serialization (rows for the .weworld SQLite container) ----

    def to_rows(self):
        ent_rows = []
        for ent in self.entities.values():
            attrs = {n: [[e.value, e.source, e.valid_from, e.valid_to, e.rev]
                         for e in es] for n, es in ent.attrs.items()}
            ent_rows.append((ent.id, ent.kind, json.dumps(attrs),
                             json.dumps(sorted(ent.locked))))
        meta = {"calendar": self.calendar.spec() if self.calendar else None,
                "eras": self.eras, "rev": self._rev}
        return ent_rows, json.dumps(meta), json.dumps(self.log[-5000:])

    @staticmethod
    def from_rows(ent_rows, meta_json: str, log_json: str) -> "WorldStore":
        store = WorldStore()
        for eid, kind, attrs_json, locked_json in ent_rows:
            ent = Entity(eid, kind)
            for name, entries in json.loads(attrs_json).items():
                ent.attrs[name] = [AttrEntry(v, s, vf, vt, rev)
                                   for v, s, vf, vt, rev in entries]
            ent.locked = set(json.loads(locked_json))
            store.entities[eid] = ent
        meta = json.loads(meta_json or "{}")
        if meta.get("calendar"):
            store.calendar = Calendar.from_spec(meta["calendar"])
        store.eras = meta.get("eras", [])
        store._rev = meta.get("rev", 0)
        store.log = json.loads(log_json or "[]")
        return store
