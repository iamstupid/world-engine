"""Full astronomy model (absorbs skymodel v1): a seeded galaxy of star
systems with real 3D positions, mass-derived stellar physics (luminosity,
temperature, radius, magnitudes, blackbody colors), Kepler orbits for the
home system's planets, moons with phases and eclipses, and observer-aware
sky queries — constellations are defined by the home culture but their
shapes distort when viewed from another star system (cross-system parallax
for interstellar fiction).

Units: pc between systems, AU inside a system, days for time, solar units
for mass/luminosity/radius.  Frames:
  - "galactic": the fixed frame galaxy positions are generated in.
  - "equatorial": home planet frame, Z = spin axis; star directions are
    rotated galactic->equatorial by a seeded constant rotation, and the
    ecliptic (home orbit plane) is tilted by the axial tilt about X.
Story time t is in days of the world calendar.  Deterministic for (spec).
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

TWO_PI = 2.0 * math.pi
AU_PER_PC = 206264.806
R_EARTH_AU = 4.26352e-5
SUN_APP_MAG_1AU = -26.832  # bolometric zero point: L=1 seen from 1 AU

PHASE_NAMES = ["new", "waxing crescent", "first quarter", "waxing gibbous",
               "full", "waning gibbous", "last quarter", "waning crescent"]

CON_SYLL_A = ["Ar", "Bel", "Cyg", "Dra", "El", "Fen", "Gal", "Hyr", "Ka", "Lum",
              "Mor", "Nys", "Or", "Pyx", "Rhi", "Sar", "Tal", "Ur", "Vel", "Zeph"]
CON_SYLL_B = ["a", "ion", "us", "ara", "eth", "or", "is", "ane", "yx", "iel"]
CON_KINDS = ["the Hunter", "the Serpent", "the Lantern", "the Twins",
             "the Plough", "the Crown", "the Ship", "the Wolf", "the Harp",
             "the Gate", "the Weaver", "the Anvil"]

STAR_SYLL_A = ["Al", "Be", "Ce", "Da", "El", "Fo", "Gie", "Ha", "Iz", "Ka",
               "Lu", "Mi", "Na", "Or", "Pol", "Ri", "Sa", "Tha", "Ve", "Za"]
STAR_SYLL_B = ["nar", "bel", "phar", "rix", "dun", "lek", "mir", "zan", "tor",
               "vell", "sha", "dral", "gol", "wen", "keth", "lios"]

PLANET_SYLL = ["Ar", "Bor", "Cal", "Dun", "Er", "Fal", "Gor", "Hel", "Ith",
               "Jor", "Kel", "Lor", "Mar", "Nim", "Oth", "Per", "Quel", "Ras",
               "Sel", "Tor", "Umb", "Vor", "Wyn", "Xan", "Yor", "Zel"]


# ---------------------------------------------------------------- kepler ----

def solve_kepler(mean_anom: float, e: float, tol: float = 1e-12) -> float:
    """Eccentric anomaly E with E - e*sin(E) = M (Newton, robust to e~0.95)."""
    m = math.fmod(mean_anom, TWO_PI)
    ecc = min(max(e, 0.0), 0.97)
    big_e = m if ecc < 0.8 else math.pi
    for _ in range(48):
        f = big_e - ecc * math.sin(big_e) - m
        big_e -= f / (1.0 - ecc * math.cos(big_e))
        if abs(f) < tol:
            break
    return big_e


def rot_z(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]])


def rot_x(a: float) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[1.0, 0.0, 0.0], [0.0, c, -s], [0.0, s, c]])


def orbit_matrix(inc: float, big_omega: float, small_omega: float) -> np.ndarray:
    return rot_z(big_omega) @ rot_x(inc) @ rot_z(small_omega)


def orbit_pos(planet: dict, t: float) -> np.ndarray:
    """Position (AU) in the ecliptic frame at day t for precomputed elements."""
    m = planet["M0"] + TWO_PI * (t / planet["period_days"])
    big_e = solve_kepler(m, planet["e"])
    a, e = planet["a"], planet["e"]
    xy = np.array([a * (math.cos(big_e) - e),
                   a * math.sqrt(1.0 - e * e) * math.sin(big_e), 0.0])
    return planet["rot"] @ xy


# --------------------------------------------------------- star physics ----

def sample_imf(rng: np.random.Generator, n: int,
               m_min: float = 0.08, m_max: float = 10.0) -> np.ndarray:
    """Kroupa broken power law: alpha=1.3 on [0.08,0.5], 2.3 above."""
    brk = 0.5
    lo, hi = max(m_min, 0.08), max(m_max, 0.6)

    def seg_number(a, b, alpha, k):
        return k * (a ** (1 - alpha) - b ** (1 - alpha)) / (alpha - 1)

    k2 = brk ** (2.3 - 1.3)  # continuity at the break
    w1 = seg_number(lo, brk, 1.3, 1.0) if lo < brk else 0.0
    w2 = seg_number(max(lo, brk), hi, 2.3, k2)
    u = rng.random(n)
    pick2 = u < (w2 / (w1 + w2))
    out = np.empty(n)
    v = rng.random(n)

    def inv_cdf(v, a, b, alpha):
        p = 1.0 - alpha
        return (a ** p + v * (b ** p - a ** p)) ** (1.0 / p)

    if lo < brk:
        out[~pick2] = inv_cdf(v[~pick2], lo, brk, 1.3)
    out[pick2] = inv_cdf(v[pick2], max(lo, brk), hi, 2.3)
    return out


def mass_luminosity(m: np.ndarray) -> np.ndarray:
    m = np.asarray(m, dtype=float)
    return np.where(m < 0.43, 0.23 * m ** 2.3,
                    np.where(m < 2.0, m ** 4.0, 1.4 * m ** 3.5))


def mass_radius(m: np.ndarray) -> np.ndarray:
    m = np.asarray(m, dtype=float)
    return np.where(m < 1.0, m ** 0.9, m ** 0.6)


def effective_temp(lum: np.ndarray, rad: np.ndarray) -> np.ndarray:
    return 5772.0 * (np.asarray(lum) / np.asarray(rad) ** 2) ** 0.25


def bolometric_correction(t_eff: np.ndarray) -> np.ndarray:
    """Reed (1998)-style polynomial in log10(T)-4; clamped, visual band."""
    x = np.log10(np.clip(np.asarray(t_eff, dtype=float), 2000.0, 45000.0)) - 4.0
    bc = (-8.499 * x ** 4 + 13.421 * x ** 3 - 8.131 * x ** 2
          - 3.901 * x - 0.438)
    return np.clip(bc, -4.5, 0.0)


SPECTRAL_EDGES = [(30000, "O"), (10000, "B"), (7500, "A"), (6000, "F"),
                  (5200, "G"), (3700, "K"), (0, "M")]


def spectral_class(t_eff: float) -> str:
    for edge, cls in SPECTRAL_EDGES:
        if t_eff >= edge:
            return cls
    return "M"


_LUT_CACHE: dict | None = None


def _lut() -> dict:
    global _LUT_CACHE
    if _LUT_CACHE is None:
        path = Path(__file__).resolve().parent / "data" / "blackbody_lut.json"
        _LUT_CACHE = json.loads(path.read_text(encoding="utf-8"))
        _LUT_CACHE["srgb"] = np.asarray(_LUT_CACHE["srgb"], dtype=np.int32)
    return _LUT_CACHE


def blackbody_rgb(t_eff) -> np.ndarray:
    """(n,3) or (3,) uint8-range sRGB tint for effective temperature(s)."""
    lut = _lut()
    t = np.clip(np.asarray(t_eff, dtype=float), lut["t_min"], lut["t_max"])
    pos = (np.log(t / lut["t_min"]) / np.log(lut["t_max"] / lut["t_min"])
           * (lut["n"] - 1))
    i0 = np.clip(pos.astype(int), 0, lut["n"] - 2)
    w = (pos - i0)[..., None]
    rows = lut["srgb"]
    return np.rint(rows[i0] * (1 - w) + rows[i0 + 1] * w).astype(int)


# ------------------------------------------------------ galaxy generators ---

def gen_spiral(rng: np.random.Generator, n: int) -> tuple[np.ndarray, dict]:
    r_disc = 2600.0
    arms = int(rng.integers(2, 5))
    pitch = math.radians(float(rng.uniform(11.0, 16.0)))
    n_b, n_h = int(0.20 * n), int(0.14 * n)
    n_d = n - n_b - n_h
    bulge = rng.normal(0, 1, (n_b, 3)) * np.array([420.0, 420.0, 300.0])
    r_d = (r_disc / 3.2) * rng.gamma(2.0, 1.0, n_d)
    theta = rng.uniform(0, TWO_PI, n_d)
    on_arm = rng.random(n_d) < 0.62
    k = rng.integers(0, arms, n_d)
    theta_arm = (np.log(np.maximum(r_d, 40.0) / 140.0) / math.tan(pitch)
                 + k * TWO_PI / arms)
    theta = np.where(on_arm,
                     theta_arm + rng.normal(0, 1, n_d) *
                     (0.10 + 0.05 * r_d / r_disc), theta)
    z_d = rng.normal(0, 1, n_d) * 42.0 * (0.6 + r_d / r_disc)
    disc = np.stack([r_d * np.cos(theta), r_d * np.sin(theta), z_d], axis=1)
    r_h = 400.0 * np.exp(rng.random(n_h) * math.log(9.0))
    dirs = rng.normal(0, 1, (n_h, 3))
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    halo = dirs * r_h[:, None]
    r_home = 0.55 * r_disc
    th_home = math.log(r_home / 140.0) / math.tan(pitch)
    home = np.array([r_home * math.cos(th_home),
                     r_home * math.sin(th_home), 10.0])
    return np.vstack([bulge, disc, halo]), {"kind": "spiral", "arms": arms,
                                            "pitch_deg": math.degrees(pitch),
                                            "r_disc": r_disc, "home": home}


def gen_cluster(rng: np.random.Generator, n: int) -> tuple[np.ndarray, dict]:
    r_c, r_t = 1.8, 44.0
    grid = np.linspace(1e-3, r_t, 4096)
    base = 1.0 / math.sqrt(1.0 + (r_t / r_c) ** 2)
    dens = (1.0 / np.sqrt(1.0 + (grid / r_c) ** 2) - base) ** 2
    w = grid * grid * dens
    w_max = float(w.max()) * 1.05
    out = np.empty(n)
    got = 0
    while got < n:
        cand = rng.uniform(1e-3, r_t, 4 * (n - got))
        d = (1.0 / np.sqrt(1.0 + (cand / r_c) ** 2) - base) ** 2
        keep = cand[rng.uniform(0, w_max, cand.size) < cand * cand * d]
        take = min(keep.size, n - got)
        out[got:got + take] = keep[:take]
        got += take
    dirs = rng.normal(0, 1, (n, 3))
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    return dirs * out[:, None], {"kind": "cluster", "r_core": r_c,
                                 "r_tidal": r_t,
                                 "home": np.array([2.5 * r_c, 0.0, 0.0])}


def gen_irregular(rng: np.random.Generator, n: int) -> tuple[np.ndarray, dict]:
    blobs = int(rng.integers(3, 6))
    centers = rng.uniform(-450, 450, (blobs, 3)) * np.array([1, 1, 0.4])
    sigma = rng.uniform(90, 260, blobs)
    which = rng.integers(0, blobs, n)
    pos = centers[which] + rng.normal(0, 1, (n, 3)) * sigma[which, None]
    return pos, {"kind": "irregular", "blobs": blobs,
                 "home": centers[0] + np.array([sigma[0] * 0.5, 0.0, 0.0])}


GALAXY_GENERATORS = {"spiral": gen_spiral, "cluster": gen_cluster,
                     "irregular": gen_irregular}


# ----------------------------------------------------------------- spec -----

@dataclass
class UniverseSpec:
    seed: int = 1337
    galaxy_type: str = "spiral"
    n_field: int = 2600            # luminous skeleton across the galaxy
    n_local: int = 2400            # full-IMF neighborhood around home
    local_radius_pc: float = 70.0
    year_days: float = 360.0
    rotation_hours: float = 24.0
    axial_tilt_deg: float = 23.0
    mag_limit: float = 6.5
    constellation_count: int = 14
    home_star_mass: float = 1.0
    binary_fraction: float = 0.22
    named_star_count: int = 160
    moons: list = field(default_factory=lambda: [
        {"name": "Moon", "period_days": 29.5, "phase0": 0.0,
         "inclination_deg": 5.0}])

    @staticmethod
    def from_overrides(seed: int, year_days: float,
                       overrides: dict | None = None) -> "UniverseSpec":
        spec = UniverseSpec(seed=int(seed), year_days=float(year_days))
        for key, value in (overrides or {}).items():
            if hasattr(spec, key):
                setattr(spec, key, value)
        return spec


# -------------------------------------------------------------- universe ----

class Universe:
    """System index 0 is always the home system (the generated world)."""

    def __init__(self, spec: UniverseSpec):
        self.spec = spec
        rng = np.random.default_rng((spec.seed * 2654435761 + 17) % (2 ** 63))
        gen = GALAXY_GENERATORS.get(spec.galaxy_type, gen_spiral)
        field_pos, self.galaxy_meta = gen(rng, spec.n_field)
        home_pos = np.asarray(self.galaxy_meta.pop("home"), dtype=float)

        dirs = rng.normal(0, 1, (spec.n_local, 3))
        dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
        radii = spec.local_radius_pc * rng.random(spec.n_local) ** (1.0 / 3.0)
        local_pos = home_pos + dirs * radii[:, None]

        self.sys_pos = np.vstack([home_pos[None, :], local_pos, field_pos])
        n = self.sys_pos.shape[0]
        self.n_systems = n
        self.tier = np.full(n, 2, dtype=np.uint8)     # 0 home / 1 local / 2 field
        self.tier[0] = 0
        self.tier[1:1 + spec.n_local] = 1

        mass = np.empty(n)
        mass[0] = spec.home_star_mass
        mass[1:1 + spec.n_local] = sample_imf(rng, spec.n_local, 0.08, 10.0)
        mass[1 + spec.n_local:] = sample_imf(rng, spec.n_field, 0.9, 25.0)
        comp = np.where(rng.random(n) < spec.binary_fraction,
                        mass * rng.uniform(0.15, 0.95, n), 0.0)
        comp[0] = 0.0  # home stays single (v1.5)
        self.mass, self.mass2 = mass, comp
        lum = mass_luminosity(mass) + np.where(comp > 0,
                                               mass_luminosity(np.maximum(comp, 0.081)), 0.0)
        rad = mass_radius(mass)
        self.lum, self.rad = lum, rad
        self.t_eff = effective_temp(mass_luminosity(mass), rad)
        m_bol = 4.74 - 2.5 * np.log10(np.maximum(lum, 1e-9))
        self.mv = m_bol - bolometric_correction(self.t_eff)
        self.rgb = blackbody_rgb(self.t_eff)
        self.cls = [spectral_class(float(t)) for t in self.t_eff]

        # galactic -> home-equatorial: seeded constant rotation
        axis = rng.normal(0, 1, 3)
        axis /= np.linalg.norm(axis)
        ang = rng.uniform(0, TWO_PI)
        kx, ky, kz = axis
        k_mat = np.array([[0, -kz, ky], [kz, 0, -kx], [-ky, kx, 0]])
        self.gal2eq = (np.eye(3) + math.sin(ang) * k_mat
                       + (1 - math.cos(ang)) * (k_mat @ k_mat))

        self._build_home_system(rng)
        self.names: dict[int, str] = {}
        self._catalog_cache: dict[int, dict] = {}
        self._assign_names(rng)
        self._build_constellations(rng)

    # ---- home system ----

    def _build_home_system(self, rng: np.random.Generator):
        spec = self.spec
        lum = float(self.lum[0])
        mass = float(self.mass[0])
        a_home = (mass * (spec.year_days / 365.25) ** 2) ** (1.0 / 3.0)
        snow = 2.7 * math.sqrt(lum)
        ratio = float(rng.uniform(1.55, 1.85))
        n_in, n_out = int(rng.integers(1, 4)), int(rng.integers(2, 5))
        semis = ([a_home / ratio ** (i + 1) for i in range(n_in)][::-1]
                 + [a_home] + [a_home * ratio ** (i + 1) for i in range(n_out)])
        self.planets = []
        used = set()
        for a in semis:
            is_home = abs(a - a_home) < 1e-12
            giant = a > snow and not is_home
            e = 0.0 if is_home else float(rng.uniform(0.0, 0.07))
            inc = 0.0 if is_home else math.radians(float(rng.uniform(0.0, 3.0)))
            big_om = 0.0 if is_home else float(rng.uniform(0, TWO_PI))
            small_om = 0.0 if is_home else float(rng.uniform(0, TWO_PI))
            while True:
                name = (PLANET_SYLL[int(rng.integers(0, len(PLANET_SYLL)))]
                        + STAR_SYLL_B[int(rng.integers(0, len(STAR_SYLL_B)))])
                if name not in used:
                    used.add(name)
                    break
            planet = {
                "name": "Homeworld" if is_home else name,
                "a": a, "e": e, "inc_deg": math.degrees(inc),
                "M0": 0.0 if is_home else float(rng.uniform(0, TWO_PI)),
                "period_days": spec.year_days * (a / a_home) ** 1.5,
                "rot": orbit_matrix(inc, big_om, small_om),
                "type": ("giant" if giant else "rocky"),
                "radius_earth": (float(rng.uniform(6.0, 13.0)) if giant
                                 else float(rng.uniform(0.4, 1.6))),
                "albedo": (float(rng.uniform(0.4, 0.55)) if giant
                           else float(rng.uniform(0.2, 0.5))),
                "is_home": is_home,
                "moon_count": (int(rng.integers(1, 5)) if giant else
                               (len(spec.moons) if is_home else 0)),
            }
            if is_home:
                planet["radius_earth"] = 1.0
            self.planets.append(planet)
        self.home_planet_idx = next(i for i, p in enumerate(self.planets)
                                    if p["is_home"])

    # ---- catalogs ----

    def apparent_mag(self, ids: np.ndarray, observer: int = 0) -> np.ndarray:
        d = np.linalg.norm(self.sys_pos[ids] - self.sys_pos[observer], axis=-1)
        return self.mv[ids] + 5.0 * (np.log10(np.maximum(d, 1e-6)) - 1.0)

    def naked_eye(self, observer: int = 0) -> dict:
        """Stars visible (mag < limit) from a system.  Directions are unit
        vectors in the GALACTIC frame (rotation-invariant comparisons)."""
        if observer in self._catalog_cache:
            return self._catalog_cache[observer]
        rel = self.sys_pos - self.sys_pos[observer]
        d = np.linalg.norm(rel, axis=1)
        d[observer] = np.inf
        mag = self.mv + 5.0 * (np.log10(np.maximum(d, 1e-6)) - 1.0)
        ids = np.nonzero(mag < self.spec.mag_limit)[0]
        order = ids[np.argsort(mag[ids])]
        cat = {"ids": order,
               "dirs": rel[order] / d[order, None],
               "dist_pc": d[order],
               "mag": mag[order]}
        self._catalog_cache[observer] = cat
        return cat

    def _assign_names(self, rng: np.random.Generator):
        cat = self.naked_eye(0)
        used = set()
        for sid in cat["ids"][: self.spec.named_star_count]:
            for _ in range(40):
                name = (STAR_SYLL_A[int(rng.integers(0, len(STAR_SYLL_A)))]
                        + STAR_SYLL_B[int(rng.integers(0, len(STAR_SYLL_B)))])
                if name not in used:
                    used.add(name)
                    self.names[int(sid)] = name
                    break

    def _build_constellations(self, rng: np.random.Generator):
        """Membership fixed by home-sky culture; shapes recomputed per
        observer.  Edges = MST over angular distance (Prim)."""
        cat = self.naked_eye(0)
        dirs, ids = cat["dirs"], cat["ids"]
        self.constellations = []
        used: set[int] = set()
        for rank in range(len(ids)):
            if len(self.constellations) >= self.spec.constellation_count:
                break
            if rank in used:
                continue
            dots = dirs @ dirs[rank]
            near = [int(i) for i in np.argsort(-dots)
                    if i not in used and float(dots[i]) > math.cos(0.42)]
            take = near[: int(rng.integers(4, 9))]
            if len(take) < 4:
                continue
            used.update(take)
            member_dirs = dirs[take]
            in_tree, edges = {0}, []
            while len(in_tree) < len(take):
                best = None
                for i in in_tree:
                    for j in range(len(take)):
                        if j in in_tree:
                            continue
                        cosd = float(member_dirs[i] @ member_dirs[j])
                        if best is None or cosd > best[0]:
                            best = (cosd, i, j)
                in_tree.add(best[2])
                edges.append((best[1], best[2]))
            centroid = member_dirs.mean(axis=0)
            centroid /= np.linalg.norm(centroid)
            name = (CON_SYLL_A[int(rng.integers(0, len(CON_SYLL_A)))]
                    + CON_SYLL_B[int(rng.integers(0, len(CON_SYLL_B)))])
            title = CON_KINDS[int(rng.integers(0, len(CON_KINDS)))]
            self.constellations.append({
                "name": f"{name}, {title}",
                "star_ids": [int(ids[i]) for i in take],
                "edges": edges,
                "centroid_gal": centroid.tolist()})

    # ---- JSON views ----

    def sky_from(self, observer: int = 0, limit: int = 900) -> dict:
        """Naked-eye chart from any system (galactic-frame unit dirs) with
        home-culture constellation shapes recomputed for that observer."""
        cat = self.naked_eye(observer)
        take = min(limit, len(cat["ids"]))
        stars = [{"sys": int(s), "name": self.names.get(int(s)),
                  "dir": [round(float(v), 5) for v in cat["dirs"][k]],
                  "mag": round(float(cat["mag"][k]), 2),
                  "dist_pc": round(float(cat["dist_pc"][k]), 2),
                  "rgb": [int(v) for v in self.rgb[int(s)]]}
                 for k, s in enumerate(cat["ids"][:take])]
        obs_pos = self.sys_pos[observer]
        cons = []
        for con in self.constellations:
            rel = self.sys_pos[con["star_ids"]] - obs_pos
            dist = np.linalg.norm(rel, axis=1)
            if np.any(dist < 1e-6):
                continue  # observer sits on a member star
            cons.append({"name": con["name"], "star_ids": con["star_ids"],
                         "dirs": np.round(rel / dist[:, None], 5).tolist(),
                         "edges": con["edges"]})
        return {"observer": int(observer), "frame": "galactic",
                "mag_limit": self.spec.mag_limit, "stars": stars,
                "constellations": cons}

    def galaxy_map(self) -> dict:
        mag_home = self.apparent_mag(np.arange(self.n_systems))
        systems = [{"id": i,
                    "pos": [round(float(v), 2) for v in self.sys_pos[i]],
                    "mv": round(float(self.mv[i]), 2),
                    "mag": round(float(mag_home[i]), 2),
                    "cls": self.cls[i],
                    "rgb": [int(v) for v in self.rgb[i]],
                    "tier": int(self.tier[i]),
                    "name": self.names.get(i)}
                   for i in range(self.n_systems)]
        return {"meta": self.galaxy_meta, "home": 0, "unit": "pc",
                "systems": systems}

    def system_info(self, idx: int) -> dict:
        idx = int(idx)
        out = {"id": idx, "name": self.names.get(idx),
               "pos_pc": [round(float(v), 3) for v in self.sys_pos[idx]],
               "stars": [{"mass": round(float(self.mass[idx]), 3),
                          "lum": round(float(self.lum[idx]), 4),
                          "radius": round(float(self.rad[idx]), 3),
                          "t_eff": round(float(self.t_eff[idx]), 0),
                          "cls": self.cls[idx],
                          "mv": round(float(self.mv[idx]), 2),
                          "rgb": [int(v) for v in self.rgb[idx]]}]}
        if self.mass2[idx] > 0:
            out["stars"].append({"mass": round(float(self.mass2[idx]), 3),
                                 "companion": True})
        if idx == 0:
            out["planets"] = [
                {k: (round(v, 5) if isinstance(v, float) else v)
                 for k, v in p.items() if k != "rot"}
                for p in self.planets]
            out["moons"] = self.spec.moons
        return out

    def store_records(self) -> list[dict]:
        """Entity records for WorldStore.apply_generation (kinds:
        star_system / planet / moon) — user renames survive regeneration."""
        recs = []
        cat = self.naked_eye(0)
        for k, sid in enumerate(cat["ids"]):
            sid = int(sid)
            if sid not in self.names:
                continue
            recs.append({"kind": "star_system", "gen_key": sid,
                         "attrs": {"name": self.names[sid],
                                   "cls": self.cls[sid],
                                   "dist_pc": round(float(cat["dist_pc"][k]), 2),
                                   "app_mag": round(float(cat["mag"][k]), 2)}})
        for j, p in enumerate(self.planets):
            recs.append({"kind": "planet", "gen_key": f"0:{j}",
                         "attrs": {"name": p["name"], "type": p["type"],
                                   "a_au": round(p["a"], 4),
                                   "period_days": round(p["period_days"], 2),
                                   "is_home": p["is_home"]}})
        for k, moon in enumerate(self.spec.moons):
            recs.append({"kind": "moon", "gen_key": f"0:home:{k}",
                         "attrs": {"name": moon.get("name", f"Moon {k+1}"),
                                   "period_days": moon["period_days"]}})
        return recs


# ------------------------------------------------------------ observatory ---

class Observatory:
    """Sky queries for an observer on the home planet (lat/lon/t) — the
    v1 skymodel API absorbed: sun/moons/phases/eclipses plus real planets
    and the naked-eye star catalog."""

    def __init__(self, universe: Universe):
        self.u = universe
        self.spec = universe.spec
        eps = math.radians(self.spec.axial_tilt_deg)
        self._ecl = rot_x(eps)                       # ecliptic -> equatorial
        self._eq_dirs = None                         # lazy star cache

    # ---- celestial positions (equatorial frame unit vectors) ----

    def _home_pos(self, t: float) -> np.ndarray:
        return orbit_pos(self.u.planets[self.u.home_planet_idx], t)

    def sun_dir(self, t: float) -> np.ndarray:
        p = self._home_pos(t)
        return self._ecl @ (-p / np.linalg.norm(p))

    def moon_dir(self, t: float, moon: dict) -> np.ndarray:
        lam = TWO_PI * (t / moon["period_days"]) + moon.get("phase0", 0.0)
        inc = math.radians(moon.get("inclination_deg", 0.0))
        vec = rot_x(inc) @ np.array([math.cos(lam), math.sin(lam), 0.0])
        return self._ecl @ vec

    def planet_dir_mag(self, j: int, t: float) -> tuple[np.ndarray, float, float]:
        """Direction (equatorial), apparent magnitude, phase angle (deg)."""
        planet = self.u.planets[j]
        rp = orbit_pos(planet, t)
        rh = self._home_pos(t)
        rel = rp - rh
        d = float(np.linalg.norm(rel))
        r_star = float(np.linalg.norm(rp))
        to_star = -rp / r_star
        to_obs = -rel / d
        cos_chi = float(np.clip(to_star @ to_obs, -1.0, 1.0))
        chi = math.acos(cos_chi)
        lambert = (math.sin(chi) + (math.pi - chi) * math.cos(chi)) / math.pi
        r_au = planet["radius_earth"] * R_EARTH_AU
        flux = (float(self.u.lum[0]) / r_star ** 2 * planet["albedo"]
                * (r_au / d) ** 2 * (2.0 / 3.0) * max(lambert, 1e-9))
        mag = SUN_APP_MAG_1AU - 2.5 * math.log10(max(flux, 1e-30))
        return self._ecl @ (rel / d), mag, math.degrees(chi)

    def star_dirs_eq(self) -> tuple[dict, np.ndarray]:
        if self._eq_dirs is None:
            cat = self.u.naked_eye(0)
            self._eq_dirs = cat["dirs"] @ self.u.gal2eq.T
        return self.u.naked_eye(0), self._eq_dirs

    # ---- observer frame ----

    def _observer_basis(self, lat_deg: float, lon_deg: float, t: float):
        theta = (TWO_PI * (t * 24.0 / self.spec.rotation_hours)
                 + math.radians(lon_deg))
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

    # ---- phases & eclipses (v1 semantics preserved) ----

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

    def conjunctions(self, t0: float, horizon_days: float,
                     sep_deg: float = 1.5, step: float = 0.5) -> list[dict]:
        """Geocentric planet-planet conjunctions (angular separation)."""
        others = [j for j in range(len(self.u.planets))
                  if j != self.u.home_planet_idx]
        active: dict[tuple, dict] = {}
        out = []
        steps = int(horizon_days / step) + 1
        for k in range(steps):
            t = t0 + k * step
            dirs = {j: self.planet_dir_mag(j, t)[0] for j in others}
            for x in range(len(others)):
                for y in range(x + 1, len(others)):
                    a, b = others[x], others[y]
                    sep = math.degrees(math.acos(max(-1.0, min(
                        1.0, float(dirs[a] @ dirs[b])))))
                    key = (a, b)
                    if sep < sep_deg:
                        cur = active.get(key)
                        if cur is None or sep < cur["sep_deg"]:
                            active[key] = {
                                "planets": [self.u.planets[a]["name"],
                                            self.u.planets[b]["name"]],
                                "day": round(t, 1),
                                "sep_deg": round(sep, 2)}
                    elif key in active:
                        out.append(active.pop(key))
        out.extend(active.values())
        return sorted(out, key=lambda e: e["day"])

    # ---- the main query ----

    def sky(self, lat_deg: float, lon_deg: float, t: float,
            star_limit: int = 80) -> dict:
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

        planets = []
        for j, planet in enumerate(self.u.planets):
            if j == self.u.home_planet_idx:
                continue
            d, mag, chi = self.planet_dir_mag(j, t)
            alt, az = self.alt_az(d, lat_deg, lon_deg, t)
            planets.append({"name": planet["name"], "alt": round(alt, 1),
                            "az": round(az, 1), "mag": round(mag, 1),
                            "phase_angle": round(chi, 1),
                            "type": planet["type"], "up": alt > 0})

        visible_cons = []
        stars = []
        if period == "night":
            for con in self.u.constellations:
                cen = self.u.gal2eq @ np.asarray(con["centroid_gal"])
                alt, az = self.alt_az(cen, lat_deg, lon_deg, t)
                if alt > 12:
                    visible_cons.append({"name": con["name"],
                                         "alt": round(alt, 1),
                                         "az": round(az, 1)})
            cat, eq = self.star_dirs_eq()
            zenith, north, east = self._observer_basis(lat_deg, lon_deg, t)
            alt_all = np.degrees(np.arcsin(np.clip(eq @ zenith, -1, 1)))
            up = np.nonzero(alt_all > 0)[0][:star_limit]
            az_all = np.degrees(np.arctan2(eq[up] @ east, eq[up] @ north)) % 360
            for k, idx in enumerate(up):
                sid = int(cat["ids"][idx])
                stars.append({"sys": sid, "name": self.u.names.get(sid),
                              "alt": round(float(alt_all[idx]), 1),
                              "az": round(float(az_all[k]), 1),
                              "mag": round(float(cat["mag"][idx]), 2),
                              "rgb": [int(v) for v in self.u.rgb[sid]]})

        return {"t": t, "period": period,
                "sun": {"alt": round(sun_alt, 1), "az": round(sun_az, 1)},
                "moons": moons, "moonlight": round(moonlight, 3),
                "constellations": visible_cons[:10],
                "planets": planets, "stars": stars}


def build_universe(seed: int, year_days: float = 360.0,
                   overrides: dict | None = None) -> tuple[Universe, Observatory]:
    universe = Universe(UniverseSpec.from_overrides(seed, year_days, overrides))
    return universe, Observatory(universe)
