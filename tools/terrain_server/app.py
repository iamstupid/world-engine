"""WorldEngine terrain server (plan M7): sessions, async generation jobs with
SSE progress, raw layer transport, point queries, .weworld save/load, paint
override upload (M10), civilization layer + GeoJSON features (M11).

Run:  uvicorn app:app --port 8151   (from tools/terrain_server, with
      build-linux on PYTHONPATH for the weterrain module)
"""

from __future__ import annotations

import asyncio
import json
import sys
import threading
import uuid
from pathlib import Path

import numpy as np
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, Response, StreamingResponse
from fastapi.staticfiles import StaticFiles

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "build-linux"))

import weterrain  # noqa: E402

import persistence  # noqa: E402
from worldstore import WorldStore  # noqa: E402

WORLDS_DIR = REPO / "worlds"
UI_DIR = REPO / "ui" / "terrain_editor"

DTYPES = {"f32": np.float32, "u8": np.uint8, "i32": np.int32}


class Session:
    def __init__(self):
        self.id = uuid.uuid4().hex[:12]
        self.params: dict[str, float] = {}
        self.result: dict | None = None
        self.features: list = []
        self.entities: list = []
        self.job: threading.Thread | None = None
        self.cancel = None
        self.error: str | None = None
        self.queue: asyncio.Queue = asyncio.Queue()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.paint: dict[str, bytes] = {}  # M10 override layers (raw f32)
        self.store = WorldStore()          # M12 entity store
        self.geo = None                    # M13 query index (lazy)
        self.astro_spec: dict = {}         # astronomy overrides (persisted)
        self.astro = None                  # (Universe, Observatory), lazy

    def running(self) -> bool:
        return self.job is not None and self.job.is_alive()

    def build_params(self):
        p = weterrain.Params()
        for key, value in self.params.items():
            p.set(key, float(value))
        return p

    def layer_array(self, name: str) -> np.ndarray:
        if self.result is None or name not in self.result["layers"]:
            raise KeyError(name)
        entry = self.result["layers"][name]
        arr = np.frombuffer(entry["data"], dtype=DTYPES[entry["dtype"]])
        return arr.reshape(self.result["height"], self.result["width"])


SESSIONS: dict[str, Session] = {}

app = FastAPI(title="WorldEngine Terrain Server")


def _push(session: Session, event: dict):
    if session.loop is not None:
        try:
            session.loop.call_soon_threadsafe(session.queue.put_nowait, event)
        except RuntimeError:
            pass  # loop closed (shutdown/test harness); don't kill the job


def _run_generation(session: Session):
    try:
        params = session.build_params()
        cancel = weterrain.CancelFlag()
        session.cancel = cancel

        def progress(stage, idx, total, phase):
            _push(session, {"type": "progress", "stage": stage, "index": idx,
                            "total": total, "phase": phase})

        paint = {name: (w, h, data)
                 for name, (w, h, data) in session.paint.items()} or None
        result = weterrain.generate(params, progress=progress, cancel=cancel,
                                    paint=paint)
        session.result = result
        session.geo = None
        session.astro = None
        session.error = None
        try:
            import civ  # noqa: E402  (M11; optional at M7/M8 stage)
            civ.generate_civilization(session)
        except ImportError:
            pass
        _push(session, {"type": "done", "hash": result.get("hash", "")})
    except Exception as exc:  # noqa: BLE001
        session.error = str(exc)
        _push(session, {"type": "error", "message": str(exc)})


@app.get("/api/schema")
def get_schema():
    return json.loads(weterrain.schema_json())


@app.post("/api/sessions")
def create_session():
    s = Session()
    SESSIONS[s.id] = s
    return {"id": s.id}


def _session(sid: str) -> Session:
    if sid not in SESSIONS:
        raise HTTPException(404, "no such session")
    return SESSIONS[sid]


@app.get("/api/sessions/{sid}")
def session_info(sid: str):
    s = _session(sid)
    layers = sorted(s.result["layers"].keys()) if s.result else []
    return {"id": s.id, "params": s.params, "running": s.running(),
            "error": s.error, "layers": layers,
            "width": s.result["width"] if s.result else 0,
            "height": s.result["height"] if s.result else 0,
            "features": len(s.features), "entities": len(s.entities)}


