"""Novelist-facing geographic queries (plan M13): route (travel time by
mode + narrative itinerary), reachable isochrones, describe context packs,
news arrival times, viewsheds. Pure computation on the geodesic cell graph -
no UI dependencies; exercised headlessly by pytest.
"""

from __future__ import annotations

import math

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra
from scipy.spatial import cKDTree

import weterrain
from worldstore import stable_id

R_KM = 6371.0
BIOME_MULT = {0: 0.0, 1: 0.35, 2: 0.7, 3: 0.75, 4: 0.85, 5: 1.0, 6: 0.6,
              7: 0.95, 8: 0.55, 9: 0.4, 10: 0.8}
SPEED_KM_DAY = {"walk": 30.0, "horse": 55.0, "cart": 35.0, "ship": 130.0}
BIOME_NAMES = ["ocean", "ice sheet", "tundra", "boreal forest",
               "temperate forest", "grassland", "tropical rainforest",
               "savanna", "desert", "alpine heights", "wetland"]


class GeoIndex:
    def __init__(self, session):
        result = session.result
        self.freq = result["cell_layers"]["cell_elevation_m"]["frequency"]

        def cl(name):
            if name not in result["cell_layers"]:
                return None
            return np.frombuffer(result["cell_layers"][name]["data"], np.float32)

        self.z = cl("cell_elevation_m")
        self.ocean = cl("cell_ocean") > 0.5
        self.biome = cl("cell_biome")
        self.biome = (self.biome.astype(np.int32) if self.biome is not None
                      else np.zeros(self.z.size, np.int32))
        self.temp = cl("cell_temperature_c")
        self.precip = cl("cell_precip_mm")
        self.accum = cl("cell_flow_accum_m2")
        self.polity_cap = cl("cell_polity_capital")
        self.culture_hearth = cl("cell_culture_hearth")
        self.road = cl("cell_road")
        self.road = (self.road > 0.5) if self.road is not None else np.zeros(
            self.z.size, bool)

        g = weterrain.geodesic_graph(self.freq)
        n = self.n = g["cell_count"]
        self.nb = np.frombuffer(g["neighbors_i32"], np.int32).reshape(n, 6)
        self.centers = np.frombuffer(g["centers_f64"], np.float64).reshape(n, 3)
        self.areas_km2 = np.frombuffer(g["areas_f64"], np.float64) * R_KM * R_KM
        self.tree = cKDTree(self.centers)

        valid = self.nb >= 0
        self.rows = np.repeat(np.arange(n), 6)[valid.ravel()]
        self.cols = self.nb.ravel()[valid.ravel()]
        self.edge_km = np.linalg.norm(
            self.centers[self.rows] - self.centers[self.cols], axis=1) * R_KM
        zdiff = np.abs(self.z[self.rows] - self.z[self.cols])
        self.slope = zdiff / (self.edge_km * 1000.0 + 1.0)
        river_thr = float(session.params.get("hydrology.river_area_threshold_m2", 8e9))
        self.is_river = (self.accum > river_thr) & (~self.ocean)
        self._graphs: dict[str, csr_matrix] = {}
        self.store = getattr(session, "store", None)

    # ---- basics ----

    def lonlat_to_dir(self, lon: float, lat: float) -> np.ndarray:
        la, lo = math.radians(lat), math.radians(lon)
        return np.array([math.cos(la) * math.cos(lo), math.sin(la),
                         math.cos(la) * math.sin(lo)])

    def cell_lonlat(self, i: int) -> tuple[float, float]:
        c = self.centers[i]
        return (math.degrees(math.atan2(c[2], c[0])),
                math.degrees(math.asin(max(-1.0, min(1.0, c[1])))))

    def nearest_cell(self, lon: float, lat: float) -> int:
        return int(self.tree.query(self.lonlat_to_dir(lon, lat))[1])

    # ---- travel graphs ----

    def graph(self, mode: str) -> csr_matrix:
        if mode in self._graphs:
            return self._graphs[mode]
        base = SPEED_KM_DAY[mode]
        r, c = self.rows, self.cols
        if mode == "ship":
            passable = self.ocean[r] & self.ocean[c]
            days = self.edge_km / base
            days = np.where(passable, days, np.inf)
        else:
            mult = np.array([BIOME_MULT.get(int(b), 0.8) for b in self.biome])
            eff = (mult[r] + mult[c]) * 0.5 / (1.0 + 9.0 * self.slope)
            road_edge = self.road[r] & self.road[c]
            eff = np.where(road_edge, np.maximum(eff * 1.8, 0.9), eff)
            days = self.edge_km / (base * np.maximum(eff, 1e-3))
            crossing = self.is_river[c] & ~self.is_river[r] & ~road_edge
            days = days + np.where(crossing, 0.25, 0.0)
            days = np.where(self.ocean[r] | self.ocean[c], np.inf, days)
        finite = np.isfinite(days)
        g = csr_matrix((days[finite], (r[finite], c[finite])), shape=(self.n, self.n))
        self._graphs[mode] = g
        return g

    def _snap(self, cell: int, mode: str) -> int:
        want_ocean = mode == "ship"
        if self.ocean[cell] == want_ocean:
            return cell
        _, idx = self.tree.query(self.centers[cell], k=256)
        for j in idx:
            if self.ocean[j] == want_ocean:
                return int(j)
        return cell

    # ---- queries ----

    def route(self, a_lonlat, b_lonlat, mode: str = "horse") -> dict:
        a = self._snap(self.nearest_cell(*a_lonlat), mode)
        b = self._snap(self.nearest_cell(*b_lonlat), mode)
        d, pred = dijkstra(self.graph(mode), indices=a, return_predecessors=True)
        if not np.isfinite(d[b]):
            return {"reachable": False, "mode": mode}
        path = []
        cur = b
        while cur != a and cur >= 0 and len(path) < 20000:
            path.append(int(cur))
            cur = int(pred[cur])
        path.append(int(a))
        path.reverse()
        km = sum(np.linalg.norm(self.centers[path[i + 1]] - self.centers[path[i]])
                 for i in range(len(path) - 1)) * R_KM
        return {"reachable": True, "mode": mode, "days": round(float(d[b]), 2),
                "distance_km": round(float(km), 1),
                "path": [self.cell_lonlat(i) for i in path[:: max(1, len(path) // 120)]],
                "narrative": self._narrative(path, mode)}

    def _narrative(self, path: list[int], mode: str) -> list[str]:
        notes = []
        if mode == "ship":
            return [f"a sea voyage of {len(path)} legs"]
        segs = []
        for i in path:
            b = int(self.biome[i])
            if not segs or segs[-1][0] != b:
                segs.append([b, 1])
            else:
                segs[-1][1] += 1
        for b, count in segs:
            if count >= 3 and b != 0:
                notes.append(f"through {BIOME_NAMES[b]} ({count} leagues)")
        zmax = max(float(self.z[i]) for i in path)
        if zmax > 2200:
            notes.append(f"crosses a high pass at {int(zmax)} m")
        crossings = sum(1 for i in path if self.is_river[i])
        if crossings:
            notes.append(f"fords or bridges {crossings} river reaches")
        pol = [self._polity_name(i) for i in path]
        borders = [p for prev, p in zip(pol, pol[1:]) if p and p != prev]
        for p in dict.fromkeys(borders):
            notes.append(f"enters {p}")
        return notes[:12]

    def _polity_name(self, cell: int) -> str | None:
        if self.polity_cap is None or self.store is None or self.ocean[cell]:
            return None
        cap = int(self.polity_cap[cell])
        if cap < 0:
            return None
        return self.store.get(stable_id("polity", cap), "name")

    def _culture_name(self, cell: int) -> str | None:
        if self.culture_hearth is None or self.store is None:
            return None
        h = int(self.culture_hearth[cell])
        return self.store.get(stable_id("culture", h), "name") if h >= 0 else None

    def reachable(self, lonlat, days: float, mode: str = "horse") -> dict:
        a = self._snap(self.nearest_cell(*lonlat), mode)
        d = dijkstra(self.graph(mode), indices=a, limit=days)
        inside = np.isfinite(d)
        return {"cells": int(inside.sum()),
                "area_km2": round(float(self.areas_km2[inside].sum()), 0),
                "max_days": days, "mode": mode}

    def describe(self, lon: float, lat: float, as_of: float | None = None) -> dict:
        i = self.nearest_cell(lon, lat)
        out = {"lon": lon, "lat": lat,
               "elevation_m": round(float(self.z[i]), 1),
               "ocean": bool(self.ocean[i]),
               "biome": BIOME_NAMES[int(self.biome[i])],
               "temperature_c": round(float(self.temp[i]), 1) if self.temp is not None else None,
               "precipitation_mm_yr": round(float(self.precip[i]), 0) if self.precip is not None else None}
        pol = self._polity_name(i)
        cul = self._culture_name(i)
        if pol:
            out["polity"] = pol
        if cul:
            out["culture"] = cul
        if self.store is not None:
            best = None
            for s in self.store.find("settlement", as_of=as_of):
                if s.get("_retired"):
                    continue
                d = math.acos(max(-1, min(1, float(np.dot(
                    self.centers[i], self.lonlat_to_dir(s["lon"], s["lat"])))))) * R_KM
                if best is None or d < best[0]:
                    best = (d, s)
            if best is not None:
                out["nearest_settlement"] = {"name": best[1]["name"],
                                             "distance_km": round(best[0], 0),
                                             "rank": best[1].get("rank")}
        if self.store is not None and as_of is not None and self.store.era_of(as_of):
            out["era"] = self.store.era_of(as_of)
        return out

    def news_arrival(self, event_lonlat, listener_lonlat) -> dict:
        """Earliest arrival of news via courier relays (fast horse) or ship."""
        options = {}
        horse = self.route(event_lonlat, listener_lonlat, "horse")
        if horse.get("reachable"):
            options["courier"] = round(horse["days"] * 0.6, 2)  # relays ride harder
        ship = self.route(event_lonlat, listener_lonlat, "ship")
        if ship.get("reachable"):
            options["ship"] = round(ship["days"] + 0.5, 2)  # port overhead
        if not options:
            return {"reachable": False}
        best = min(options, key=options.get)
        return {"reachable": True, "days": options[best], "via": best,
                "options": options}

    def viewshed(self, lon: float, lat: float, observer_m: float = 10.0) -> dict:
        i = self.nearest_cell(lon, lat)
        origin = self.centers[i]
        z0 = max(0.0, float(self.z[i])) + observer_m
        east = np.array([origin[2], 0.0, -origin[0]])
        east /= np.linalg.norm(east) + 1e-12
        north = np.cross(origin, east)
        horizons = []
        best_peak = None
        for az in np.linspace(0, 2 * math.pi, 36, endpoint=False):
            direction = math.cos(az) * east + math.sin(az) * north
            blocked_at = 320.0
            max_ang = -1e9
            for dist_km in np.arange(8.0, 320.0, 8.0):
                ang = dist_km / R_KM
                p = origin * math.cos(ang) + direction * math.sin(ang)
                j = int(self.tree.query(p)[1])
                zj = max(0.0, float(self.z[j]))
                # elevation angle with curvature drop
                drop = (dist_km * 1000.0) ** 2 / (2 * R_KM * 1000.0)
                elev_ang = (zj - z0 - drop) / (dist_km * 1000.0)
                if elev_ang > max_ang:
                    max_ang = elev_ang
                    if zj > 1500 and (best_peak is None or zj > best_peak["elevation_m"]):
                        lo, la = self.cell_lonlat(j)
                        best_peak = {"elevation_m": round(zj, 0),
                                     "distance_km": float(dist_km),
                                     "lon": round(lo, 2), "lat": round(la, 2)}
                elif max_ang > 0.002 and elev_ang < max_ang - 0.004:
                    blocked_at = min(blocked_at, dist_km)
            horizons.append(blocked_at)
        return {"horizon_km_median": float(np.median(horizons)),
                "visible_peak": best_peak}
