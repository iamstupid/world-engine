"""Civilization layer v2 (docs/MAP_SUITE_DESIGN.md §2, batch B): species
packs with a restricted, hard-validated filter DSL + IGM spherical-polygon
region masks, era-unrolled settlement seeding with road co-evolution and
accessibility write-back, species-aware culture growth, culture-cost polity
territories, and gravity-ranked trunk routes — all executed as a phase DAG
(phasedag.py) so unchanged phases replay from cache.

Output-compatible with civ v1: same cell layers (cell_polity_capital,
cell_culture_hearth, cell_road) and the same entity kinds/attrs geoquery
resolves, with the same gen_key scheme (settlement=cell, culture=hearth
cell, polity=capital cell), so stable ids survive the v1->v2 switch.
"""

from __future__ import annotations

import functools
import hashlib
import math

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra

import weterrain
import phasedag
from civ import feature, make_namegen, to_lonlat
from worldstore import stable_id

R_KM = 6371.0
WEX = 0.45          # road reuse discount on edge cost (design §2.1②)
ACC_SCALE = 600.0   # cost-units e-folding for the accessibility field
ERA_SHARE = [0.40, 0.25, 0.20, 0.15, 0.10, 0.08, 0.06, 0.05]

FILTER_FIELDS = ("elevation_m", "temperature_c", "precip_mm", "runoff_mm",
                 "vegetation", "flow_accum_m2", "biome", "slope", "is_ocean",
                 "is_land", "is_coast", "is_river", "river_intensity",
                 "abs_lat_deg")
HARD_OPS = ("eq", "ne", "gt", "ge", "lt", "le", "between", "in")
SOFT_FNS = ("linear", "min1", "gauss", "inv")
MULT_FNS = ("penalty", "bonus")

DEFAULT_SPEC = {
    "eras": {"count": 4, "era_years": 240, "names": None},
    "settlement_target": None,
    "species": [{
        "id": "human", "name": "人族",
        "description": "泛化的定居种群：温带可耕地、水源与河海之利。",
        "filter": {
            "hard": [{"field": "is_land", "op": "eq", "value": 1},
                     {"field": "biome", "op": "ne", "value": 1}],
            "soft": [{"field": "vegetation", "fn": "linear", "weight": 0.45},
                     {"field": "runoff_mm", "fn": "min1", "scale": 400.0,
                      "weight": 0.30},
                     {"field": "temperature_c", "fn": "gauss", "center": 14.0,
                      "width": 16.0, "weight": 0.25}],
            "mult": [{"field": "slope", "fn": "penalty", "scale": 1.0,
                      "cap": 0.8},
                     {"field": "is_coast", "fn": "bonus", "weight": 0.5},
                     {"field": "river_intensity", "fn": "bonus",
                      "weight": 0.8}],
        },
        "masks": [], "share": 1.0, "min_separation_deg": 3.2,
    }],
    "cultures": {"per_species_max": 4},
    "polities": {"max": 14, "foreign_culture_cost": 3.0},
    "routes": {"trunk_max": 14},
}


# ------------------------------------------------------------- geometry ----

def lonlat_dir(lon: float, lat: float) -> np.ndarray:
    la, lo = math.radians(lat), math.radians(lon)
    return np.array([math.cos(la) * math.cos(lo), math.sin(la),
                     math.cos(la) * math.sin(lo)])


def _winding(points: np.ndarray, verts: np.ndarray) -> np.ndarray:
    w = np.zeros(points.shape[0])
    for i in range(len(verts)):
        a, b = verts[i], verts[(i + 1) % len(verts)]
        ta = a[None, :] - points * (points @ a)[:, None]
        tb = b[None, :] - points * (points @ b)[:, None]
        sin_v = np.einsum("ij,ij->i", np.cross(ta, tb), points)
        cos_v = np.einsum("ij,ij->i", ta, tb)
        w += np.arctan2(sin_v, cos_v)
    return w