@app.put("/api/sessions/{sid}/params")
async def set_params(sid: str, request: Request):
    s = _session(sid)
    body = await request.json()
    probe = weterrain.Params()
    for key, value in body.items():
        probe.set(key, float(value))  # raises KeyError -> 500 on bad key
    s.params.update({k: float(v) for k, v in body.items()})
    return {"ok": True, "params": s.params}


@app.post("/api/sessions/{sid}/generate")
async def generate(sid: str):
    s = _session(sid)
    if s.running():
        raise HTTPException(409, "generation already running")
    s.loop = asyncio.get_running_loop()
    s.queue = asyncio.Queue()
    s.job = threading.Thread(target=_run_generation, args=(s,), daemon=True)
    s.job.start()
    return {"started": True}


@app.post("/api/sessions/{sid}/cancel")
def cancel(sid: str):
    s = _session(sid)
    if s.cancel is not None:
        s.cancel.set()
    return {"ok": True}


@app.get("/api/sessions/{sid}/progress")
async def progress_stream(sid: str):
    s = _session(sid)

    async def stream():
        while True:
            try:
                event = await asyncio.wait_for(s.queue.get(), timeout=120)
            except asyncio.TimeoutError:
                yield "data: {\"type\":\"timeout\"}\n\n"
                return
            yield f"data: {json.dumps(event)}\n\n"
            if event["type"] in ("done", "error"):
                return

    return StreamingResponse(stream(), media_type="text/event-stream")


@app.get("/api/sessions/{sid}/layer/{name}")
def get_layer(sid: str, name: str):
    s = _session(sid)
    if s.result is None or name not in s.result["layers"]:
        raise HTTPException(404, "layer not available")
    entry = s.result["layers"][name]
    return Response(content=entry["data"], media_type="application/octet-stream",
                    headers={"X-Dtype": entry["dtype"],
                             "X-Width": str(s.result["width"]),
                             "X-Height": str(s.result["height"])})


_GLOBE_MESH_CACHE: dict[int, tuple] = {}


@app.get("/api/sessions/{sid}/globe_mesh")
def globe_mesh(sid: str):
    """Stitched icosahedral mesh for the client globe (plan addendum b:
    the geodesic cell array IS the transport format — zero waste, no
    polar oversampling). Body = unit cell centers (f32 x 3n) followed by
    triangle indices (u32 x 3t); attributes ride separately via
    /cell_layer/{name} in the same cell order."""
    s = _session(sid)
    if s.result is None or "cell_elevation_m" not in s.result.get(
            "cell_layers", {}):
        raise HTTPException(409, "no cell layers yet")
    freq = int(s.result["cell_layers"]["cell_elevation_m"]["frequency"])
    if freq not in _GLOBE_MESH_CACHE:
        g = weterrain.geodesic_graph(freq)
        n = g["cell_count"]
        nb = np.frombuffer(g["neighbors_i32"], np.int32).reshape(n, 6)
        deg = np.frombuffer(g["degree_i8"], np.int8).astype(np.int64)
        pos = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
        idx = np.arange(n, dtype=np.int64)
        parts = []
        for k in range(6):
            valid = deg > k
            a = nb[idx, k]
            b = nb[idx, (k + 1) % deg]
            keep = valid & (a >= 0) & (idx < a) & (idx < b)
            parts.append(np.stack([idx[keep], a[keep], b[keep]], axis=1))
        tri = np.concatenate(parts).astype(np.uint32)
        _GLOBE_MESH_CACHE[freq] = (pos.astype(np.float32).tobytes(),
                                   tri.tobytes(), n, tri.shape[0])
    p, t, n, nt = _GLOBE_MESH_CACHE[freq]
    return Response(content=p + t, media_type="application/octet-stream",
                    headers={"X-Cells": str(n), "X-Tris": str(nt),
                             "X-Frequency": str(freq)})


