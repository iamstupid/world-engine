"""Scene reference packs (RP-2, docs/RP_DESIGN.md §4): render the world's
answer to "standing here, now, what do I see?" — consumed three ways:
as machine vision reports injected into seat contexts, as composition
references for image generation, and as transcript metadata so novelize
gets the same environment the seats saw.

All pure computation on session rasters + the astro observatory; PNGs
are drawn with PIL (no matplotlib).
"""

from __future__ import annotations

import io
import math

import numpy as np

R_KM = 6371.0
DIRS = ["北", "东北偏北", "东北", "东北偏东", "东", "东南偏东", "东南",
        "东南偏南", "南", "西南偏南", "西南", "西南偏西", "西",
        "西北偏西", "西北", "西北偏北"]


def dir_name(az_deg: float) -> str:
    return DIRS[int(((az_deg % 360) + 11.25) // 22.5) % 16]


def _elev_raster(session):
    result = session.result
    name = ("elevation_conditioned_m"
            if "elevation_conditioned_m" in result["layers"]
            else "elevation_eroded_m")
    h, w = result["height"], result["width"]
    return (np.frombuffer(result["layers"][name]["data"], np.float32)
            .reshape(h, w), w, h)


def _sample(raster, w, h, lon, lat):
    u = (lon + 180.0) / 360.0 * w - 0.5
    v = (90.0 - lat) / 180.0 * h - 0.5
    x0 = int(np.floor(u)) % w
    y0 = min(h - 1, max(0, int(np.floor(v))))
    x1 = (x0 + 1) % w
    y1 = min(h - 1, y0 + 1)
    tx = u - np.floor(u)
    ty = min(1.0, max(0.0, v - y0))
    a = raster[y0, x0] * (1 - tx) + raster[y0, x1] * tx
    b = raster[y1, x0] * (1 - tx) + raster[y1, x1] * tx
    return float(a * (1 - ty) + b * ty)


# ------------------------------------------------------------- skyline ----

def skyline_profile(session, lon: float, lat: float, n_az: int = 36,
                    max_km: float = 220.0, step_km: float = 4.0) -> dict:
    """Horizon elevation-angle profile: for each azimuth, march along the
    ray sampling terrain, apply the earth-curvature drop, keep the max
    apparent angle. Observer eye 2 m above local ground (min sea level)."""
    z, w, h = _elev_raster(session)
    h0 = max(0.0, _sample(z, w, h, lon, lat)) + 2.0
    az_list = np.arange(n_az) * (360.0 / n_az)
    angles = np.full(n_az, -0.5)
    dists = np.zeros(n_az)
    heights = np.zeros(n_az)
    coslat = max(0.2, math.cos(math.radians(lat)))
    for k, az in enumerate(az_list):
        s, c = math.sin(math.radians(az)), math.cos(math.radians(az))
        best = -0.5
        for d in np.arange(step_km, max_km + 1e-9, step_km):
            plat = lat + (d * c) / 111.19
            plon = lon + (d * s) / (111.19 * coslat)
            ht = max(0.0, _sample(z, w, h, plon, plat))
            drop_m = (d * d) / (2.0 * R_KM) * 1000.0
            ang = math.degrees(math.atan2(ht - drop_m - h0, d * 1000.0))
            if ang > best:
                best = ang
                dists[k] = d
                heights[k] = ht
        angles[k] = best
    report = []
    thresh = 0.5
    k = 0
    while k < n_az:
        if angles[k] > thresh:
            j = k
            while j + 1 < n_az and angles[j + 1] > thresh:
                j += 1
            peak = int(k + np.argmax(angles[k:j + 1]))
            report.append(
                f"{dir_name(az_list[k])}到{dir_name(az_list[j])}方向："
                f"约{dists[peak]:.0f}公里外有海拔约{heights[peak]:.0f}米的"
                f"高地压住地平线（仰角{angles[peak]:.1f}°）")
            k = j + 1
        else:
            k += 1
    if not report:
        report.append("四望开阔，地平线上没有显著的山影")
    return {"az_deg": az_list.tolist(),
            "elev_angle_deg": np.round(angles, 2).tolist(),
            "dist_km": dists.tolist(), "report": report}


# ------------------------------------------------------------- minimap ----

def minimap_png(session, lon: float, lat: float, radius_km: float = 80.0,
                size: int = 384) -> bytes:
    from PIL import Image, ImageDraw
    z, w, h = _elev_raster(session)
    ocean = (np.frombuffer(session.result["layers"]["ocean_mask"]["data"],
                           np.uint8).reshape(h, w)
             if "ocean_mask" in session.result["layers"] else None)
    coslat = max(0.2, math.cos(math.radians(lat)))
    dlat = radius_km / 111.19
    dlon = radius_km / (111.19 * coslat)
    img = np.zeros((size, size, 3), np.uint8)
    lats = lat + dlat - (np.arange(size) + 0.5) / size * 2 * dlat
    lons = lon - dlon + (np.arange(size) + 0.5) / size * 2 * dlon
    for row in range(size):
        for col in range(size):
            zz = _sample(z, w, h, lons[col], lats[row])
            if zz <= 0:
                t = min(1.0, -zz / 4000.0)
                img[row, col] = (int(40 - 25 * t), int(90 - 60 * t),
                                 int(160 - 70 * t))
            else:
                t = min(1.0, zz / 2500.0)
                if t < 0.4:
                    u = t / 0.4
                    img[row, col] = (int(70 + 90 * u), int(130 + 30 * u),
                                     int(60 + 20 * u))
                else:
                    u = (t - 0.4) / 0.6
                    img[row, col] = (int(160 + 80 * u), int(160 + 60 * u),
                                     int(80 + 150 * u))
    pil = Image.fromarray(img)
    draw = ImageDraw.Draw(pil)
    for f in getattr(session, "features", []):
        if f.get("kind") != "settlement":
            continue
        c = f["geometry"]["coordinates"]
        px = (c[0] - (lon - dlon)) / (2 * dlon) * size
        py = ((lat + dlat) - c[1]) / (2 * dlat) * size
        if 0 <= px < size and 0 <= py < size:
            draw.ellipse([px - 3, py - 3, px + 3, py + 3],
                         fill=(255, 60, 60), outline=(255, 255, 255))
    cx = cy = size / 2
    draw.line([cx - 6, cy, cx + 6, cy], fill=(255, 255, 0), width=2)
    draw.line([cx, cy - 6, cx, cy + 6], fill=(255, 255, 0), width=2)
    buf = io.BytesIO()
    pil.save(buf, "PNG")
    return buf.getvalue()


# ------------------------------------------------------------- skydome ----

def skydome_png(sky: dict, size: int = 420) -> bytes:
    """Polar all-sky chart from an Observatory.sky() dict: zenith at the
    center, horizon at the rim, stars with their blackbody colors."""
    from PIL import Image, ImageDraw
    night = sky["period"] == "night"
    bg = (8, 10, 24) if night else (110, 150, 200)
    pil = Image.new("RGB", (size, size), bg)
    draw = ImageDraw.Draw(pil)
    cx = cy = size / 2
    rad = size / 2 - 6

    def to_xy(alt, az):
        rr = (90.0 - alt) / 90.0 * rad
        a = math.radians(az)
        return cx + rr * math.sin(a), cy - rr * math.cos(a)

    draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad],
                 outline=(90, 90, 110))
    for s in sky.get("stars", []):
        if s["alt"] <= 0:
            continue
        x, y = to_xy(s["alt"], s["az"])
        r = max(0.8, 2.8 - 0.35 * s["mag"])
        col = tuple(s.get("rgb", [255, 255, 255]))
        draw.ellipse([x - r, y - r, x + r, y + r], fill=col)
    for p in sky.get("planets", []):
        if p.get("up"):
            x, y = to_xy(p["alt"], p["az"])
            draw.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5],
                         fill=(255, 240, 200), outline=(255, 255, 255))
    for m in sky.get("moons", []):
        if m["alt"] > 0:
            x, y = to_xy(m["alt"], m["az"])
            r = 9
            shade = int(60 + 195 * m["phase"])
            draw.ellipse([x - r, y - r, x + r, y + r],
                         fill=(shade, shade, min(255, shade + 10)),
                         outline=(200, 200, 210))
    if sky["sun"]["alt"] > -1:
        x, y = to_xy(max(0.5, sky["sun"]["alt"]), sky["sun"]["az"])
        draw.ellipse([x - 11, y - 11, x + 11, y + 11], fill=(255, 244, 190))
    comp = sky.get("companion_star")
    if comp and comp["alt"] > 0:
        x, y = to_xy(comp["alt"], comp["az"])
        draw.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(200, 60, 40))
    buf = io.BytesIO()
    pil.save(buf, "PNG")
    return buf.getvalue()


