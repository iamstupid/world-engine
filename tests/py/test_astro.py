"""Astronomy gates.  The seven skymodel-v1 behavior gates are ported here
(day/night cycle, moon phases, eclipse scanning, constellation visibility
and diurnal rotation, polar midnight sun, moonlight, determinism) and the
full model adds: Kepler solver correctness, Kepler's third law across the
home system, mass-derived stellar physics, IMF sampling, the blackbody
color LUT, naked-eye catalogs, cross-system parallax (constellations
distort between star systems), galaxy morphology, and WorldStore merge
integration with cross-generator isolation."""

import math
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "tools" / "terrain_server"))

import astro  # noqa: E402
from worldstore import WorldStore, stable_id  # noqa: E402


@pytest.fixture(scope="module")
def uo():
    return astro.build_universe(42, 360.0)


# ---------------------------------------------- ported v1 behavior gates ----

def test_deterministic():
    a = astro.build_universe(42, 360.0)[1].sky(30, 10, 100.3)
    b = astro.build_universe(42, 360.0)[1].sky(30, 10, 100.3)
    assert a == b


def test_day_night_cycle(uo):
    _, obs = uo
    alts = [obs.sky(0, 0, 200 + h / 24.0)["sun"]["alt"] for h in range(24)]
    assert max(alts) > 30 and min(alts) < -30
    periods = {obs.sky(0, 0, 200 + h / 24.0)["period"] for h in range(24)}
    assert {"day", "night"} <= periods


def test_moon_phase_range_and_period(uo):
    _, obs = uo
    moon = obs.spec.moons[0]
    period = moon["period_days"]
    fracs = [obs.moon_phase(i * period / 16, moon)[0] for i in range(16)]
    assert max(fracs) > 0.9 and min(fracs) < 0.1
    f0 = obs.moon_phase(50.0, moon)[0]
    f1 = obs.moon_phase(50.0 + period, moon)[0]
    assert abs(f0 - f1) < 0.25


def test_eclipses_found():
    _, obs = astro.build_universe(7, 360.0, {
        "moons": [{"name": "Moon", "period_days": 29.5, "phase0": 0.0,
                   "inclination_deg": 0.0}]})
    ecl = obs.find_eclipses(0, 90)
    assert any(e["type"] == "solar" for e in ecl)
    assert any(e["type"] == "lunar" for e in ecl)


def test_eclipse_seasons_with_inclined_moon(uo):
    _, obs = uo  # default 5-degree inclination gates eclipses to node seasons
    ecl = obs.find_eclipses(0, 1080)
    coplanar = astro.build_universe(42, 360.0, {
        "moons": [{"name": "Moon", "period_days": 29.5, "phase0": 0.0,
                   "inclination_deg": 0.0}]})[1].find_eclipses(0, 1080)
    assert 1 <= len(ecl) < len(coplanar)


def test_constellations_visible_and_rotate(uo):
    universe, obs = uo
    assert len(universe.constellations) >= 10
    t_night = None
    for h in range(48):
        t = 300 + h / 24.0
        if (obs.sky(20, 0, t)["period"] == "night" and
                obs.sky(20, 0, t + 2 / 24.0)["period"] == "night"):
            t_night = t
            break
    assert t_night is not None
    s1 = obs.sky(20, 0, t_night)
    assert len(s1["constellations"]) > 0
    s2 = obs.sky(20, 0, t_night + 2 / 24.0)
    common = {c["name"] for c in s1["constellations"]} & \
             {c["name"] for c in s2["constellations"]}
    assert common
    deltas = []
    for name in common:
        az1 = next(c["az"] for c in s1["constellations"] if c["name"] == name)
        az2 = next(c["az"] for c in s2["constellations"] if c["name"] == name)
        d = abs(az2 - az1) % 360
        deltas.append(min(d, 360 - d))
    # ~15 deg/hour for mid-sky objects; circumpolar ones may barely move
    assert any(5 < d < 60 for d in deltas)


def test_polar_midnight_sun(uo):
    _, obs = uo
    found = False
    for d0 in range(0, 360, 15):
        alts = [obs.sky(85, 0, d0 + h / 24.0)["sun"]["alt"] for h in range(24)]
        if min(alts) > 0:
            found = True
            break
    assert found


def test_moonlight_field(uo):
    _, obs = uo
    s = obs.sky(10, 10, 123.45)
    assert 0.0 <= s["moonlight"] <= 1.0


# ------------------------------------------------------- kepler machinery ---