@app.get("/api/sessions/{sid}/cell_layer/{name}")
def get_cell_layer(sid: str, name: str):
    s = _session(sid)
    if s.result is None or name not in s.result.get("cell_layers", {}):
        raise HTTPException(404, "cell layer not available")
    entry = s.result["cell_layers"][name]
    return Response(content=entry["data"], media_type="application/octet-stream",
                    headers={"X-Dtype": entry["dtype"],
                             "X-Frequency": str(entry["frequency"])})


@app.get("/api/sessions/{sid}/cell_atlas/{name}")
def get_cell_atlas(sid: str, name: str):
    """Rhombus-atlas view (2F x 5F f32 image + the two pole values)."""
    s = _session(sid)
    if s.result is None or name not in s.result.get("cell_layers", {}):
        raise HTTPException(404, "cell layer not available")
    import atlas
    freq, img, poles = atlas.pack_cell_atlas(s.result, name)
    return Response(content=img.tobytes(), media_type="application/octet-stream",
                    headers={"X-Dtype": "f32", "X-Frequency": str(freq),
                             "X-Width": str(5 * freq), "X-Height": str(2 * freq),
                             "X-Poles": f"{float(poles[0])},{float(poles[1])}"})


@app.get("/api/sessions/{sid}/refine")
def api_refine(sid: str, lon0: float, lat0: float, lon1: float, lat1: float,
               scale: int = 4, iterations: int = 3):
    """M11.5 region refinement: higher-resolution tile with boundary
    conditions from the global solution (f32 row-major, north-up)."""
    s = _session(sid)
    if s.result is None:
        raise HTTPException(409, "no data yet")
    import refine as refine_mod
    out = refine_mod.refine_tile(s, lon0, lat0, lon1, lat1, scale=scale,
                                 iterations=min(int(iterations), 8))
    return Response(content=out["tile"].tobytes(),
                    media_type="application/octet-stream",
                    headers={"X-Dtype": "f32",
                             "X-Width": str(out["width"]),
                             "X-Height": str(out["height"]),
                             "X-Bbox": f"{out['lon0']},{out['lat0']},"
                                       f"{out['lon1']},{out['lat1']}",
                             "X-Base-Layer": out["base_layer"]})


@app.get("/api/sessions/{sid}/query")
def point_query(sid: str, lat: float, lon: float):
    s = _session(sid)
    if s.result is None:
        raise HTTPException(409, "no data yet")
    w, h = s.result["width"], s.result["height"]
    x = int(((lon + 180.0) / 360.0) * w) % w
    y = min(h - 1, max(0, int(((90.0 - lat) / 180.0) * h)))
    out = {"lat": lat, "lon": lon}
    for name in s.result["layers"]:
        arr = s.layer_array(name)
        out[name] = float(arr[y, x])
    return out


@app.get("/api/worlds")
def list_worlds():
    WORLDS_DIR.mkdir(exist_ok=True)
    return {"worlds": sorted(p.name for p in WORLDS_DIR.glob("*.weworld"))}


@app.post("/api/sessions/{sid}/save")
async def save_world(sid: str, request: Request):
    s = _session(sid)
    if s.result is None:
        raise HTTPException(409, "nothing to save")
    body = await request.json()
    name = "".join(c for c in body.get("name", "world") if c.isalnum() or c in "-_")
    path = WORLDS_DIR / f"{name}.weworld"
    persistence.save_world(path, s.params, s.result, s.features, s.entities,
                           store=s.store, astro_spec=s.astro_spec)
    return {"saved": path.name}


@app.post("/api/sessions/{sid}/load")
async def load_world(sid: str, request: Request):
    s = _session(sid)
    body = await request.json()
    path = WORLDS_DIR / Path(body["name"]).name
    if not path.exists():
        raise HTTPException(404, "no such world")
    params, result, features, entities, store, astro_spec = \
        persistence.load_world(path)
    s.params, s.result = params, result
    s.features, s.entities = features, entities
    if store is not None:
        s.store = store
    s.astro_spec = astro_spec or {}
    s.geo = None
    s.astro = None
    return {"loaded": path.name, "layers": sorted(result["layers"].keys())}


# ---- M10: paint override layers ----

