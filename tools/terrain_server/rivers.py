"""Vector river pipeline (plan addendum c): rivers are vector features at
every scale.

  1. extract_rivers  - channel chains on the geodesic cell graph with
                       hydraulic attributes (discharge, width via downstream
                       hydraulic geometry, bed elevation)
  2. refine_meanders - deterministic, resolution-independent meander
                       displacement: wavelength ~11 channel widths, endpoints
                       pinned, amplitude bounded by the wavelength
  3. burn_rasters    - stream-burning DEM conditioning: carve a monotone
                       (downstream non-increasing) bed profile into the
                       export raster so rendered valleys align with the
                       vector network
  4. tributary_trees - sub-threshold tributaries as constrained trees that
                       follow the real flow field and terminate on the
                       channel network

Everything is deterministic for (seed, world).
"""

from __future__ import annotations

import math

import numpy as np

R_KM = 6371.0


def _hash01(*keys: int) -> float:
    x = 0x9E3779B97F4A7C15
    for k in keys:
        x = (x ^ (int(k) & 0xFFFFFFFFFFFFFFFF)) * 0xBF58476D1CE4E5B9 % (2 ** 64)
        x = (x ^ (x >> 27)) * 0x94D049BB133111EB % (2 ** 64)
    return ((x >> 11) & ((1 << 53) - 1)) / float(1 << 53)


def to_lonlat(xyz: np.ndarray) -> np.ndarray:
    lat = np.degrees(np.arcsin(np.clip(xyz[..., 1], -1, 1)))
    lon = np.degrees(np.arctan2(xyz[..., 2], xyz[..., 0]))
    return np.stack([lon, lat], axis=-1)


def lonlat_to_xyz(lon_deg, lat_deg) -> np.ndarray:
    lon = np.radians(lon_deg)
    lat = np.radians(lat_deg)
    return np.stack([np.cos(lat) * np.cos(lon), np.sin(lat),
                     np.cos(lat) * np.sin(lon)], axis=-1)


def width_m_from_accum(accum_m2) -> np.ndarray:
    """Hydraulic geometry: accum is runoff-equivalent area at 500 mm/yr, so
    Q [m^3/s] ~ accum * 0.5 m/yr / 3.15e7 s; width ~ 7 sqrt(Q) (Leopold)."""
    q = np.maximum(0.0, np.asarray(accum_m2, dtype=np.float64)) * 0.5 / 3.15e7
    return 7.0 * np.sqrt(q)


# ------------------------------------------------------------- extract ----

