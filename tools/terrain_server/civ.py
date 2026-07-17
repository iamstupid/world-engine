"""Civilization layer (plan M11): resources, settlements, cultures, polities,
names, roads and vector rivers, computed on the geodesic cell graph exported
by the kernel. Everything lands as editable entities + GeoJSON features
(stable ids, gen provenance) rather than baked pixels.

Deliberate v1 scale: runs at the physics grid frequency the session used;
borders are emitted as dense midpoint chains (dual-edge tracing is a later
upgrade); sea lanes deferred.
"""

from __future__ import annotations

import math

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra

import weterrain
from worldstore import stable_id

# ---------------------------------------------------------------- names ----

CONSONANTS = "b c d f g h j k l m n p q r s t v w x z br cr dr gr kr tr st sk sh th kh vl".split()
VOWELS = "a e i o u a e i o ai ei ou ua ie".split()


def _rng(seed: int) -> np.random.Generator:
    return np.random.default_rng(np.uint64(seed) * np.uint64(2654435761) % np.uint64(2**63))


def make_namegen(culture_seed: int):
    rng = _rng(culture_seed)
    cons = list(rng.choice(CONSONANTS, size=8, replace=False))
    vows = list(rng.choice(VOWELS, size=5, replace=False))
    endings = list(rng.choice(
        ["a", "os", "ia", "un", "ar", "eth", "or", "an", "is", "ul", "heim", "gard",
         "dor", "wick", "mar"], size=4, replace=False))

    def name(entity_seed: int) -> str:
        r = _rng(culture_seed * 7919 + entity_seed)
        n_syll = 1 + int(r.integers(0, 2))
        s = ""
        for _ in range(n_syll):
            s += cons[int(r.integers(0, len(cons)))] + vows[int(r.integers(0, len(vows)))]
        s += endings[int(r.integers(0, len(endings)))]
        return s.capitalize()

    return name


# ------------------------------------------------------------ geometry ----


def to_lonlat(centers: np.ndarray) -> np.ndarray:
    """Cell centers (n,3, y-up domain frame) -> (n,2) lon/lat degrees."""
    lat = np.degrees(np.arcsin(np.clip(centers[:, 1], -1, 1)))
    lon = np.degrees(np.arctan2(centers[:, 2], centers[:, 0]))
    return np.stack([lon, lat], axis=1)


def feature(kind: str, geometry: dict, props: dict) -> dict:
    props = dict(props)
    props["kind"] = kind
    return {"type": "Feature", "kind": kind, "geometry": geometry,
            "properties": props}


# ---------------------------------------------------------------- main ----