def polygon_mask(centers: np.ndarray, poly_lonlat: list) -> np.ndarray:
    """Spherical winding number: True where the cell center lies inside the
    polygon (vertices as [lon, lat], any orientation). Resolution-free —
    the C-node stores the polygon, this rasterizes it at any frequency.
    A closed curve winds around BOTH ends of its axis with opposite signs
    (a latitude ring wraps both poles), so raw |winding| would include the
    antipodal region: the interior is defined as the side sharing the
    winding sign at the vertex centroid — 'the side the polygon is drawn
    around', independent of vertex orientation."""
    verts = np.array([lonlat_dir(lo, la) for lo, la in poly_lonlat])
    centroid = verts.mean(axis=0)
    norm = np.linalg.norm(centroid)
    winding = _winding(centers, verts)
    if norm > 1e-9:
        w0 = float(_winding((centroid / norm)[None, :], verts)[0])
        if abs(w0) > math.pi:
            return np.sign(w0) * winding > math.pi
    # degenerate centroid (hemisphere-spanning ring): keep the smaller side
    pos, neg = winding > math.pi, winding < -math.pi
    return pos if pos.sum() <= neg.sum() else neg


# ------------------------------------------------------ spec validation ----

def validate_spec(spec: dict) -> list[str]:
    errors = []
    eras = spec.get("eras", {})
    if not (1 <= int(eras.get("count", 4)) <= 8):
        errors.append("eras.count 须在 1..8")
    species = spec.get("species") or []
    if not species:
        errors.append("至少需要一个种群")
    seen = set()
    for sp in species:
        sid = sp.get("id", "")
        tag = f"species[{sid}]"
        if not sid or sid in seen:
            errors.append(f"{tag}: id 缺失或重复")
        seen.add(sid)
        filt = sp.get("filter", {})
        for cond in filt.get("hard", []):
            if cond.get("field") not in FILTER_FIELDS:
                errors.append(f"{tag}: 未知字段 {cond.get('field')}"
                              f"（词表: {', '.join(FILTER_FIELDS)}）")
            if cond.get("op") not in HARD_OPS:
                errors.append(f"{tag}: 未知比较 {cond.get('op')}")
        for term in filt.get("soft", []):
            if term.get("field") not in FILTER_FIELDS:
                errors.append(f"{tag}: 未知字段 {term.get('field')}")
            if term.get("fn", "linear") not in SOFT_FNS:
                errors.append(f"{tag}: 未知 soft fn {term.get('fn')}")
            if not isinstance(term.get("weight", 0), (int, float)):
                errors.append(f"{tag}: soft weight 须为数值")
        for term in filt.get("mult", []):
            if term.get("field") not in FILTER_FIELDS:
                errors.append(f"{tag}: 未知字段 {term.get('field')}")
            if term.get("fn") not in MULT_FNS:
                errors.append(f"{tag}: 未知 mult fn {term.get('fn')}")
        for mask in sp.get("masks", []):
            poly = mask.get("polygon", [])
            if len(poly) < 3:
                errors.append(f"{tag}: 掩膜多边形至少 3 个顶点")
            if mask.get("mode", "include") not in ("include", "exclude"):
                errors.append(f"{tag}: 掩膜 mode 须为 include/exclude")
    return errors


# ---------------------------------------------------------------- context ----

