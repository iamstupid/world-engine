"""`.weworld` container: a single SQLite database holding params, raster
layers, geodesic cell layers, and (M11+) vector features / lore entities.
Layers are zstd-compressed."""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path

import zstandard as zstd

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT);
CREATE TABLE IF NOT EXISTS layers (
  name TEXT PRIMARY KEY, dtype TEXT, width INT, height INT, data BLOB);
CREATE TABLE IF NOT EXISTS cell_layers (
  name TEXT PRIMARY KEY, frequency INT, dtype TEXT, data BLOB);
CREATE TABLE IF NOT EXISTS features (
  id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT, name TEXT, geojson TEXT);
CREATE TABLE IF NOT EXISTS entities (
  id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT, name TEXT, data TEXT,
  locked INT DEFAULT 0);
CREATE TABLE IF NOT EXISTS store_entities (
  id TEXT PRIMARY KEY, kind TEXT, attrs TEXT, locked TEXT);
CREATE TABLE IF NOT EXISTS store_meta (k TEXT PRIMARY KEY, v TEXT);
"""


def save_world(path: Path, params: dict, result: dict, features: list | None = None,
               entities: list | None = None, store=None,
               astro_spec: dict | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    con = sqlite3.connect(path)
    try:
        con.executescript(SCHEMA)
        cctx = zstd.ZstdCompressor(level=6)
        con.execute("INSERT INTO meta VALUES ('params', ?)", (json.dumps(params),))
        if astro_spec:
            con.execute("INSERT INTO meta VALUES ('astro_spec', ?)",
                        (json.dumps(astro_spec),))
        con.execute("INSERT INTO meta VALUES ('width', ?)", (str(result["width"]),))
        con.execute("INSERT INTO meta VALUES ('height', ?)", (str(result["height"]),))
        con.execute("INSERT INTO meta VALUES ('hash', ?)", (result.get("hash", ""),))
        for name, entry in result["layers"].items():
            con.execute(
                "INSERT INTO layers VALUES (?,?,?,?,?)",
                (name, entry["dtype"], result["width"], result["height"],
                 cctx.compress(entry["data"])))
        for name, entry in result.get("cell_layers", {}).items():
            con.execute(
                "INSERT INTO cell_layers VALUES (?,?,?,?)",
                (name, entry["frequency"], entry["dtype"], cctx.compress(entry["data"])))
        for feat in features or []:
            con.execute("INSERT INTO features (kind, name, geojson) VALUES (?,?,?)",
                        (feat["kind"], feat.get("name", ""), json.dumps(feat)))
        for ent in entities or []:
            con.execute(
                "INSERT INTO entities (kind, name, data, locked) VALUES (?,?,?,?)",
                (ent["kind"], ent.get("name", ""), json.dumps(ent),
                 1 if ent.get("locked") else 0))
        if store is not None:
            rows, meta, log = store.to_rows()
            con.executemany("INSERT INTO store_entities VALUES (?,?,?,?)", rows)
            con.execute("INSERT INTO store_meta VALUES ('meta', ?)", (meta,))
            con.execute("INSERT INTO store_meta VALUES ('log', ?)", (log,))
        con.commit()
    finally:
        con.close()


def load_world(path: Path):
    """Returns (params, result, features, entities, store, astro_spec)."""
    con = sqlite3.connect(path)
    try:
        dctx = zstd.ZstdDecompressor()
        meta = dict(con.execute("SELECT k, v FROM meta"))
        params = json.loads(meta.get("params", "{}"))
        astro_spec = json.loads(meta.get("astro_spec", "{}"))
        result = {
            "width": int(meta["width"]),
            "height": int(meta["height"]),
            "hash": meta.get("hash", ""),
            "layers": {},
            "cell_layers": {},
        }
        for name, dtype, w, h, blob in con.execute("SELECT * FROM layers"):
            result["layers"][name] = {"dtype": dtype, "data": dctx.decompress(blob)}
        for name, freq, dtype, blob in con.execute("SELECT * FROM cell_layers"):
            result["cell_layers"][name] = {
                "frequency": freq, "dtype": dtype, "data": dctx.decompress(blob)}
        features = [json.loads(row[0]) for row in
                    con.execute("SELECT geojson FROM features")]
        entities = [json.loads(row[0]) for row in
                    con.execute("SELECT data FROM entities")]
        store = None
        try:
            rows = list(con.execute("SELECT * FROM store_entities"))
            meta = dict(con.execute("SELECT k, v FROM store_meta"))
            if rows or meta:
                from worldstore import WorldStore
                store = WorldStore.from_rows(rows, meta.get("meta", "{}"),
                                             meta.get("log", "[]"))
        except Exception:  # noqa: BLE001  (older files without store tables)
            store = None
        return params, result, features, entities, store, astro_spec
    finally:
        con.close()