# ------------------------------------------------------------- lighting ---

def lighting_brief(sky: dict) -> dict:
    period_cn = {"day": "白昼", "twilight": "晨昏", "night": "夜晚"}[sky["period"]]
    moon_bits = []
    for m in sky.get("moons", []):
        state = "在天" if m["alt"] > 0 else "未升"
        moon_bits.append(f"{m['name']}{m['phase_name']}(照明{m['phase']:.2f}，{state})")
    ml = sky.get("moonlight", 0.0)
    if sky["period"] == "night":
        light = ("月色明亮，地面有影" if ml > 0.6 else
                 "月色朦胧" if ml > 0.25 else
                 "近乎无月，仅有星光")
    else:
        light = f"太阳高度{sky['sun']['alt']:.0f}°"
    comp = sky.get("companion_star")
    comp_txt = (f"暗红伴日{'在天' if comp['up'] else '在地平线下'}"
                if comp else "无伴星")
    return {"period": sky["period"],
            "报告": f"{period_cn}；{light}；{'；'.join(moon_bits)}；{comp_txt}"}


# ----------------------------------------------------------------- pack ---

def scene_refpack(session, obs, lon: float, lat: float, t: float,
                  with_png: bool = False) -> dict:
    """Text-first reference pack (PNGs optional so transcripts stay JSON)."""
    pack = {}
    skyline = skyline_profile(session, lon, lat)
    pack["skyline_report"] = skyline["report"]
    pack["skyline"] = {"az_deg": skyline["az_deg"],
                       "elev_angle_deg": skyline["elev_angle_deg"]}
    if obs is not None:
        sky = obs.sky(lat, lon, t + 0.75)
        pack["lighting"] = lighting_brief(sky)
        if with_png:
            pack["skydome_png"] = skydome_png(sky)
    if with_png:
        pack["minimap_png"] = minimap_png(session, lon, lat)
    return pack