@app.post("/api/sessions/{sid}/paint/{layer}")
async def upload_paint(sid: str, layer: str, request: Request):
    s = _session(sid)
    if layer not in ("uplift_paint", "continent_seed_paint"):
        raise HTTPException(400, "unknown paint layer")
    w = int(request.headers.get("X-Width", "512"))
    h = int(request.headers.get("X-Height", "256"))
    body = await request.body()
    if len(body) != w * h * 4:
        raise HTTPException(400, "size mismatch")
    s.paint[layer] = (w, h, body)
    return {"ok": True, "bytes": len(body)}


@app.delete("/api/sessions/{sid}/paint")
def clear_paint(sid: str):
    s = _session(sid)
    s.paint.clear()
    return {"ok": True}


# ---- M11: features / entities ----

@app.get("/api/sessions/{sid}/features")
def get_features(sid: str, kind: str | None = None):
    s = _session(sid)
    feats = [f for f in s.features if kind is None or f.get("kind") == kind]
    return {"type": "FeatureCollection",
            "features": [f.get("geojson", f) for f in feats]}


@app.get("/api/sessions/{sid}/entities")
def get_entities(sid: str, kind: str | None = None, name: str | None = None,
                 as_of: float | None = None):
    s = _session(sid)
    return {"entities": s.store.find(kind=kind, name_like=name, as_of=as_of)}


@app.put("/api/sessions/{sid}/entities/{eid}")
async def edit_entity(sid: str, eid: str, request: Request):
    s = _session(sid)
    body = await request.json()
    if eid not in s.store.entities:
        raise HTTPException(404, "no such entity")
    for name, value in body.get("attrs", {}).items():
        s.store.set_attr(eid, name, value,
                         valid_from=body.get("valid_from"),
                         valid_to=body.get("valid_to"))
    for name in body.get("lock", []):
        s.store.lock(eid, name)
    for name in body.get("unlock", []):
        s.store.unlock(eid, name)
    return {"ok": True, "entity": s.store.snapshot(eid)}


# ---- M13 queries ----

def _geo(s: Session):
    if s.result is None:
        raise HTTPException(409, "no data yet")
    if s.geo is None:
        import geoquery
        s.geo = geoquery.GeoIndex(s)
    return s.geo


@app.get("/api/sessions/{sid}/route")
def api_route(sid: str, alat: float, alon: float, blat: float, blon: float,
              mode: str = "horse"):
    return _geo(_session(sid)).route((alon, alat), (blon, blat), mode)


@app.get("/api/sessions/{sid}/describe")
def api_describe(sid: str, lat: float, lon: float, as_of: float | None = None):
    return _geo(_session(sid)).describe(lon, lat, as_of)


@app.get("/api/sessions/{sid}/reachable")
def api_reachable(sid: str, lat: float, lon: float, days: float,
                  mode: str = "horse"):
    return _geo(_session(sid)).reachable((lon, lat), days, mode)


@app.get("/api/sessions/{sid}/news")
def api_news(sid: str, elat: float, elon: float, llat: float, llon: float):
    return _geo(_session(sid)).news_arrival((elon, elat), (llon, llat))


@app.get("/api/sessions/{sid}/viewshed")
def api_viewshed(sid: str, lat: float, lon: float):
    return _geo(_session(sid)).viewshed(lon, lat)


# ---- astronomy (full model; absorbs skymodel v1) ----

def _astro(s: Session):
    if s.astro is None:
        import astro
        seed = int(s.params.get("world.seed", 1337))
        year = s.store.calendar.year_days if s.store.calendar else 360.0
        universe, obs = astro.build_universe(seed, float(year), s.astro_spec)
        s.astro = (universe, obs)
        s.store.apply_generation(universe.store_records())
    return s.astro


def _store_named(s: Session, universe, kind: str, gen_key, default):
    from worldstore import stable_id
    return s.store.get(stable_id(kind, gen_key), "name", default=default)


@app.get("/api/sessions/{sid}/sky")
def api_sky(sid: str, lat: float, lon: float, t: float):
    _, obs = _astro(_session(sid))
    return obs.sky(lat, lon, t)


@app.get("/api/sessions/{sid}/eclipses")
def api_eclipses(sid: str, t0: float = 0.0, days: float = 360.0):
    _, obs = _astro(_session(sid))
    return {"eclipses": obs.find_eclipses(t0, min(days, 3600.0))}