def test_kepler_solver_residual_and_bounds():
    for e in (0.0, 0.3, 0.7, 0.93):
        for m in np.linspace(0, 2 * math.pi, 37):
            big_e = astro.solve_kepler(m, e)
            res = (big_e - e * math.sin(big_e) - m) % (2 * math.pi)
            assert min(res, 2 * math.pi - res) < 1e-9
    planet = {"a": 2.5, "e": 0.4, "M0": 1.1, "period_days": 500.0,
              "rot": astro.orbit_matrix(0.3, 1.0, 2.0)}
    radii = [np.linalg.norm(astro.orbit_pos(planet, t))
             for t in np.linspace(0, 500, 101)]
    assert min(radii) > 2.5 * 0.6 - 1e-6 and max(radii) < 2.5 * 1.4 + 1e-6
    p0 = astro.orbit_pos(planet, 123.4)
    p1 = astro.orbit_pos(planet, 123.4 + 500.0)
    assert np.linalg.norm(p0 - p1) < 1e-8


def test_kepler_third_law_home_system(uo):
    universe, _ = uo
    home = universe.planets[universe.home_planet_idx]
    assert abs(home["period_days"] - 360.0) < 1e-9
    for p in universe.planets:
        expect = 360.0 * (p["a"] / home["a"]) ** 1.5
        assert abs(p["period_days"] - expect) < 1e-6
    assert 4 <= len(universe.planets) <= 8


# --------------------------------------------------------- star physics -----

def test_star_physics_sunlike_and_monotonic(uo):
    universe, _ = uo
    assert abs(float(universe.mv[0]) - 4.82) < 0.3   # home star is sun-like
    assert abs(float(universe.t_eff[0]) - 5772.0) < 100
    masses = np.array([0.2, 0.5, 1.0, 2.0, 5.0, 10.0])
    lums = astro.mass_luminosity(masses)
    assert np.all(np.diff(lums) > 0)
    temps = astro.effective_temp(lums, astro.mass_radius(masses))
    assert np.all(np.diff(temps) > 0)
    assert astro.spectral_class(5772) == "G"
    assert astro.spectral_class(12000) == "B"
    assert astro.spectral_class(3000) == "M"


def test_imf_sampling():
    rng = np.random.default_rng(9)
    m = astro.sample_imf(rng, 4000, 0.08, 10.0)
    assert float(m.min()) >= 0.08 and float(m.max()) <= 10.0
    assert float(np.median(m)) < 0.6  # bottom-heavy
    m2 = astro.sample_imf(np.random.default_rng(9), 4000, 0.08, 10.0)
    assert np.array_equal(m, m2)


def test_blackbody_lut_monotonic():
    lut = astro._lut()
    rows = np.asarray(lut["srgb"])
    assert rows.min() >= 0 and rows.max() <= 255
    b_minus_r = rows[:, 2].astype(int) - rows[:, 0].astype(int)
    assert b_minus_r[0] < -180          # 1000 K: strongly red
    assert b_minus_r[-1] > 60           # 40000 K: strongly blue
    assert np.all(np.diff(b_minus_r) >= -1)  # monotone up to rounding
    warm = astro.blackbody_rgb(2500.0)
    cool = astro.blackbody_rgb(25000.0)
    assert warm[0] > warm[2] and cool[2] > cool[0]


# ------------------------------------------------------------- catalogs -----

def test_naked_eye_catalog(uo):
    universe, _ = uo
    cat = universe.naked_eye(0)
    assert 60 <= len(cat["ids"]) <= 2000
    assert float(cat["mag"].max()) < universe.spec.mag_limit
    assert np.all(np.diff(cat["mag"]) >= 0)          # sorted by brightness
    assert 0 not in set(int(i) for i in cat["ids"])  # home star not in own sky
    norms = np.linalg.norm(cat["dirs"], axis=1)
    assert np.allclose(norms, 1.0, atol=1e-9)
    named = [universe.names.get(int(i)) for i in cat["ids"][:20]]
    assert all(n is not None for n in named)


