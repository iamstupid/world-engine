"""Per-world sky model (plan M13 component): seeded star catalog with named
constellations, planet rotation/tilt/orbit, moons with phases, eclipse
scanning, and sky(observer, t) queries answering "what is in the sky
tonight" - navigation, omens, festivals, moonlight conditions.

Frames: inertial equatorial frame with Z = rotation axis. The ecliptic is
tilted by axial_tilt about X. Story time t is in days of the world calendar.
Deterministic for (seed, spec).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

TWO_PI = 2.0 * math.pi

PHASE_NAMES = ["new", "waxing crescent", "first quarter", "waxing gibbous",
               "full", "waning gibbous", "last quarter", "waning crescent"]

CON_SYLL_A = ["Ar", "Bel", "Cyg", "Dra", "El", "Fen", "Gal", "Hyr", "Ka", "Lum",
              "Mor", "Nys", "Or", "Pyx", "Rhi", "Sar", "Tal", "Ur", "Vel", "Zeph"]
CON_SYLL_B = ["a", "ion", "us", "ara", "eth", "or", "is", "ane", "yx", "iel"]
CON_KINDS = ["the Hunter", "the Serpent", "the Lantern", "the Twins",
             "the Plough", "the Crown", "the Ship", "the Wolf", "the Harp",
             "the Gate", "the Weaver", "the Anvil"]


@dataclass
class SkySpec:
    seed: int = 1337
    star_count: int = 1400
    constellation_count: int = 14
    rotation_hours: float = 24.0
    year_days: float = 360.0
    axial_tilt_deg: float = 23.0
    moons: list = field(default_factory=lambda: [
        {"name": "Moon", "period_days": 29.5, "phase0": 0.0,
         "inclination_deg": 5.0}])


class SkyModel:
    def __init__(self, spec: SkySpec):
        self.spec = spec
        rng = np.random.default_rng(spec.seed * 2654435761 % (2 ** 63))
        n = spec.star_count
        z = rng.uniform(-1, 1, n)
        phi = rng.uniform(0, TWO_PI, n)
        r = np.sqrt(1 - z * z)
        self.stars = np.stack([r * np.cos(phi), r * np.sin(phi), z], axis=1)
        # magnitudes: few bright, many dim (lower = brighter)
        self.mag = 1.0 + 5.0 * rng.power(2.5, n)

        # constellations: cluster around the brightest stars
        bright = np.argsort(self.mag)[: spec.constellation_count * 12]
        self.constellations = []
        used = set()
        for k in range(spec.constellation_count):
            anchor = None
            for cand in bright:
                if cand not in used:
                    anchor = int(cand)
                    break
            if anchor is None:
                break
            dots = self.stars @ self.stars[anchor]
            near = [int(i) for i in np.argsort(-dots)[:40]
                    if self.mag[i] < 5.0 and i not in used][:int(rng.integers(4, 8))]
            used.update(near)
            name = (CON_SYLL_A[int(rng.integers(0, len(CON_SYLL_A)))] +
                    CON_SYLL_B[int(rng.integers(0, len(CON_SYLL_B)))])
            title = CON_KINDS[int(rng.integers(0, len(CON_KINDS)))]
            self.constellations.append({
                "name": f"{name}, {title}", "stars": near,
                "centroid": (self.stars[near].mean(axis=0) /
                             np.linalg.norm(self.stars[near].mean(axis=0))).tolist()})

        eps = math.radians(spec.axial_tilt_deg)
        self._ecl = np.array([[1, 0, 0],
                              [0, math.cos(eps), -math.sin(eps)],
                              [0, math.sin(eps), math.cos(eps)]])

    # ---- celestial positions (inertial frame) ----

    def sun_dir(self, t: float) -> np.ndarray:
        lam = TWO_PI * (t / self.spec.year_days)
        return self._ecl @ np.array([math.cos(lam), math.sin(lam), 0.0])

    def moon_dir(self, t: float, moon: dict) -> np.ndarray:
        lam = TWO_PI * (t / moon["period_days"]) + moon.get("phase0", 0.0)
        inc = math.radians(moon.get("inclination_deg", 0.0))
        tilt = np.array([[1, 0, 0],
                         [0, math.cos(inc), -math.sin(inc)],
                         [0, math.sin(inc), math.cos(inc)]])
        return self._ecl @ (tilt @ np.array([math.cos(lam), math.sin(lam), 0.0]))

    # ---- observer frame ----

    def _observer_basis(self, lat_deg: float, lon_deg: float, t: float):
        theta = TWO_PI * (t * 24.0 / self.spec.rotation_hours) + math.radians(lon_deg)
        la = math.radians(lat_deg)
        zenith = np.array([math.cos(la) * math.cos(theta),
                           math.cos(la) * math.sin(theta), math.sin(la)])
        north = np.array([-math.sin(la) * math.cos(theta),
                          -math.sin(la) * math.sin(theta), math.cos(la)])
        east = np.cross(north, zenith)
        return zenith, north, east

    def alt_az(self, d: np.ndarray, lat_deg: float, lon_deg: float, t: float):
        zenith, north, east = self._observer_basis(lat_deg, lon_deg, t)
        alt = math.degrees(math.asin(max(-1.0, min(1.0, float(d @ zenith)))))
        az = math.degrees(math.atan2(float(d @ east), float(d @ north))) % 360.0
        return alt, az

    # ---- phases & eclipses ----

    def moon_phase(self, t: float, moon: dict) -> tuple[float, str]:
        s = self.sun_dir(t)
        m = self.moon_dir(t, moon)
        elong = math.acos(max(-1.0, min(1.0, float(s @ m))))
        frac = (1.0 - math.cos(elong)) / 2.0
        lam_s = math.atan2(float(s[1]), float(s[0]))
        lam_m = math.atan2(float(m[1]), float(m[0]))
        waxing = math.sin(lam_m - lam_s) > 0
        octant = int(((elong / math.pi) * 4.0) + 0.5)  # 0..4
        idx = octant if waxing else (8 - octant) % 8
        return frac, PHASE_NAMES[idx]

    def find_eclipses(self, t0: float, horizon_days: float, moon_idx: int = 0,
                      step: float = 0.05) -> list[dict]:
        moon = self.spec.moons[moon_idx]
        out = []
        t = t0
        while t < t0 + horizon_days:
            s = self.sun_dir(t)
            m = self.moon_dir(t, moon)
            elong = math.degrees(math.acos(max(-1.0, min(1.0, float(s @ m)))))
            if elong < 0.9:
                out.append({"type": "solar", "day": round(t, 2)})
                t += moon["period_days"] * 0.5
            elif elong > 179.1:
                out.append({"type": "lunar", "day": round(t, 2)})
                t += moon["period_days"] * 0.5
            else:
                t += step
        return out

    # ---- the main query ----

    def sky(self, lat_deg: float, lon_deg: float, t: float) -> dict:
        sun_alt, sun_az = self.alt_az(self.sun_dir(t), lat_deg, lon_deg, t)
        period = ("day" if sun_alt > 0 else
                  "twilight" if sun_alt > -8 else "night")
        moons = []
        moonlight = 0.0
        for moon in self.spec.moons:
            alt, az = self.alt_az(self.moon_dir(t, moon), lat_deg, lon_deg, t)
            frac, name = self.moon_phase(t, moon)
            moons.append({"name": moon["name"], "alt": round(alt, 1),
                          "az": round(az, 1), "phase": round(frac, 3),
                          "phase_name": name, "up": alt > 0})
            if alt > 0:
                moonlight = max(moonlight, frac)
        visible_cons = []
        if period == "night":
            for con in self.constellations:
                alt, az = self.alt_az(np.array(con["centroid"]), lat_deg, lon_deg, t)
                if alt > 12:
                    visible_cons.append({"name": con["name"], "alt": round(alt, 1),
                                         "az": round(az, 1)})
        return {"t": t, "period": period,
                "sun": {"alt": round(sun_alt, 1), "az": round(sun_az, 1)},
                "moons": moons, "moonlight": round(moonlight, 3),
                "constellations": visible_cons[:10]}