@app.put("/api/sessions/{sid}/astro/spec")
async def api_astro_spec(sid: str, request: Request):
    s = _session(sid)
    s.astro_spec.update(await request.json())
    s.astro = None
    return {"ok": True, "spec": s.astro_spec}


@app.get("/api/sessions/{sid}/astro/galaxy")
def api_astro_galaxy(sid: str):
    s = _session(sid)
    universe, _ = _astro(s)
    out = universe.galaxy_map()
    for sys in out["systems"]:
        if sys["name"] is not None:
            sys["name"] = _store_named(s, universe, "star_system",
                                       sys["id"], sys["name"])
    return out


@app.get("/api/sessions/{sid}/astro/system/{idx}")
def api_astro_system(sid: str, idx: int):
    s = _session(sid)
    universe, _ = _astro(s)
    out = universe.system_info(idx)
    if out["name"] is not None:
        out["name"] = _store_named(s, universe, "star_system", idx, out["name"])
    for j, planet in enumerate(out.get("planets", [])):
        planet["name"] = _store_named(s, universe, "planet", f"0:{j}",
                                      planet["name"])
    return out


@app.get("/api/sessions/{sid}/astro/sky_from")
def api_astro_sky_from(sid: str, system: int = 0, epoch_yr: float = 0.0):
    universe, _ = _astro(_session(sid))
    if not (0 <= system < universe.n_systems):
        raise HTTPException(404, "no such system")
    return universe.sky_from(system, epoch_yr=epoch_yr)


@app.get("/api/sessions/{sid}/astro/events")
def api_astro_events(sid: str, t0: float = 0.0, days: float = 360.0):
    _, obs = _astro(_session(sid))
    days = min(days, 3600.0)
    return {"eclipses": obs.find_eclipses(t0, days),
            "conjunctions": obs.conjunctions(t0, days)}


# ---- RP-3: controlled roleplay endpoints ----

def _rp(s: Session):
    if getattr(s, "rp", None) is None:
        raise HTTPException(409, "roleplay not initialized")
    return s.rp


@app.post("/api/sessions/{sid}/rp/init")
async def rp_init(sid: str, request: Request):
    s = _session(sid)
    body = await request.json()
    import novelkit
    import refpack as refpack_mod
    import rpkit
    from llmpool import LLMPool
    _, obs = _astro(s)
    pool = LLMPool() if body.get("use_ai", True) else None
    s.rp_project = novelkit.Project(body.get("name", "rp"), s)
    s.rp = rpkit.Roleplay(
        s, pool=pool, obs=obs, project=s.rp_project,
        refpack_fn=lambda ss, oo, lon, lat, t:
            refpack_mod.scene_refpack(ss, oo, lon, lat, t))
    return {"ok": True, "ai": pool is not None}