class Ctx:
    """Field access + travel graph for the phase functions. field_hash
    feeds phasedag: derived fields hash their own bytes, so any terrain
    change dirties every phase that declares __all__."""

    def __init__(self, session):
        result = session.result
        self.freq = result["cell_layers"]["cell_elevation_m"]["frequency"]
        self.world_seed = int(session.params.get("world.seed", 1337))
        self.seed = f"{self.world_seed}:{self.freq}"
        self.store = getattr(session, "store", None)

        def cl(name):
            return np.frombuffer(result["cell_layers"][name]["data"],
                                 np.float32)

        g = weterrain.geodesic_graph(self.freq)
        n = self.n = g["cell_count"]
        self.nb = np.frombuffer(g["neighbors_i32"], np.int32).reshape(n, 6)
        self.centers = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
        self.lonlat = to_lonlat(self.centers)

        z = cl("cell_elevation_m")
        ocean = cl("cell_ocean") > 0.5
        accum = cl("cell_flow_accum_m2")
        biome = cl("cell_biome").astype(np.int32)
        valid = self.nb >= 0
        nbc = np.clip(self.nb, 0, n - 1)
        slope = np.abs(np.where(valid, z[nbc] - z[:, None], 0)).max(axis=1) / 40000.0
        coast = (~ocean) & np.where(valid, ocean[nbc], False).any(axis=1)
        river_thr = float(session.params.get(
            "hydrology.river_area_threshold_m2", 8e9))
        is_river = (accum > river_thr) & ~ocean
        self.fields = {
            "elevation_m": z, "temperature_c": cl("cell_temperature_c"),
            "precip_mm": cl("cell_precip_mm"), "runoff_mm": cl("cell_runoff_mm"),
            "vegetation": cl("cell_vegetation"), "flow_accum_m2": accum,
            "biome": biome.astype(np.float32), "slope": slope.astype(np.float32),
            "is_ocean": ocean.astype(np.float32),
            "is_land": (~ocean).astype(np.float32),
            "is_coast": coast.astype(np.float32),
            "is_river": is_river.astype(np.float32),
            "river_intensity": (np.minimum(1.0, accum / (river_thr * 2))
                                * (~ocean)).astype(np.float32),
            "abs_lat_deg": np.abs(self.lonlat[:, 1]).astype(np.float32),
        }
        self.ocean, self.land, self.biome = ocean, ~ocean, biome

        rows = np.repeat(np.arange(n), 6)[valid.ravel()]
        cols = self.nb.ravel()[valid.ravel()]
        self.rows, self.cols = rows, cols
        self.edge_km = np.linalg.norm(
            self.centers[rows] - self.centers[cols], axis=1) * R_KM
        friction = np.where(ocean, np.inf,
                            1.0 + 4.0 * np.minimum(1.0, slope * 3))
        friction = friction + np.where((biome == 8) | (biome == 2), 2.5, 0.0)
        friction = friction + np.where((biome == 1) | (biome == 9), 4.0, 0.0)
        self.friction = friction
        self.base_w = self.edge_km * 0.5 * (friction[rows] + friction[cols])
        self.base_w = self.base_w + np.where(
            is_river[cols] & ~is_river[rows], 60.0, 0.0)  # bridge/ford cost
        self._hashes: dict[str, str] = {}

    def field_hash(self, name: str) -> str:
        if name not in self._hashes:
            if name == "__all__":
                h = hashlib.blake2b(digest_size=16)
                for key in FILTER_FIELDS:
                    h.update(np.ascontiguousarray(self.fields[key]).tobytes())
                self._hashes[name] = h.hexdigest()
            else:
                self._hashes[name] = hashlib.blake2b(
                    np.ascontiguousarray(self.fields[name]).tobytes(),
                    digest_size=16).hexdigest()
        return self._hashes[name]

    def graph(self, edge_w: np.ndarray) -> csr_matrix:
        finite = np.isfinite(edge_w)
        return csr_matrix((edge_w[finite],
                           (self.rows[finite], self.cols[finite])),
                          shape=(self.n, self.n))


# ------------------------------------------------------------ suitability ----

def suitability(ctx: Ctx, sp: dict) -> np.ndarray:
    filt = sp.get("filter", {})
    hard = np.ones(ctx.n, bool)
    for cond in filt.get("hard", []):
        v, op, ref = ctx.fields[cond["field"]], cond["op"], cond.get("value")
        if op == "eq":
            hard &= v == ref
        elif op == "ne":
            hard &= v != ref
        elif op == "gt":
            hard &= v > ref
        elif op == "ge":
            hard &= v >= ref
        elif op == "lt":
            hard &= v < ref
        elif op == "le":
            hard &= v <= ref
        elif op == "between":
            hard &= (v >= ref[0]) & (v <= ref[1])
        elif op == "in":
            hard &= np.isin(v, np.asarray(ref, v.dtype))
    soft = np.zeros(ctx.n)
    for term in filt.get("soft", []):
        v = ctx.fields[term["field"]].astype(np.float64)
        fn = term.get("fn", "linear")
        scale = float(term.get("scale", 1.0))
        if fn == "linear":
            f = np.clip(v * scale, 0.0, 1.0)
        elif fn == "min1":
            f = np.minimum(1.0, v / scale)
        elif fn == "gauss":
            f = np.exp(-((v - float(term.get("center", 0.0)))
                         / float(term.get("width", 1.0))) ** 2)
        else:  # inv
            f = 1.0 - np.minimum(1.0, v / scale)
        soft += float(term.get("weight", 1.0)) * f
    s = hard * np.maximum(0.0, soft)
    for term in filt.get("mult", []):
        v = ctx.fields[term["field"]].astype(np.float64)
        if term["fn"] == "penalty":
            s = s * (1.0 - np.minimum(float(term.get("cap", 0.8)),
                                      v * float(term.get("scale", 1.0))))
        else:  # bonus
            s = s * (1.0 + float(term.get("weight", 0.5))
                     * np.minimum(1.0, v / float(term.get("scale", 1.0))))
    include = None
    for mask in sp.get("masks", []):
        m = polygon_mask(ctx.centers, mask["polygon"])
        if mask.get("mode", "include") == "include":
            include = m if include is None else (include | m)
        else:
            s = s * ~m
    if include is not None:
        s = s * include
    return np.maximum(0.0, s)


