"""Controlled-roleplay kernel (RP-1, docs/RP_DESIGN.md).

Scene-stage model: free-form utterances inside scene containers; the KP
(or the world) advances time by closing scenes. Every seat keeps its own
context (state card + delivered-knowledge feed); the world context
arbitrates (movement physics is deterministic via the geographic
database) and distributes events with real courier delays. Three-layer
utterances (action/dialogue/inner) — inner never leaves its seat. The
author holds merge rights over everything: AI seats only *suggest*
unless automation is set to "auto".

Everything is offline-testable: the LLM pool is optional and mockable;
astronomy (sky) is optional injection.
"""

from __future__ import annotations

import json
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

LAYERS = ("action", "dialogue", "inner", "narration")


@dataclass
class StateCard:
    persona: str = ""            # long-term characterization
    goals: str = ""              # what they want right now
    mood: str = ""
    relations: dict = field(default_factory=dict)   # name -> text
    knowledge: list = field(default_factory=list)   # summarized known facts
    appearance: str = ""         # visual canon (portrait prompts)

    def brief(self) -> str:
        rel = "; ".join(f"{k}: {v}" for k, v in self.relations.items())
        knows = "\n".join(f"- {k}" for k in self.knowledge[-12:])
        return (f"【人设】{self.persona}\n【当前目标】{self.goals}\n"
                f"【情绪】{self.mood}\n【关系】{rel}\n【已知】\n{knows}")


@dataclass
class Seat:
    name: str
    kind: str = "actor"                  # "kp" | "actor"
    card: StateCard = field(default_factory=StateCard)
    automation: str = "manual"           # manual | assist | auto
    ai_role: str = "actor_minor"         # llmpool role
    place: tuple | None = None           # (lon, lat) when off-stage
    feed: list = field(default_factory=list)      # what this seat knows
    pending: list = field(default_factory=list)   # undelivered news