@app.post("/api/sessions/{sid}/rp/seat")
async def rp_seat(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    rp.add_seat(body["name"], kind=body.get("kind", "actor"),
                automation=body.get("automation", "manual"),
                ai_role=body.get("ai_role"),
                place=body.get("place"),
                persona=body.get("persona", ""), goals=body.get("goals", ""),
                mood=body.get("mood", ""))
    return {"ok": True}


@app.get("/api/sessions/{sid}/rp/state")
def rp_state(sid: str):
    rp = _rp(_session(sid))
    scene = rp.current_scene()
    return {"clock": rp.clock,
            "seats": {n: {"kind": st.kind, "automation": st.automation,
                          "place": st.place, "card": vars(st.card),
                          "pending": len(st.pending)}
                      for n, st in rp.seats.items()},
            "threads": rp.threads,
            "scene": None if scene is None else {
                "id": scene["id"], "day": scene["day"],
                "place": scene["place"], "cast": scene["cast"],
                "opening": scene["opening"],
                "lighting": ((scene.get("refpack") or {})
                             .get("lighting", {}).get("报告", "")),
                "skyline": (scene.get("refpack") or {}).get(
                    "skyline_report", [])},
            "scenes_total": len(rp.scenes),
            "ai": rp.pool is not None}


@app.get("/api/sessions/{sid}/rp/entries")
def rp_entries(sid: str, viewer: str):
    """Stage flow filtered SERVER-SIDE: inner layers only for their owner."""
    rp = _rp(_session(sid))
    scene = rp.current_scene() or (rp.scenes[-1] if rp.scenes else None)
    if scene is None:
        return {"entries": []}
    out = [e for e in scene["entries"]
           if e["layer"] != "inner" or e["seat"] == viewer]
    return {"scene": scene["id"], "status": scene["status"], "entries": out}


@app.get("/api/sessions/{sid}/rp/feed/{seat}")
def rp_feed(sid: str, seat: str):
    rp = _rp(_session(sid))
    return {"feed": rp.seats[seat].feed[-40:]}


@app.post("/api/sessions/{sid}/rp/scene/open")
async def rp_scene_open(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    scene = rp.open_scene(tuple(body["place"]), body["cast"], body["opening"],
                          day=body.get("day"))
    return {"id": scene["id"], "day": scene["day"],
            "lighting": ((scene.get("refpack") or {})
                         .get("lighting", {}).get("报告", ""))}


@app.post("/api/sessions/{sid}/rp/scene/close")
async def rp_scene_close(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    settle = rp.close_scene(float(body.get("elapsed_days", 0.5)),
                            events=body.get("events", []),
                            deltas=body.get("deltas", {}),
                            summary=body.get("summary", ""))
    return settle


@app.post("/api/sessions/{sid}/rp/say")
async def rp_say(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    return rp.say(body["seat"], body["layer"], body["text"])


@app.post("/api/sessions/{sid}/rp/suggest")
async def rp_suggest(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    return rp.suggest(body["seat"])


@app.post("/api/sessions/{sid}/rp/move")
async def rp_move(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    return rp.move(body["seat"], tuple(body["dest"]),
                   float(body["days"]), body.get("mode", "horse"))


@app.post("/api/sessions/{sid}/rp/thread")
async def rp_thread(sid: str, request: Request):
    body = await request.json()
    rp = _rp(_session(sid))
    if body.get("pay"):
        rp.pay_thread(int(body["pay"]))
        return {"ok": True}
    return rp.plant_thread(body["desc"])


@app.get("/api/sessions/{sid}/rp/refpack.png")
def rp_refpack_png(sid: str, kind: str = "minimap",
                   lon: float | None = None, lat: float | None = None):
    s = _session(sid)
    rp = _rp(s)
    import refpack as refpack_mod
    scene = rp.current_scene() or (rp.scenes[-1] if rp.scenes else None)
    if lon is None or lat is None:
        if scene is None:
            raise HTTPException(409, "no scene")
        lon, lat = scene["place"]
    if kind == "minimap":
        png = refpack_mod.minimap_png(s, lon, lat)
    else:
        sky = rp.obs.sky(lat, lon, rp.clock + 0.75)
        png = refpack_mod.skydome_png(sky)
    return Response(content=png, media_type="image/png")


@app.get("/api/sessions/{sid}/rp/transcript")
def rp_transcript(sid: str):
    rp = _rp(_session(sid))
    return Response(content=rp.transcript_jsonl(),
                    media_type="application/jsonl; charset=utf-8")


@app.post("/api/sessions/{sid}/rp/novelize")
async def rp_novelize(sid: str, request: Request):
    body = await request.json()
    s = _session(sid)
    rp = _rp(s)
    import novelize as novelize_mod
    out_dir = WORLDS_DIR / "novelized" / s.id
    paths = novelize_mod.novelize(rp.transcript, body["pov"], rp.pool,
                                  out_dir)
    return {"chapters": [p.read_text(encoding="utf-8") for p in paths]}


RP_UI_DIR = REPO / "ui" / "rp"


@app.get("/rp")
def rp_index():
    return HTMLResponse((RP_UI_DIR / "index.html").read_text(encoding="utf-8"))


@app.get("/")
def index():
    return HTMLResponse((UI_DIR / "index.html").read_text(encoding="utf-8"))


app.mount("/static", StaticFiles(directory=str(UI_DIR)), name="static")
app.mount("/rpstatic", StaticFiles(directory=str(RP_UI_DIR)), name="rpstatic")