def generate_civilization(session) -> None:
    result = session.result
    if not result or "cell_elevation_m" not in result.get("cell_layers", {}):
        return
    freq = result["cell_layers"]["cell_elevation_m"]["frequency"]
    seed = int(session.params.get("world.seed", 1337))

    def cl(name):
        return np.frombuffer(result["cell_layers"][name]["data"], np.float32)

    z = cl("cell_elevation_m")
    ocean = cl("cell_ocean") > 0.5
    accum = cl("cell_flow_accum_m2")
    temp = cl("cell_temperature_c")
    runoff = cl("cell_runoff_mm")
    veg = cl("cell_vegetation")
    biome = cl("cell_biome").astype(np.int32)

    g = weterrain.geodesic_graph(freq)
    n = g["cell_count"]
    nb = np.frombuffer(g["neighbors_i32"], np.int32).reshape(n, 6)
    centers = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
    areas = np.frombuffer(g["areas_f64"], np.float64) * (6371.0 ** 2)  # km^2
    lonlat = to_lonlat(centers)
    land = ~ocean

    valid = nb >= 0
    rows = np.repeat(np.arange(n), 6)[valid.ravel()]
    cols = nb.ravel()[valid.ravel()]
    edge_km = np.linalg.norm(centers[rows] - centers[cols], axis=1) * 6371.0

    # ---- vector rivers (rivers.py: extraction + meanders + stream burning
    # + sub-threshold tributaries; plan addendum c) ----
    import rivers as rivers_mod
    river_thr = float(session.params.get("hydrology.river_area_threshold_m2", 8e9))
    riv = rivers_mod.build(session)
    features = list(riv["features"])

    # ---- suitability & settlements ----
    slope = np.zeros(n, np.float32)
    zdiff = np.abs(np.where(valid, z[np.clip(nb, 0, n - 1)] - z[:, None], 0)).max(axis=1)
    slope = zdiff / 40000.0  # ~ per cell spacing
    coast = land & (np.where(valid, ocean[np.clip(nb, 0, n - 1)], False).any(axis=1))
    river_bonus = np.minimum(1.0, accum / (river_thr * 2)) * land
    comfort = np.exp(-((temp - 14.0) / 16.0) ** 2)
    water = np.minimum(1.0, runoff / 400.0)
    pop = land * (0.45 * veg + 0.3 * water + 0.25 * comfort) * (1.0 - np.minimum(0.8, slope))
    pop = pop * (1.0 + 0.5 * coast + 0.8 * river_bonus)
    pop[biome == 1] = 0  # ice

    target = max(24, int(land.sum() / 3500))
    order = np.argsort(-pop)
    chosen = []
    min_dot = math.cos(math.radians(3.2))  # min separation ~3.2 deg
    for i in order[: target * 40]:
        if pop[i] <= 0:
            break
        c = centers[i]
        if all(np.dot(c, centers[j]) < min_dot for j in chosen):
            chosen.append(int(i))
            if len(chosen) >= target:
                break

    # ---- cultures ----
    n_cult = min(6, max(2, len(chosen) // 8))
    hearths = []
    for i in chosen:
        if len(hearths) >= n_cult:
            break
        if all(np.dot(centers[i], centers[h]) < math.cos(math.radians(25)) for h in hearths):
            hearths.append(i)
    while len(hearths) < n_cult:
        hearths.append(chosen[len(hearths)])

    friction = np.where(ocean, 6.0, 1.0 + 4.0 * np.minimum(1.0, slope * 3))
    friction = friction + np.where((biome == 8) | (biome == 2), 2.5, 0.0)
    friction = friction + np.where((biome == 1) | (biome == 9), 4.0, 0.0)
    w_edge = edge_km * 0.5 * (friction[rows] + friction[cols])
    graph = csr_matrix((w_edge, (rows, cols)), shape=(n, n))

    dist_c = dijkstra(graph, indices=hearths, min_only=False)
    culture_of = np.argmin(dist_c, axis=0)

    culture_names = []
    namegens = []
    for k in range(n_cult):
        ng = make_namegen(seed * 131 + k)
        namegens.append(ng)
        culture_names.append(ng(9000 + k) + ("ish" if k % 2 == 0 else "ic"))

    # ---- polities ----
    caps = []
    for k in range(n_cult):
        members = [i for i in chosen if culture_of[i] == k]
        members.sort(key=lambda i: -pop[i])
        caps.extend(members[:2] if len(members) >= 6 else members[:1])
    caps = caps[:14] if caps else chosen[:1]
    dist_p = dijkstra(graph, indices=caps, min_only=False)
    polity_of = np.argmin(dist_p, axis=0)
    polity_names = []
    for pi, cap in enumerate(caps):
        k = int(culture_of[cap])
        polity_names.append("Kingdom of " + namegens[k](5000 + pi))

    # ---- settlement entities/features ----
    entities = []
    pops = pop[chosen]
    ranks = np.full(len(chosen), 2)
    order2 = np.argsort(-pops)
    ranks[order2[: max(1, len(chosen) // 12)]] = 0
    ranks[order2[max(1, len(chosen) // 12): max(2, len(chosen) // 3)]] = 1
    for idx, cell in enumerate(chosen):
        k = int(culture_of[cell])
        pi = int(polity_of[cell])
        nm = namegens[k](cell)
        entities.append({
            "kind": "settlement", "name": nm, "cell": cell, "rank": int(ranks[idx]),
            "culture": culture_names[k], "polity": polity_names[pi],
            "lon": float(lonlat[cell, 0]), "lat": float(lonlat[cell, 1]),
            "locked": False, "provenance": {"seed": seed, "generator": "civ-v1"},
        })
        features.append(feature("settlement",
                                {"type": "Point",
                                 "coordinates": [float(lonlat[cell, 0]), float(lonlat[cell, 1])]},
                                {"name": nm, "rank": int(ranks[idx]),
                                 "polity": polity_names[pi], "culture": culture_names[k]}))

    for k in range(n_cult):
        entities.append({"kind": "culture", "name": culture_names[k],
                         "hearth_cell": int(hearths[k]), "locked": False,
                         "provenance": {"seed": seed, "generator": "civ-v1"}})
    for pi, cap in enumerate(caps):
        entities.append({"kind": "polity", "name": polity_names[pi],
                         "capital_cell": int(cap),
                         "culture": culture_names[int(culture_of[cap])],
                         "locked": False,
                         "provenance": {"seed": seed, "generator": "civ-v1"}})

    # ---- borders: proper dual-edge polylines ----
    # The dual edge separating cells (i, j) connects the centroids of the two
    # lattice triangles sharing edge (i, j), i.e. (i, j, k) for the two common
    # neighbors k. Segments are chained into LineStrings by endpoint matching.
    pol_r = polity_of[rows]
    pol_c = polity_of[cols]
    bmask = (pol_r != pol_c) & land[rows] & land[cols] & (rows < cols)
    involved = set(int(x) for x in rows[bmask]) | set(int(x) for x in cols[bmask])
    nb_sets = {i: set(int(x) for x in nb[i] if x >= 0) for i in involved}
    segments = []
    for i, j in zip(rows[bmask], cols[bmask]):
        common = nb_sets[int(i)] & nb_sets[int(j)]
        if len(common) < 2:
            continue
        ks = sorted(common)[:2]
        pts = []
        for k in ks:
            c = centers[i] + centers[j] + centers[k]
            c = c / np.linalg.norm(c)
            pts.append((round(math.degrees(math.atan2(c[2], c[0])), 3),
                        round(math.degrees(math.asin(min(1, max(-1, c[1])))), 3)))
        if pts[0] != pts[1]:
            segments.append((pts[0], pts[1]))
    # chain segments into polylines
    adj = {}
    for a, b in segments:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    seg_set = {frozenset(s) for s in segments}
    used = set()
    for a, b in segments:
        if frozenset((a, b)) in used:
            continue
        line = [a, b]
        used.add(frozenset((a, b)))
        for _ in range(4000):  # extend forward
            tail = line[-1]
            nxt = [q for q in adj.get(tail, [])
                   if frozenset((tail, q)) in seg_set and
                   frozenset((tail, q)) not in used]
            if not nxt:
                break
            line.append(nxt[0])
            used.add(frozenset((tail, nxt[0])))
        for _ in range(4000):  # extend backward
            head = line[0]
            nxt = [q for q in adj.get(head, [])
                   if frozenset((head, q)) in seg_set and
                   frozenset((head, q)) not in used]
            if not nxt:
                break
            line.insert(0, nxt[0])
            used.add(frozenset((head, nxt[0])))
        if len(line) >= 3:
            features.append(feature(
                "border",
                {"type": "LineString", "coordinates": [[p[0], p[1]] for p in line]},
                {}))

    # ---- roads: MST over top settlements, least-cost paths ----
    road_mask = np.zeros(n, np.float32)
    tops = [chosen[i] for i in order2[: min(28, len(chosen))]]
    if len(tops) >= 2:
        pts = centers[tops]
        ang = np.arccos(np.clip(pts @ pts.T, -1, 1))
        in_tree = {0}
        edges = []
        while len(in_tree) < len(tops):
            best = (1e9, None, None)
            for a in in_tree:
                for b in range(len(tops)):
                    if b not in in_tree and ang[a, b] < best[0]:
                        best = (ang[a, b], a, b)
            edges.append((best[1], best[2]))
            in_tree.add(best[2])
        land_graph = csr_matrix(
            (np.where(ocean[rows] | ocean[cols], 1e6, w_edge), (rows, cols)),
            shape=(n, n))
        for a, b in edges:
            if ang[a, b] > math.radians(35):
                continue
            ang_km = float(ang[a, b]) * 6371.0
            d, pred = dijkstra(land_graph, indices=tops[a], return_predecessors=True,
                               limit=max(800.0, ang_km * 6.0))
            if not np.isfinite(d[tops[b]]) or d[tops[b]] >= 5e5:
                continue
            path = []
            cur = tops[b]
            while cur >= 0 and cur != tops[a] and len(path) < 3000:
                path.append(int(cur))
                cur = int(pred[cur])
            path.append(int(tops[a]))
            coords = [[round(float(lonlat[i, 0]), 3), round(float(lonlat[i, 1]), 3)]
                      for i in reversed(path)]
            road_mask[path] = 1.0
            features.append(feature("road", {"type": "LineString", "coordinates": coords}, {}))

    # ---- cell layers for the query engine (M13): polity/culture identity is
    # keyed by capital/hearth CELL id, so describe() can resolve entities by
    # stable id without side tables ----
    pol_cap_cell = np.array(caps, np.float32)[polity_of]
    pol_cap_cell[ocean] = -1.0
    cul_hearth_cell = np.array(hearths, np.float32)[culture_of]
    result["cell_layers"]["cell_polity_capital"] = {
        "frequency": freq, "dtype": "f32", "data": pol_cap_cell.tobytes()}
    result["cell_layers"]["cell_culture_hearth"] = {
        "frequency": freq, "dtype": "f32", "data": cul_hearth_cell.tobytes()}
    result["cell_layers"]["cell_road"] = {
        "frequency": freq, "dtype": "f32", "data": road_mask.tobytes()}

    # ---- entity store merge (M12): regeneration never clobbers user edits ----
    store = getattr(session, "store", None)
    if store is not None:
        records = []
        for k in range(n_cult):
            records.append({"kind": "culture", "gen_key": int(hearths[k]),
                            "attrs": {"name": culture_names[k],
                                      "hearth_cell": int(hearths[k])}})
        for pi, cap in enumerate(caps):
            records.append({"kind": "polity", "gen_key": int(cap),
                            "attrs": {"name": polity_names[pi],
                                      "capital_cell": int(cap),
                                      "culture_id": stable_id(
                                          "culture", int(hearths[int(culture_of[cap])]))}})
        for idx, cell in enumerate(chosen):
            records.append({"kind": "settlement", "gen_key": int(cell),
                            "attrs": {
                                "name": namegens[int(culture_of[cell])](cell),
                                "rank": int(ranks[idx]), "cell": int(cell),
                                "lon": float(lonlat[cell, 0]),
                                "lat": float(lonlat[cell, 1]),
                                "polity_id": stable_id("polity",
                                                       int(caps[int(polity_of[cell])])),
                                "culture_id": stable_id("culture",
                                                        int(hearths[int(culture_of[cell])])),
                            }})
        store.apply_generation(records)
        # Settlement features reflect the MERGED names (user renames survive).
        features = [f for f in features if f["kind"] != "settlement"]
        for s in store.find("settlement"):
            features.append(feature(
                "settlement",
                {"type": "Point", "coordinates": [s["lon"], s["lat"]]},
                {"name": s["name"], "rank": s.get("rank", 2),
                 "polity": store.get(s.get("polity_id", ""), "name"),
                 "culture": store.get(s.get("culture_id", ""), "name")}))

    session.features = features
    session.entities = entities
    print(f"  [civ] {len(chosen)} settlements, {n_cult} cultures, {len(caps)} polities, "
          f"{sum(1 for f in features if f['kind'] == 'river')} rivers, "
          f"{sum(1 for f in features if f['kind'] == 'road')} roads")
