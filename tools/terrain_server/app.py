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
        session.loop.call_soon_threadsafe(session.queue.put_nowait, event)


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
    persistence.save_world(path, s.params, s.result, s.features, s.entities)
    return {"saved": path.name}


@app.post("/api/sessions/{sid}/load")
async def load_world(sid: str, request: Request):
    s = _session(sid)
    body = await request.json()
    path = WORLDS_DIR / Path(body["name"]).name
    if not path.exists():
        raise HTTPException(404, "no such world")
    params, result, features, entities = persistence.load_world(path)
    s.params, s.result = params, result
    s.features, s.entities = features, entities
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
def get_entities(sid: str, kind: str | None = None):
    s = _session(sid)
    return {"entities": [e for e in s.entities
                         if kind is None or e.get("kind") == kind]}


@app.get("/")
def index():
    return HTMLResponse((UI_DIR / "index.html").read_text(encoding="utf-8"))


app.mount("/static", StaticFiles(directory=str(UI_DIR)), name="static")