def test_cross_system_parallax(uo):
    universe, _ = uo
    d = np.linalg.norm(universe.sys_pos - universe.sys_pos[0], axis=1)
    other = int(np.argsort(d)[1])  # nearest neighbor system
    cat_a = universe.naked_eye(0)
    cat_b = universe.naked_eye(other)
    dir_a = {int(s): cat_a["dirs"][k] for k, s in enumerate(cat_a["ids"])}
    dir_b = {int(s): cat_b["dirs"][k] for k, s in enumerate(cat_b["ids"])}
    dist_a = {int(s): float(cat_a["dist_pc"][k])
              for k, s in enumerate(cat_a["ids"])}
    shared = [s for s in dir_a if s in dir_b and s != other]
    assert len(shared) > 20
    shifts = sorted(
        (dist_a[s], math.degrees(math.acos(max(-1.0, min(1.0,
         float(dir_a[s] @ dir_b[s])))))) for s in shared)
    near = np.mean([sh for _, sh in shifts[:10]])
    far = np.mean([sh for _, sh in shifts[-10:]])
    assert near > 2.0 and near > 3.0 * far  # nearby stars shift far more


def test_sky_from_constellation_distortion(uo):
    universe, _ = uo
    home = universe.sky_from(0)
    assert home["frame"] == "galactic" and len(home["stars"]) >= 60
    d = np.linalg.norm(universe.sys_pos - universe.sys_pos[0], axis=1)
    other = int(np.argsort(d)[1])
    away = universe.sky_from(other)
    names_home = {c["name"] for c in home["constellations"]}
    names_away = {c["name"] for c in away["constellations"]}
    assert names_home == names_away  # membership is home-culture-fixed
    con_h = home["constellations"][0]
    con_a = next(c for c in away["constellations"]
                 if c["name"] == con_h["name"])
    shift = max(math.degrees(math.acos(max(-1.0, min(1.0, float(
        np.dot(da, db)))))) for da, db in zip(con_h["dirs"], con_a["dirs"]))
    assert shift > 0.5  # the shape visibly distorts


# ------------------------------------------------------ planets & events ----

def test_planets_bright_wanderers(uo):
    universe, obs = uo
    best = min(min(p["mag"] for p in obs.sky(0, 0, float(t))["planets"])
               for t in range(0, 720, 15))
    assert best < 2.5  # at least one naked-eye wanderer
    conj = obs.conjunctions(0, 720)
    assert all(c["sep_deg"] < 1.5 for c in conj)


def test_planet_magnitude_formula_venus_like():
    """Validated anchor: a Venus-like planet peaks near mag -4."""
    universe, obs = astro.build_universe(42, 360.0)
    venus = {"a": 0.72, "e": 0.0, "M0": 0.0, "period_days": 360 * 0.72 ** 1.5,
             "rot": astro.orbit_matrix(0, 0, 0), "radius_earth": 0.95,
             "albedo": 0.65, "type": "rocky", "is_home": False,
             "name": "Venus", "inc_deg": 0.0, "moon_count": 0}
    universe.planets.append(venus)
    j = len(universe.planets) - 1
    mags = [obs.planet_dir_mag(j, float(t))[1] for t in range(0, 360, 2)]
    assert -5.5 < min(mags) < -3.0


# ---------------------------------------------------- galaxy morphology -----

def test_galaxy_morphology_spiral_vs_cluster():
    spiral, _ = astro.build_universe(11, 360.0, {"n_local": 200})
    fpos = spiral.sys_pos[spiral.tier == 2]
    r_cyl = np.hypot(fpos[:, 0], fpos[:, 1])
    assert np.median(np.abs(fpos[:, 2])) < 0.25 * np.median(r_cyl)  # flat disc
    cluster, _ = astro.build_universe(11, 360.0, {
        "galaxy_type": "cluster", "n_field": 900, "n_local": 200})
    r = np.linalg.norm(cluster.sys_pos[cluster.tier == 2], axis=1)
    r50, r90 = np.percentile(r, [50, 90])
    assert r50 / r90 < 0.6  # King-profile concentration (uniform ~0.82)


# ------------------------------------------------- store integration --------

def test_store_records_merge_and_cross_generator_isolation(uo):
    universe, _ = uo
    store = WorldStore()
    recs = universe.store_records()
    assert any(r["kind"] == "star_system" for r in recs)
    assert any(r["kind"] == "planet" and r["attrs"]["is_home"] for r in recs)
    store.apply_generation(recs)
    star_key = next(r["gen_key"] for r in recs if r["kind"] == "star_system")
    eid = stable_id("star_system", star_key)
    store.set_attr(eid, "name", "The Author Star")
    store.lock(eid, "name")
    store.apply_generation(universe.store_records())  # astro regen
    assert store.get(eid, "name") == "The Author Star"
    # another generator's batch (civ kinds) must NOT retire astro entities
    store.apply_generation([{"kind": "settlement", "gen_key": 1,
                             "attrs": {"name": "Town"}}])
    assert store.get(eid, "_retired") is not True
    assert store.get(eid, "name") == "The Author Star"