def extract_rivers(session):
    """Channel chains with per-vertex attributes. Returns a dict with the
    rivers list, the receiver array and the channel mask (cell space)."""
    result = session.result
    entry = result["cell_layers"]["cell_elevation_m"]
    freq = int(entry["frequency"])

    def cl(name):
        return np.frombuffer(result["cell_layers"][name]["data"], np.float32)

    import weterrain
    z = cl("cell_elevation_m")
    ocean = cl("cell_ocean") > 0.5
    accum = cl("cell_flow_accum_m2")
    g = weterrain.geodesic_graph(freq)
    n = g["cell_count"]
    nb = np.frombuffer(g["neighbors_i32"], np.int32).reshape(n, 6)
    centers = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
    lonlat = to_lonlat(centers)
    land = ~ocean

    river_thr = float(session.params.get("hydrology.river_area_threshold_m2", 8e9))
    channel = (accum > river_thr) & land

    valid = nb >= 0
    z_nb = np.where(valid, z[np.clip(nb, 0, n - 1)], np.inf)
    acc_nb = np.where(valid & (z_nb < z[:, None]), accum[np.clip(nb, 0, n - 1)], -1.0)
    has_lower = acc_nb.max(axis=1) > 0
    recv = np.full(n, -1, np.int64)
    recv[has_lower] = nb[np.arange(n), acc_nb.argmax(axis=1)][has_lower]

    upstream_max = np.zeros(n, np.float32)
    for i in np.nonzero(channel)[0]:
        r = recv[i]
        if r >= 0 and channel[r]:
            upstream_max[r] = max(upstream_max[r], accum[i])
    heads = channel & (upstream_max < accum * 0.5)

    spacing_km = 2.0 * math.pi * R_KM / (5.0 * freq)
    rivers = []
    visited = np.zeros(n, bool)
    for h in np.nonzero(heads)[0]:
        if visited[h]:
            continue
        path = [int(h)]
        cur = int(h)
        while True:
            visited[cur] = True
            nxt = int(recv[cur])
            if nxt < 0 or len(path) > 4000:
                break
            path.append(nxt)
            if ocean[nxt] or visited[nxt]:
                break
            cur = nxt
        if len(path) < 5 or accum[path[-1]] <= river_thr * 1.5:
            continue
        cells = np.asarray(path)
        rivers.append({
            "id": len(rivers),
            "cells": cells,
            "coords": lonlat[cells].round(4).tolist(),
            "z": z[cells].astype(float).round(1).tolist(),
            "accum": accum[cells].astype(float).tolist(),
            "width_m": width_m_from_accum(accum[cells]).round(1).tolist(),
            "discharge_m2": float(accum[cells[-1]]),
            "mouth_ocean": bool(ocean[cells[-1]]),
        })
    rivers.sort(key=lambda r: -r["discharge_m2"])
    for k, r in enumerate(rivers):
        r["id"] = k
    return {"rivers": rivers, "recv": recv, "channel": channel,
            "freq": freq, "spacing_km": spacing_km, "lonlat": lonlat,
            "accum": accum, "z": z, "ocean": ocean, "river_thr": river_thr}


# ------------------------------------------------------------ meanders ----

def refine_meanders(river: dict, seed: int, spacing_km: float,
                    subdiv_target_km: float | None = None) -> list:
    """Deterministic meander displacement. Wavelength ~11 channel widths
    (clamped to [2 km, 2 cell spacings]), lateral amplitude 0.12 wavelength,
    endpoints pinned with a smooth taper. Output lon/lat polyline."""
    coords = np.asarray(river["coords"], dtype=np.float64)
    widths = np.asarray(river["width_m"], dtype=np.float64)
    xyz = lonlat_to_xyz(coords[:, 0], coords[:, 1])

    seg = np.linalg.norm(np.diff(xyz, axis=0), axis=1) * R_KM  # chord km
    target = subdiv_target_km or max(1.0, spacing_km / 4.0)
    pts = [xyz[0]]
    w_out = [widths[0]]
    s_out = [0.0]
    s = 0.0
    for i in range(len(seg)):
        k = max(1, int(math.ceil(seg[i] / target)))
        for j in range(1, k + 1):
            t = j / k
            p = xyz[i] * (1 - t) + xyz[i + 1] * t
            p /= np.linalg.norm(p)
            pts.append(p)
            w_out.append(widths[i] * (1 - t) + widths[i + 1] * t)
            s_out.append(s + seg[i] * t)
        s += seg[i]
    pts = np.asarray(pts)
    w_out = np.asarray(w_out)
    s_out = np.asarray(s_out)
    total = max(1e-6, s_out[-1])

    disp = np.zeros(len(pts))
    for octave in range(3):
        lam = np.clip(11.0 * w_out / 1000.0, 2.0, 2.0 * spacing_km) / (octave + 1)
        amp = 0.12 * lam
        phase = _hash01(seed, river["id"], octave) * 2 * math.pi
        disp += amp * np.sin(2 * math.pi * s_out / np.maximum(lam, 1e-6) + phase)
    taper = np.minimum(1.0, np.minimum(s_out, total - s_out) / (0.1 * total + 1e-9))
    disp *= np.sin(np.clip(taper, 0, 1) * math.pi / 2) ** 2

    out = np.empty_like(pts)
    for i in range(len(pts)):
        if i == 0 or i == len(pts) - 1:
            out[i] = pts[i]
            continue
        t = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]
        t /= max(1e-12, np.linalg.norm(t))
        side = np.cross(pts[i], t)
        side /= max(1e-12, np.linalg.norm(side))
        ang = disp[i] / R_KM  # km -> radians of arc
        p = pts[i] + side * ang
        out[i] = p / np.linalg.norm(p)
    return to_lonlat(out).round(4).tolist()


