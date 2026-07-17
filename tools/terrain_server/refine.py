"""Region refinement pyramid (plan M11.5): re-simulate a lat/lon window at
higher resolution, with boundary conditions taken from the global solution.

Contract:
  - the tile's 1-px border is LOCKED to the (bilinearly upsampled) global
    elevation, so tiles always join the global field continuously;
  - detail noise is world-anchored (value noise hashed on absolute lon/lat
    lattices), so overlapping or adjacent tiles agree on the added detail;
  - a short local pass of the analytical stream-power update (same formula
    as the C++ kernel: z = z0*lambda + (z_r + U*d/a)(1-lambda) on the
    filled-DEM receiver tree) carves drainage into the added detail while
    ocean cells and the border stay fixed.

Deterministic for (world, bbox, scale).
"""

from __future__ import annotations

import heapq
import math

import numpy as np

R_M = 6_371_000.0


def _hash_grid(seed: int, ix: np.ndarray, iy: np.ndarray) -> np.ndarray:
    x = (ix.astype(np.uint64) * np.uint64(0x9E3779B1)
         ^ iy.astype(np.uint64) * np.uint64(0x85EBCA77)
         ^ np.uint64(seed & 0xFFFFFFFF) * np.uint64(0xC2B2AE3D))
    x ^= x >> np.uint64(31)
    x *= np.uint64(0xBF58476D1CE4E5B9)  # wraps mod 2^64, deterministic
    x ^= x >> np.uint64(29)
    return (x & np.uint64((1 << 53) - 1)).astype(np.float64) / float(1 << 53)


def world_value_noise(lon: np.ndarray, lat: np.ndarray, freq: float,
                      seed: int) -> np.ndarray:
    """Smoothed value noise anchored on absolute lon/lat (deg) lattices —
    identical wherever it is evaluated, so tiles never crack."""
    x = lon * freq
    y = lat * freq
    ix = np.floor(x).astype(np.int64)
    iy = np.floor(y).astype(np.int64)
    fx = x - ix
    fy = y - iy
    sx = fx * fx * (3 - 2 * fx)
    sy = fy * fy * (3 - 2 * fy)
    v00 = _hash_grid(seed, ix, iy)
    v10 = _hash_grid(seed, ix + 1, iy)
    v01 = _hash_grid(seed, ix, iy + 1)
    v11 = _hash_grid(seed, ix + 1, iy + 1)
    a = v00 + (v10 - v00) * sx
    b = v01 + (v11 - v01) * sx
    return (a + (b - a) * sy) * 2.0 - 1.0