class Roleplay:
    def __init__(self, session, pool=None, obs=None, project=None,
                 refpack_fn=None):
        self.session = session          # world session (result/store/geo)
        self.pool = pool                # LLMPool | None
        self.obs = obs                  # astro.Observatory | None
        self.project = project          # novelkit.Project | None
        self.refpack_fn = refpack_fn    # callable(session, obs, lon, lat, t)
        self.seats: dict[str, Seat] = {}
        self.scenes: list[dict] = []
        self.threads: list[dict] = []
        self.transcript: list[dict] = []
        self.clock: float = 0.0
        self._entry_seq = 0

    # ------------------------------------------------------------ seats --

    def add_seat(self, name: str, kind: str = "actor", automation="manual",
                 ai_role: str | None = None, place=None, **card) -> Seat:
        seat = Seat(name=name, kind=kind, automation=automation,
                    ai_role=ai_role or ("kp" if kind == "kp" else "actor_minor"),
                    place=tuple(place) if place else None,
                    card=StateCard(**card))
        self.seats[name] = seat
        return seat

    def _geo(self):
        if getattr(self.session, "geo", None) is None:
            import geoquery
            self.session.geo = geoquery.GeoIndex(self.session)
        return self.session.geo

    # ------------------------------------------------------------ scenes --

    def current_scene(self) -> dict | None:
        return self.scenes[-1] if (self.scenes and
                                   self.scenes[-1]["status"] == "open") else None

    def open_scene(self, place, cast: list[str], opening: str,
                   day: float | None = None) -> dict:
        assert self.current_scene() is None, "close the previous scene first"
        if day is not None:
            self.clock = max(self.clock, float(day))
        sky = None
        if self.obs is not None:
            sky = self.obs.sky(place[1], place[0], self.clock + 0.75)
        refpack = None
        if self.refpack_fn is not None:
            refpack = self.refpack_fn(self.session, self.obs,
                                      place[0], place[1], self.clock)
        scene = {"type": "scene", "id": len(self.scenes) + 1,
                 "day": round(self.clock, 2), "place": list(place),
                 "cast": list(cast), "opening": opening,
                 "sky": sky, "refpack": refpack, "status": "open",
                 "entries": []}
        self.scenes.append(scene)
        self.transcript.append(scene)
        self.deliver_due()
        for name in cast:
            seat = self.seats[name]
            seat.place = tuple(place)
            seat.feed.append({"day": scene["day"], "kind": "scene_opening",
                              "scene": scene["id"], "text": opening})
        return scene

    def say(self, seat_name: str, layer: str, text: str,
            arbitrate: bool = True) -> dict:
        scene = self.current_scene()
        assert scene is not None, "no open scene"
        seat = self.seats[seat_name]
        assert layer in LAYERS
        assert seat.kind == "kp" or seat_name in scene["cast"], \
            f"{seat_name} is not on stage"
        note = None
        if arbitrate and layer in ("action", "narration"):
            note = self._light_arbitrate(seat_name, text, scene)
        self._entry_seq += 1
        entry = {"type": "say", "id": self._entry_seq, "scene": scene["id"],
                 "seat": seat_name, "layer": layer, "text": text,
                 "arbiter_note": note, "edited": False}
        scene["entries"].append(entry)
        self.transcript.append(entry)
        if layer != "inner":  # inner never leaves its seat
            for name in scene["cast"]:
                if name != seat_name:
                    self.seats[name].feed.append(
                        {"day": scene["day"], "kind": f"observed_{layer}",
                         "scene": scene["id"], "who": seat_name, "text": text})
        seat.feed.append({"day": scene["day"], "kind": f"self_{layer}",
                          "scene": scene["id"], "text": text})
        return entry

    def _light_arbitrate(self, seat_name, text, scene) -> str | None:
        """Fast in-scene sanity note. Deterministic physics lives in
        move(); this optional pass asks a cheap model for a one-line
        plausibility note (or stays silent without a pool)."""
        if self.pool is None:
            return None
        try:
            reply = self.pool.chat("arbiter", [
                {"role": "system",
                 "content": ("你是场记。对下面这条角色行动，只在它与物理/"
                             "地理/时刻明显冲突时输出一行『疑点：…』，"
                             "否则输出 OK。不要多话。")},
                {"role": "user",
                 "content": f"场景: {scene['opening'][:200]}\n"
                            f"行动({seat_name}): {text}"}],
                seat="arbiter", max_tokens=120)
            reply = reply.strip()
            return None if reply.upper().startswith("OK") else reply
        except Exception:  # noqa: BLE001 — arbitration must never block play
            return None

    # ------------------------------------------------- movement physics --

    def move(self, seat_name: str, dest, claimed_days: float,
             mode: str = "horse", slack: float = 0.85) -> dict:
        """Deterministic travel check BEFORE the move enters canon."""
        seat = self.seats[seat_name]
        assert seat.place is not None, "seat has no known location"
        geo = self._geo()
        route = geo.route(seat.place, tuple(dest), mode)
        if not route["reachable"]:
            ship = geo.route(seat.place, tuple(dest), "ship")
            route = ship if ship["reachable"] else route
        if not route["reachable"]:
            return {"verdict": "reject", "reason": "unreachable"}
        if claimed_days < route["days"] * slack:
            return {"verdict": "reject", "reason": "too_fast",
                    "needed_days": round(route["days"], 1),
                    "claimed_days": claimed_days,
                    "distance_km": route.get("distance_km")}
        seat.place = tuple(dest)
        rec = {"type": "move", "seat": seat_name, "day": round(self.clock, 2),
               "dest": list(dest), "days": claimed_days,
               "route_days": round(route["days"], 1), "mode": mode,
               "verdict": "ok"}
        self.transcript.append(rec)
        return rec

    # --------------------------------------------------------- settling --

    def close_scene(self, elapsed_days: float, events: list[dict] | None = None,
                    deltas: dict | None = None, summary: str = "") -> dict:
        """events: {desc, visibility: 'public'|'cast'|list[seat], place?}.
        Public events reach off-stage seats after the real courier delay."""
        scene = self.current_scene()
        assert scene is not None
        scene["status"] = "closed"
        self.clock = round(self.clock + float(elapsed_days), 3)
        distributed = []
        for ev in events or []:
            vis = ev.get("visibility", "cast")
            targets_now = list(scene["cast"]) if vis in ("cast", "public") \
                else list(vis)
            for name in targets_now:
                self.seats[name].feed.append(
                    {"day": scene["day"], "kind": "event", "text": ev["desc"]})
            if vis == "public":
                src = tuple(ev.get("place", scene["place"]))
                for name, seat in self.seats.items():
                    if name in scene["cast"] or seat.place is None:
                        continue
                    geo = self._geo()
                    news = geo.news_arrival(src, seat.place)
                    if not news["reachable"]:
                        continue
                    arrives = round(scene["day"] + news["days"], 2)
                    seat.pending.append(
                        {"arrives_day": arrives, "kind": "news",
                         "text": f"（消息，发生于第{scene['day']:.0f}天）"
                                 f"{ev['desc']}"})
                    distributed.append({"to": name, "arrives_day": arrives,
                                        "desc": ev["desc"][:60]})
            if self.project is not None:
                cs = self.project.stage(ev["desc"][:40], scene["day"],
                                        ev.get("ops", []))
                ev["changeset"] = cs
        for name, delta in (deltas or {}).items():
            card = self.seats[name].card
            for key, value in delta.items():
                if key == "knowledge+":
                    card.knowledge.append(value)
                elif key == "relations+":
                    card.relations.update(value)
                else:
                    setattr(card, key, value)
        settle = {"type": "settle", "scene": scene["id"],
                  "elapsed_days": elapsed_days, "day_after": self.clock,
                  "events": events or [], "distributed": distributed,
                  "summary": summary}
        self.transcript.append(settle)
        self.deliver_due()
        return settle

    def deliver_due(self):
        for seat in self.seats.values():
            due = [p for p in seat.pending if p["arrives_day"] <= self.clock]
            for p in due:
                seat.pending.remove(p)
                seat.feed.append({"day": p["arrives_day"], "kind": "news",
                                  "text": p["text"]})

    # ---------------------------------------------------------- threads --

    def plant_thread(self, desc: str) -> dict:
        th = {"id": len(self.threads) + 1, "desc": desc,
              "planted_scene": len(self.scenes), "status": "open", "paid_scene": None}
        self.threads.append(th)
        return th

    def pay_thread(self, thread_id: int):
        th = next(t for t in self.threads if t["id"] == thread_id)
        th["status"] = "paid"
        th["paid_scene"] = len(self.scenes)

    def open_threads(self) -> list[dict]:
        return [t for t in self.threads if t["status"] == "open"]

    # ------------------------------------------------------------- AI ----

    def seat_messages(self, seat_name: str) -> list[dict]:
        seat = self.seats[seat_name]
        scene = self.current_scene()
        feed_tail = "\n".join(
            f"[第{e['day']:.1f}天|{e['kind']}] {e.get('who', '')}"
            f"{'：' if e.get('who') else ''}{e['text']}"
            for e in seat.feed[-24:])
        system = (f"你在扮演「{seat_name}」。严格贴合状态卡，用第一人称。\n"
                  f"{seat.card.brief()}\n"
                  "输出恰好三段，格式：\n行动：……\n台词：……\n内心：……\n"
                  "『内心』是只有你自己知道的真实想法，可以与台词相反。")
        if seat.kind == "kp":
            threads = "\n".join(f"- #{t['id']} {t['desc']}"
                                for t in self.open_threads())
            system = ("你是 KP（主持人）。推进场景、扮演群演、决定事件；"
                      "不代替玩家角色做决定。\n"
                      f"【未回收的线头】\n{threads}")
        user = (f"【你所知道的近况】\n{feed_tail}\n\n"
                + (f"【当前场景】{scene['opening']}" if scene else
                   "【当前无开启场景】"))
        return [{"role": "system", "content": system},
                {"role": "user", "content": user}]

    def suggest(self, seat_name: str, max_tokens: int = 1200) -> dict:
        """AI draft for a seat. automation=auto lands it immediately
        (three layers parsed); otherwise it is a draft for the author."""
        assert self.pool is not None, "no LLM pool attached"
        seat = self.seats[seat_name]
        text = self.pool.chat(seat.ai_role, self.seat_messages(seat_name),
                              seat=seat_name, max_tokens=max_tokens)
        draft = {"seat": seat_name, "text": text, "landed": False}
        if seat.automation == "auto" and seat.kind != "kp":
            for layer, cn in (("action", "行动"), ("dialogue", "台词"),
                              ("inner", "内心")):
                for line in text.splitlines():
                    if line.strip().startswith(f"{cn}："):
                        self.say(seat_name, layer,
                                 line.split("：", 1)[1].strip(),
                                 arbitrate=(layer == "action"))
            draft["landed"] = True
        return draft

    # ----------------------------------------------------- author power --

    def edit_entry(self, entry_id: int, new_text: str):
        """The author's pen always wins; edits propagate to feeds."""
        for rec in self.transcript:
            if rec.get("type") == "say" and rec["id"] == entry_id:
                old = rec["text"]
                rec["text"] = new_text
                rec["edited"] = True
                for seat in self.seats.values():
                    for item in seat.feed:
                        if item.get("text") == old:
                            item["text"] = new_text
                return rec
        raise KeyError(entry_id)

    # ------------------------------------------------------ persistence --

    def transcript_jsonl(self) -> str:
        return "\n".join(json.dumps(r, ensure_ascii=False, default=str)
                         for r in self.transcript)

    def save(self, path: Path):
        data = {"clock": self.clock, "threads": self.threads,
                "transcript": self.transcript,
                "seats": {n: {"kind": s.kind, "automation": s.automation,
                              "ai_role": s.ai_role, "place": s.place,
                              "feed": s.feed, "pending": s.pending,
                              "card": vars(s.card)}
                          for n, s in self.seats.items()}}
        Path(path).write_text(json.dumps(data, ensure_ascii=False, default=str),
                              encoding="utf-8")

    def load(self, path: Path):
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        self.clock = data["clock"]
        self.threads = data["threads"]
        self.transcript = data["transcript"]
        self.scenes = [r for r in self.transcript if r.get("type") == "scene"]
        self._entry_seq = max((r["id"] for r in self.transcript
                               if r.get("type") == "say"), default=0)
        self.seats = {}
        for name, s in data["seats"].items():
            seat = Seat(name=name, kind=s["kind"], automation=s["automation"],
                        ai_role=s["ai_role"],
                        place=tuple(s["place"]) if s["place"] else None,
                        card=StateCard(**s["card"]))
            seat.feed = s["feed"]
            seat.pending = s["pending"]
            self.seats[name] = seat