# ------------------------------------------------------------- burning ----

def burn_rasters(session, rivers_out) -> None:
    """Stream-burn the vector network into the export raster: along each
    river, carve a monotone (cummin) bed profile whose depth grows with
    discharge. Writes layer 'elevation_conditioned_m'."""
    result = session.result
    w = result["width"]
    h = result["height"]
    base = np.frombuffer(result["layers"]["elevation_eroded_m"]["data"],
                         np.float32).reshape(h, w).copy()

    def px_of(lon, lat):
        x = int(((lon + 180.0) / 360.0) * w) % w
        y = min(h - 1, max(0, int(((90.0 - lat) / 180.0) * h)))
        return x, y

    for river in rivers_out["rivers"]:
        coords = river.get("coords_refined", river["coords"])
        widths = np.asarray(river["width_m"], dtype=np.float64)
        depth = np.clip(2.0 + 0.06 * widths.mean(), 2.0, 40.0)
        bed = None
        for lon, lat in coords:
            x, y = px_of(lon, lat)
            zpx = float(base[y, x])
            bed = zpx - depth if bed is None else min(bed, zpx - depth)
            if base[y, x] > bed:
                base[y, x] = bed
    result["layers"]["elevation_conditioned_m"] = {
        "dtype": "f32", "data": base.astype(np.float32).tobytes()}


# ---------------------------------------------------------- tributaries ---

def tributary_trees(extraction, max_features: int = 400) -> list:
    """Sub-threshold tributaries: chains that follow the receiver field from
    minor-flow cells until they land on the channel network. Constrained
    space-filling by construction (they trace the real flow tree)."""
    accum = extraction["accum"]
    channel = extraction["channel"]
    recv = extraction["recv"]
    ocean = extraction["ocean"]
    lonlat = extraction["lonlat"]
    thr = extraction["river_thr"]

    cand = np.nonzero((accum > thr / 8.0) & (accum <= thr) & ~channel & ~ocean)[0]
    cand = cand[np.argsort(-accum[cand])]
    feats = []
    used = np.zeros(len(accum), bool)
    for c in cand:
        if len(feats) >= max_features:
            break
        if used[c]:
            continue
        path = [int(c)]
        cur = int(c)
        hit = False
        for _ in range(200):
            nxt = int(recv[cur])
            if nxt < 0 or ocean[nxt]:
                break
            path.append(nxt)
            used[cur] = True
            if channel[nxt]:
                hit = True
                break
            if used[nxt]:
                break
            cur = nxt
        if hit and len(path) >= 3:
            coords = [[round(float(lonlat[i, 0]), 3), round(float(lonlat[i, 1]), 3)]
                      for i in path]
            feats.append({"type": "Feature", "kind": "minor_river",
                          "geometry": {"type": "LineString", "coordinates": coords},
                          "properties": {"kind": "minor_river",
                                         "discharge_m2": float(accum[path[0]])}})
    return feats


# ---------------------------------------------------------------- main ----

def build(session, refine: bool = True, burn: bool = True,
          minor: bool = True) -> dict:
    """Full vector-river pass; returns {'features': [...], 'extraction': ...}."""
    seed = int(session.params.get("world.seed", 1337))
    ext = extract_rivers(session)
    features = []
    for river in ext["rivers"]:
        if refine:
            river["coords_refined"] = refine_meanders(river, seed,
                                                      ext["spacing_km"])
        coords = river.get("coords_refined", river["coords"])
        features.append({"type": "Feature", "kind": "river",
                         "geometry": {"type": "LineString", "coordinates": coords},
                         "properties": {"kind": "river",
                                        "discharge_m2": river["discharge_m2"],
                                        "width_m": river["width_m"][-1],
                                        "river_id": river["id"]}})
    if burn:
        burn_rasters(session, ext)
    if minor:
        features.extend(tributary_trees(ext))
    return {"features": features, "extraction": ext}