# ---------------------------------------------------------------- phases ----

def ph_species(ctx: Ctx, up, spec):
    suit, hits, records = {}, {}, []
    for sp in spec["species"]:
        s = suitability(ctx, sp)
        suit[sp["id"]] = s
        hits[sp["id"]] = int((s > 0).sum())
        records.append({"kind": "species", "gen_key": sp["id"],
                        "attrs": {"name": sp.get("name", sp["id"]),
                                  "description": sp.get("description", ""),
                                  "filter": sp.get("filter", {}),
                                  "suitable_cells": hits[sp["id"]]}})
    return {"records": records, "state": {"suit": suit},
            "report": {"species_hits": hits}}


def ph_settle_era(ctx: Ctx, up, spec, k: int = 0):
    suit = up["species"]["state"]["suit"]
    prev = up.get(f"settle_era_{k - 1}", {}).get("state") if k else None
    settlements = list(prev["settlements"]) if prev else []
    road = (np.array(prev["road"], bool) if prev
            else np.zeros(ctx.n, bool))
    acc = np.array(prev["acc"]) if prev else np.zeros(ctx.n)
    paths = list(prev["paths"]) if prev else []

    n_eras = int(spec["eras"].get("count", 4))
    shares = np.array(ERA_SHARE[:n_eras])
    shares = shares / shares.sum()
    target = spec.get("settlement_target") or max(
        24, int(ctx.land.sum() / 3500))
    mass = {sid: float(s.sum()) for sid, s in suit.items()}
    weights = {sp["id"]: float(sp.get("share", 1.0)) * mass[sp["id"]]
               for sp in spec["species"]}
    wsum = sum(weights.values()) or 1.0

    taken = [s["cell"] for s in settlements]
    placed = 0
    for sp in spec["species"]:
        sid = sp["id"]
        if mass[sid] <= 0:
            continue
        count = int(round(target * weights[sid] / wsum * shares[k]))
        if k == 0:
            count = max(1, count)
        s_eff = suit[sid] * (1.0 + 1.5 * acc)
        min_dot = math.cos(math.radians(float(
            sp.get("min_separation_deg", 3.2))))
        got = 0
        for i in np.argsort(-s_eff)[: max(400, count * 60)]:
            if got >= count or s_eff[i] <= 0:
                break
            c = ctx.centers[i]
            if all(np.dot(c, ctx.centers[j]) < min_dot for j in taken):
                settlements.append({"cell": int(i), "species": sid, "era": k})
                taken.append(int(i))
                got += 1
        placed += got

    # roads: connect this era's settlements to the existing network
    new_cells = [s["cell"] for s in settlements if s["era"] == k]
    network = np.flatnonzero(road).tolist() + [
        s["cell"] for s in settlements if s["era"] < k]
    if not network and new_cells:
        network = [new_cells[0]]  # era-0 bootstrap: first (best) settlement
    if network and new_cells:
        w = ctx.base_w * np.where(road[ctx.rows] & road[ctx.cols], WEX, 1.0)
        dist, pred, _ = dijkstra(ctx.graph(w), indices=network,
                                 min_only=True, return_predecessors=True,
                                 limit=25000.0)
        net_set = set(network)
        for cell in new_cells:
            if cell in net_set or not np.isfinite(dist[cell]):
                continue
            path, cur = [], cell
            while cur >= 0 and cur not in net_set and len(path) < 4000:
                path.append(int(cur))
                cur = int(pred[cur])
            if cur >= 0:
                path.append(int(cur))
                road[path] = True
                paths.append(path)

    # accessibility write-back for the next era (design §2.1②)
    sources = sorted(set(np.flatnonzero(road).tolist() + taken))
    if sources:
        w = ctx.base_w * np.where(road[ctx.rows] & road[ctx.cols], WEX, 1.0)
        d = dijkstra(ctx.graph(w), indices=sources, min_only=True,
                     limit=6.0 * ACC_SCALE)
        acc = np.where(np.isfinite(d), np.exp(-d / ACC_SCALE), 0.0)

    out = {"state": {"settlements": settlements, "road": road.tolist(),
                     "acc": acc.tolist(), "paths": paths},
           "report": {"placed": placed, "roads": len(paths)}}
    if k == n_eras - 1:
        out["cell_layers"] = {"cell_accessibility": {
            "frequency": ctx.freq, "dtype": "f32",
            "data": acc.astype(np.float32).tobytes()}}
    return out