def _bilinear(raster: np.ndarray, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    h, w = raster.shape
    x0 = np.floor(u).astype(int)
    y0 = np.clip(np.floor(v).astype(int), 0, h - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    tx = u - np.floor(u)
    ty = np.clip(v - y0, 0.0, 1.0)
    x1 = (x0 + 1) % w
    x0 = x0 % w
    c0 = raster[y0, x0] + (raster[y0, x1] - raster[y0, x0]) * tx
    c1 = raster[y1, x0] + (raster[y1, x1] - raster[y1, x0]) * tx
    return c0 + (c1 - c0) * ty


def refine_tile(session, lon0: float, lat0: float, lon1: float, lat1: float,
                scale: int = 4, iterations: int = 3, max_px: int = 768,
                detail: bool = True) -> dict:
    result = session.result
    w, h = result["width"], result["height"]
    seed = int(session.params.get("world.seed", 1337))
    sea = float(session.params.get("hydrology.sea_level_m", 0.0))

    def layer(name):
        return np.frombuffer(result["layers"][name]["data"],
                             np.float32).reshape(h, w)

    base_name = ("elevation_conditioned_m"
                 if "elevation_conditioned_m" in result["layers"]
                 else "elevation_eroded_m")
    zg = layer(base_name)
    ug = layer("uplift_rate_m_per_yr")

    lon1 = lon0 + max(0.05, lon1 - lon0)
    lat1 = lat0 + max(0.05, lat1 - lat0)
    tw = min(max_px, max(16, int(round((lon1 - lon0) / 360.0 * w * scale))))
    th = min(max_px, max(16, int(round((lat1 - lat0) / 180.0 * h * scale))))

    lons = lon0 + (np.arange(tw) + 0.5) / tw * (lon1 - lon0)
    lats = lat1 - (np.arange(th) + 0.5) / th * (lat1 - lat0)
    lon_m, lat_m = np.meshgrid(lons, lats)
    u = (lon_m + 180.0) / 360.0 * w - 0.5
    v = (90.0 - lat_m) / 180.0 * h - 0.5

    z_up = _bilinear(zg, u, v)
    uplift = np.maximum(0.0, _bilinear(ug, u, v))

    z0 = z_up.copy()
    if detail:
        rel = np.clip((z_up - sea) / 2200.0, 0.0, 1.0)
        uf = np.clip(uplift / 2.0e-4, 0.0, 1.0)
        mness = np.minimum(1.0, 0.55 * rel + 0.6 * uf)
        # continue the detail spectrum below the global raster's resolution
        base_freq = w / 360.0 * 1.2  # cycles per degree, ~just past Nyquist
        amp = (18.0 + 160.0 * mness)
        noise = np.zeros_like(z_up)
        f, a = base_freq, 1.0
        for octave in range(4):
            noise += a * world_value_noise(lon_m, lat_m, f, seed + 71 * octave)
            f *= 2.0
            a *= 0.55
        z0 = z_up + amp * noise * (z_up > sea)
        z0[0, :] = z_up[0, :]   # the boundary condition is the global field,
        z0[-1, :] = z_up[-1, :]  # never noised
        z0[:, 0] = z_up[:, 0]
        z0[:, -1] = z_up[:, -1]

    # ---- local analytical erosion with locked boundary ----
    border = np.zeros((th, tw), bool)
    border[0, :] = border[-1, :] = True
    border[:, 0] = border[:, -1] = True
    oceanish = z_up <= sea
    locked = border | oceanish

    zt = z0.copy()
    if iterations > 0:
        px_m = (lon1 - lon0) / tw * math.pi / 180.0 * R_M * \
            max(0.2, math.cos(math.radians((lat0 + lat1) / 2)))
        py_m = (lat1 - lat0) / th * math.pi / 180.0 * R_M
        cell_area = px_m * py_m
        k_sp = float(session.params.get("erosion.k", 1.2e-6))
        m_sp = float(session.params.get("erosion.m", 0.45))
        dt = float(session.params.get("erosion.time_years", 2e6)) * 0.25

        offs = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
        dist = np.array([math.hypot(dy * py_m, dx * px_m)
                         for dy, dx in offs])

        idx = np.arange(th * tw).reshape(th, tw)
        for _ in range(iterations):
            # priority-flood fill from locked cells
            filled = zt.copy()
            visited = locked.copy()
            heap = [(float(filled[y, x]), int(y), int(x))
                    for y, x in zip(*np.nonzero(locked))]
            heapq.heapify(heap)
            fy = filled.ravel()
            while heap:
                fz, y, x = heapq.heappop(heap)
                for dy, dx in offs:
                    ny, nx = y + dy, x + dx
                    if ny < 0 or ny >= th or nx < 0 or nx >= tw or visited[ny, nx]:
                        continue
                    visited[ny, nx] = True
                    if filled[ny, nx] < fz:
                        filled[ny, nx] = fz
                    heapq.heappush(heap, (float(filled[ny, nx]), ny, nx))

            # D8 receivers: steepest descent on the filled surface. Cells
            # outside the tile read as +inf so edge rolls are never taken.
            recv = np.full(th * tw, -1, np.int64)
            rdist = np.ones(th * tw)
            best = np.zeros((th, tw))
            for k, (dy, dx) in enumerate(offs):
                nbz = np.full((th, tw), np.inf)
                ys = slice(max(0, dy), th + min(0, dy))
                xs = slice(max(0, dx), tw + min(0, dx))
                ys0 = slice(max(0, -dy), th + min(0, -dy))
                xs0 = slice(max(0, -dx), tw + min(0, -dx))
                nbz[ys0, xs0] = filled[ys, xs]
                slope = (filled - nbz) / dist[k]
                take = slope > best
                tgt = np.roll(np.roll(idx, -dy, 0), -dx, 1)
                recv = np.where(take.ravel(), tgt.ravel(), recv)
                rdist = np.where(take.ravel(), dist[k], rdist)
                best = np.where(take, slope, best)

            order = np.argsort(-filled.ravel(), kind="stable")
            area = np.full(th * tw, cell_area)
            rec = recv
            for i in order:
                r = rec[i]
                if r >= 0:
                    area[r] += area[i]

            lam_a = k_sp * np.maximum(1.0, area) ** m_sp
            zt_f = zt.ravel()
            z0_f = z0.ravel()
            up_f = uplift.ravel()
            lk = locked.ravel()
            z_new = zt_f.copy()
            for oi in range(th * tw - 1, -1, -1):
                i = order[oi]
                if lk[i]:
                    continue
                r = rec[i]
                if r < 0:
                    continue
                a = max(1e-10, lam_a[i])
                lam = math.exp(-a * dt / rdist[i])
                target = z_new[r] + up_f[i] * rdist[i] / a
                z_new[i] = z0_f[i] * lam + target * (1.0 - lam)
                if z0_f[i] > sea:
                    z_new[i] = max(z_new[i], sea)
            zt = z_new.reshape(th, tw)

    zt = np.where(locked, z_up, zt)
    return {"tile": zt.astype(np.float32), "width": tw, "height": th,
            "lon0": lon0, "lat0": lat0, "lon1": lon1, "lat1": lat1,
            "base_layer": base_name, "z_up": z_up.astype(np.float32)}