def ph_hierarchy(ctx: Ctx, up, spec, last_era: str = ""):
    st = up[last_era]["state"]
    suit = up["species"]["state"]["suit"]
    acc = np.array(st["acc"])
    setts = st["settlements"]
    score = np.array([suit[s["species"]][s["cell"]] * (0.5 + acc[s["cell"]])
                      for s in setts])
    order = np.argsort(-score, kind="stable")
    pops = np.zeros(len(setts))
    ranks = np.full(len(setts), 2)
    for zipf_rank, idx in enumerate(order):
        pops[idx] = 60000.0 / (zipf_rank + 1) ** 0.95
    ranks[order[: max(1, len(setts) // 12)]] = 0
    ranks[order[max(1, len(setts) // 12): max(2, len(setts) // 3)]] = 1
    return {"state": {"pops": pops.tolist(), "ranks": ranks.tolist()}}


def ph_cultures(ctx: Ctx, up, spec, last_era: str = ""):
    setts = up[last_era]["state"]["settlements"]
    suit = up["species"]["state"]["suit"]
    pops = up["hierarchy"]["state"]["pops"]
    pmax = int(spec.get("cultures", {}).get("per_species_max", 4))

    hearths, hearth_species = [], []
    by_species: dict[str, list[int]] = {}
    for i, s in enumerate(setts):
        by_species.setdefault(s["species"], []).append(i)
    for sid in sorted(by_species):
        idxs = sorted(by_species[sid], key=lambda i: -pops[i])
        n_c = max(1, min(pmax, len(idxs) // 6))
        min_dot = math.cos(math.radians(25))
        for i in idxs:
            cell = setts[i]["cell"]
            mine = [h for h, hs in zip(hearths, hearth_species) if hs == sid]
            if len(mine) >= n_c:
                break
            if all(np.dot(ctx.centers[cell], ctx.centers[h]) < min_dot
                   for h in mine):
                hearths.append(cell)
                hearth_species.append(sid)

    # species-aware growth cost: a culture expands cheaply over terrain its
    # species finds suitable (the L1.5 causal chain, design §2.2)
    dists = []
    for cell, sid in zip(hearths, hearth_species):
        s = suit[sid]
        s_norm = s / (s.max() or 1.0)
        mult = 1.7 - 1.4 * np.clip(s_norm, 0.0, 1.0)
        w = ctx.base_w * 0.5 * (mult[ctx.rows] + mult[ctx.cols])
        dists.append(dijkstra(ctx.graph(w), indices=cell, min_only=True))
    culture_of = np.argmin(np.stack(dists), axis=0) if hearths else \
        np.zeros(ctx.n, np.int64)

    names, records = [], []
    for gk, (cell, sid) in enumerate(zip(hearths, hearth_species)):
        ng = make_namegen(ctx.world_seed * 131 + gk)
        names.append(ng(9000 + gk) + ("ish" if gk % 2 == 0 else "ic"))
        records.append({"kind": "culture", "gen_key": cell,
                        "attrs": {"name": names[-1], "hearth_cell": cell,
                                  "species_id": stable_id("species", sid)}})
    layer = np.array(hearths, np.float32)[culture_of] if hearths else \
        np.full(ctx.n, -1.0, np.float32)
    return {"records": records,
            "cell_layers": {"cell_culture_hearth": {
                "frequency": ctx.freq, "dtype": "f32",
                "data": layer.astype(np.float32).tobytes()}},
            "state": {"culture_of": culture_of.tolist(), "hearths": hearths,
                      "hearth_species": hearth_species, "names": names},
            "report": {"cultures": len(hearths)}}


def _border_features(ctx: Ctx, polity_of: np.ndarray) -> list:
    """Dual-edge border polylines (ported from civ v1)."""
    rows, cols, nb, centers = ctx.rows, ctx.cols, ctx.nb, ctx.centers
    land = ctx.land
    bmask = ((polity_of[rows] != polity_of[cols]) & land[rows] & land[cols]
             & (rows < cols))
    involved = set(int(x) for x in rows[bmask]) | \
        set(int(x) for x in cols[bmask])
    nb_sets = {i: set(int(x) for x in nb[i] if x >= 0) for i in involved}
    segments = []
    for i, j in zip(rows[bmask], cols[bmask]):
        common = nb_sets[int(i)] & nb_sets[int(j)]
        if len(common) < 2:
            continue
        pts = []
        for k in sorted(common)[:2]:
            c = centers[i] + centers[j] + centers[k]
            c = c / np.linalg.norm(c)
            pts.append((round(math.degrees(math.atan2(c[2], c[0])), 3),
                        round(math.degrees(math.asin(min(1, max(-1, c[1])))), 3)))
        if pts[0] != pts[1]:
            segments.append((pts[0], pts[1]))
    adj: dict = {}
    for a, b in segments:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    seg_set = {frozenset(s) for s in segments}
    used = set()
    feats = []
    for a, b in segments:
        if frozenset((a, b)) in used:
            continue
        line = [a, b]
        used.add(frozenset((a, b)))
        for _ in range(4000):
            tail = line[-1]
            nxt = [q for q in adj.get(tail, [])
                   if frozenset((tail, q)) in seg_set
                   and frozenset((tail, q)) not in used]
            if not nxt:
                break
            line.append(nxt[0])
            used.add(frozenset((tail, nxt[0])))
        for _ in range(4000):
            head = line[0]
            nxt = [q for q in adj.get(head, [])
                   if frozenset((head, q)) in seg_set
                   and frozenset((head, q)) not in used]
            if not nxt:
                break
            line.insert(0, nxt[0])
            used.add(frozenset((head, nxt[0])))
        if len(line) >= 3:
            feats.append(feature(
                "border",
                {"type": "LineString",
                 "coordinates": [[p[0], p[1]] for p in line]}, {}))
    return feats


def ph_polities(ctx: Ctx, up, spec, last_era: str = ""):
    setts = up[last_era]["state"]["settlements"]
    pops = up["hierarchy"]["state"]["pops"]
    cul = up["cultures"]["state"]
    culture_of = np.array(cul["culture_of"])
    fc = float(spec.get("polities", {}).get("foreign_culture_cost", 3.0))
    cap_max = int(spec.get("polities", {}).get("max", 14))
    if not setts:
        return {"records": [], "features": [], "cell_layers": {},
                "state": {"polity_of": [], "caps": []},
                "report": {"polities": 0}}

    caps = []
    for gk in range(len(cul["hearths"])):
        members = [i for i, s in enumerate(setts)
                   if culture_of[s["cell"]] == gk]
        members.sort(key=lambda i: -pops[i])
        take = 2 if len(members) >= 6 else 1
        caps.extend(setts[i]["cell"] for i in members[:take])
    caps = caps[:cap_max] or [setts[0]["cell"]]

    # crossing into foreign-culture cells is expensive (FMG cultureCost
    # analog): borders end up hugging culture lines
    dists = []
    for cap in caps:
        my_cul = culture_of[cap]
        foreign = culture_of != my_cul
        mult = 1.0 + fc * 0.5 * (foreign[ctx.rows] + foreign[ctx.cols])
        dists.append(dijkstra(ctx.graph(ctx.base_w * mult), indices=cap,
                              min_only=True))
    polity_of = np.argmin(np.stack(dists), axis=0)

    names, records = [], []
    for pi, cap in enumerate(caps):
        gk = int(culture_of[cap])
        ng = make_namegen(ctx.world_seed * 131 + gk)
        names.append("Kingdom of " + ng(5000 + pi))
        records.append({"kind": "polity", "gen_key": cap,
                        "attrs": {"name": names[-1], "capital_cell": int(cap),
                                  "culture_id": stable_id(
                                      "culture", int(cul["hearths"][gk]))}})
    layer = np.array(caps, np.float32)[polity_of]
    layer[ctx.ocean] = -1.0
    return {"records": records,
            "features": _border_features(ctx, polity_of),
            "cell_layers": {"cell_polity_capital": {
                "frequency": ctx.freq, "dtype": "f32",
                "data": layer.astype(np.float32).tobytes()}},
            "state": {"polity_of": polity_of.tolist(), "caps": caps},
            "report": {"polities": len(caps)}}


def ph_assemble(ctx: Ctx, up, spec, last_era: str = ""):
    setts = up[last_era]["state"]["settlements"]
    hier = up["hierarchy"]["state"]
    cul = up["cultures"]["state"]
    pol = up["polities"]["state"]
    culture_of = np.array(cul["culture_of"])
    polity_of = np.array(pol["polity_of"])
    era_names = spec["eras"].get("names") or [
        f"纪元 {r}" for r in ("I", "II", "III", "IV", "V", "VI", "VII", "VIII")]
    year_days = float(spec["eras"].get("_year_days", 360.0))
    era_days = float(spec["eras"].get("era_years", 240)) * year_days

    records = []
    for i, s in enumerate(setts):
        cell = s["cell"]
        gk = int(culture_of[cell])
        ng = make_namegen(ctx.world_seed * 131 + gk)
        records.append({"kind": "settlement", "gen_key": cell, "attrs": {
            "name": ng(cell), "rank": int(hier["ranks"][i]), "cell": cell,
            "lon": float(ctx.lonlat[cell, 0]), "lat": float(ctx.lonlat[cell, 1]),
            "population": int(hier["pops"][i]),
            "species_id": stable_id("species", s["species"]),
            "culture_id": stable_id("culture", int(cul["hearths"][gk])),
            "polity_id": stable_id("polity",
                                   int(pol["caps"][int(polity_of[cell])])),
            "founded_era": era_names[s["era"]],
            "founded_day": float(s["era"]) * era_days,
        }})
    return {"records": records, "report": {"settlements": len(setts)}}


def ph_routes(ctx: Ctx, up, spec, last_era: str = ""):
    st = up[last_era]["state"]
    hier = up["hierarchy"]["state"]
    setts = st["settlements"]
    road = np.array(st["road"], bool)
    trunk_max = int(spec.get("routes", {}).get("trunk_max", 14))

    # travel-days graph (horse-ish), roads cut cost
    days_w = ctx.base_w / 55.0 * np.where(
        road[ctx.rows] & road[ctx.cols], 0.45, 1.0)
    tops = [i for i, r in enumerate(hier["ranks"]) if r <= 1][:30]
    cells = [setts[i]["cell"] for i in tops]
    pairs = []
    if len(tops) >= 2:
        dmat = dijkstra(ctx.graph(days_w), indices=cells)
        for a in range(len(tops)):
            for b in range(a + 1, len(tops)):
                days = float(dmat[a, cells[b]])
                if np.isfinite(days) and days < 45.0:
                    grav = (hier["pops"][tops[a]] * hier["pops"][tops[b]]
                            / (days * days + 1.0))
                    pairs.append((grav, a, b, days))
        pairs.sort(key=lambda t: -t[0])
        pairs = pairs[:trunk_max]

    features, records = [], []
    trunk_cells = set()
    graph = ctx.graph(days_w)
    for grav, a, b, days in pairs:
        _, pred = dijkstra(graph, indices=cells[a], return_predecessors=True,
                           limit=days * 1.5)
        path, cur = [], cells[b]
        while cur >= 0 and cur != cells[a] and len(path) < 4000:
            path.append(int(cur))
            cur = int(pred[cur])
        if cur != cells[a]:
            continue
        path.append(cells[a])
        trunk_cells.update(path)
        coords = [[round(float(ctx.lonlat[i, 0]), 3),
                   round(float(ctx.lonlat[i, 1]), 3)]
                  for i in reversed(path)]
        records.append({"kind": "route",
                        "gen_key": f"{min(cells[a], cells[b])}-"
                                   f"{max(cells[a], cells[b])}",
                        "attrs": {"tier": "trunk", "days": round(days, 1),
                                  "from_id": stable_id("settlement", cells[a]),
                                  "to_id": stable_id("settlement", cells[b])}})
        features.append(feature("route", {"type": "LineString",
                                          "coordinates": coords},
                                {"tier": "trunk", "days": round(days, 1)}))
    for path in st["paths"]:
        coords = [[round(float(ctx.lonlat[i, 0]), 3),
                   round(float(ctx.lonlat[i, 1]), 3)] for i in path]
        if len(coords) >= 2:
            features.append(feature("road", {"type": "LineString",
                                             "coordinates": coords}, {}))
    layer = road.astype(np.float32)
    if trunk_cells:
        layer[sorted(trunk_cells)] = 2.0
    return {"records": records, "features": features,
            "cell_layers": {"cell_road": {
                "frequency": ctx.freq, "dtype": "f32",
                "data": layer.tobytes()}},
            "report": {"trunk_routes": len(records)}}


# ------------------------------------------------------------------ main ----

def build_phases(spec: dict) -> list[phasedag.Phase]:
    n_eras = int(spec.get("eras", {}).get("count", 4))
    last = f"settle_era_{n_eras - 1}"
    phases = [phasedag.Phase("species", ph_species, fields=("__all__",),
                             spec_keys=("species",))]
    for k in range(n_eras):
        ups = ("species",) + ((f"settle_era_{k - 1}",) if k else ())
        phases.append(phasedag.Phase(
            f"settle_era_{k}", functools.partial(ph_settle_era, k=k),
            upstream=ups, spec_keys=("eras.count", "settlement_target")))
    phases += [
        phasedag.Phase("hierarchy",
                       functools.partial(ph_hierarchy, last_era=last),
                       upstream=("species", last)),
        phasedag.Phase("cultures",
                       functools.partial(ph_cultures, last_era=last),
                       upstream=("species", last, "hierarchy"),
                       spec_keys=("cultures",)),
        phasedag.Phase("polities",
                       functools.partial(ph_polities, last_era=last),
                       upstream=(last, "hierarchy", "cultures"),
                       spec_keys=("polities",)),
        phasedag.Phase("assemble",
                       functools.partial(ph_assemble, last_era=last),
                       upstream=(last, "hierarchy", "cultures", "polities"),
                       spec_keys=("eras",)),
        phasedag.Phase("routes_trunk",
                       functools.partial(ph_routes, last_era=last),
                       upstream=(last, "hierarchy"),
                       spec_keys=("routes",)),
    ]
    return phases


def generate_civilization(session) -> dict | None:
    result = session.result
    if not result or "cell_elevation_m" not in result.get("cell_layers", {}):
        return None
    spec = getattr(session, "civ_spec", None) or DEFAULT_SPEC
    errors = validate_spec(spec)
    if errors:
        raise ValueError("civ spec invalid: " + "; ".join(errors))

    store = getattr(session, "store", None)
    year_days = 360.0
    if store is not None and store.calendar is not None:
        year_days = float(store.calendar.year_days)
    spec = dict(spec)
    spec["eras"] = dict(spec["eras"])
    spec["eras"]["_year_days"] = year_days

    ctx = Ctx(session)
    if not hasattr(session, "phase_cache"):
        session.phase_cache = {}
    res = phasedag.run(build_phases(spec), ctx, spec, session.phase_cache)

    import rivers as rivers_mod
    features = list(rivers_mod.build(session)["features"])
    features.extend(res.collect("features"))
    result["cell_layers"].update(res.collect_layers())

    records = res.collect("records")
    entities = []
    if store is not None:
        # story-calendar eras (design §1.9⑥: generation eras ARE calendar
        # eras); only seeded when the author hasn't defined any
        if not store.eras:
            n_eras = int(spec["eras"].get("count", 4))
            era_days = float(spec["eras"].get("era_years", 240)) * year_days
            era_names = spec["eras"].get("names") or [
                f"纪元 {r}" for r in
                ("I", "II", "III", "IV", "V", "VI", "VII", "VIII")]
            for k in range(n_eras):
                store.add_era(era_names[k], k * era_days,
                              None if k == n_eras - 1 else (k + 1) * era_days)
        store.apply_generation(records)
        for s in store.find("settlement"):
            features.append(feature(
                "settlement",
                {"type": "Point", "coordinates": [s["lon"], s["lat"]]},
                {"name": s["name"], "rank": s.get("rank", 2),
                 "population": s.get("population"),
                 "polity": store.get(s.get("polity_id", ""), "name"),
                 "culture": store.get(s.get("culture_id", ""), "name"),
                 "species": store.get(s.get("species_id", ""), "name"),
                 "founded_era": s.get("founded_era")}))
            entities.append({"kind": "settlement", "name": s["name"],
                             "cell": s.get("cell"), "locked": False,
                             "provenance": {"seed": ctx.world_seed,
                                            "generator": "civ-v2"}})
        for kind in ("species", "culture", "polity"):
            for e in store.find(kind):
                entities.append({"kind": kind, "name": e.get("name", ""),
                                 "locked": False,
                                 "provenance": {"seed": ctx.world_seed,
                                                "generator": "civ-v2"}})

    session.features = features
    session.entities = entities
    report = {"phases": res.report,
              "counts": {"settlements": sum(
                  1 for r in records if r["kind"] == "settlement"),
                  "cultures": sum(1 for r in records
                                  if r["kind"] == "culture"),
                  "polities": sum(1 for r in records
                                  if r["kind"] == "polity"),
                  "routes": sum(1 for r in records if r["kind"] == "route")}}
    cached = sum(1 for p in res.report if p["cached"])
    print(f"  [civ2] {report['counts']} | phases {cached} cached / "
          f"{len(res.report)} total")
    return report
