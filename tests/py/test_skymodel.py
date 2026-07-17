"""Sky model gates: determinism, day/night cycle, moon phases, eclipse
scanning, constellation visibility and diurnal rotation."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "terrain_server"))

import math  # noqa: E402

from skymodel import SkyModel, SkySpec  # noqa: E402


def make():
    return SkyModel(SkySpec(seed=42))


def test_deterministic():
    a = make().sky(30, 10, 100.3)
    b = make().sky(30, 10, 100.3)
    assert a == b


def test_day_night_cycle():
    sky = make()
    alts = [sky.sky(0, 0, 200 + h / 24.0)["sun"]["alt"] for h in range(24)]
    assert max(alts) > 30 and min(alts) < -30  # equator sees both extremes
    periods = {sky.sky(0, 0, 200 + h / 24.0)["period"] for h in range(24)}
    assert {"day", "night"} <= periods


def test_moon_phase_range_and_period():
    sky = make()
    period = sky.spec.moons[0]["period_days"]
    fracs = [sky.moon_phase(t, sky.spec.moons[0])[0]
             for t in [i * period / 16 for i in range(16)]]
    assert max(fracs) > 0.9 and min(fracs) < 0.1  # full and new occur
    f0 = sky.moon_phase(50.0, sky.spec.moons[0])[0]
    # synodic-ish repetition (sidereal vs solar drift allowed)
    f1 = sky.moon_phase(50.0 + period, sky.spec.moons[0])[0]
    assert abs(f0 - f1) < 0.25


def test_eclipses_found():
    spec = SkySpec(seed=7)
    spec.moons[0]["inclination_deg"] = 0.0  # coplanar -> eclipses every month
    sky = SkyModel(spec)
    ecl = sky.find_eclipses(0, 90)
    assert any(e["type"] == "solar" for e in ecl)
    assert any(e["type"] == "lunar" for e in ecl)


def test_constellations_visible_and_rotate():
    sky = make()
    assert len(sky.constellations) >= 10
    # find a night hour
    t_night = None
    for h in range(48):
        t = 300 + h / 24.0
        if sky.sky(20, 0, t)["period"] == "night":
            t_night = t
            break
    assert t_night is not None
    s1 = sky.sky(20, 0, t_night)
    assert len(s1["constellations"]) > 0
    # two hours later the sky has rotated
    s2 = sky.sky(20, 0, t_night + 2 / 24.0)
    common = {c["name"] for c in s1["constellations"]} & \
             {c["name"] for c in s2["constellations"]}
    assert common
    name = next(iter(common))
    az1 = next(c["az"] for c in s1["constellations"] if c["name"] == name)
    az2 = next(c["az"] for c in s2["constellations"] if c["name"] == name)
    delta = abs(az2 - az1) % 360
    assert 5 < min(delta, 360 - delta) < 60  # ~15 deg/hour expected


def test_polar_midnight_sun():
    sky = make()
    # near the pole in local summer there should be days with no night
    year = sky.spec.year_days
    # summer solstice-ish: scan a day at lat 85 across the year for a
    # 24h period where the sun never sets
    found = False
    for d0 in range(0, int(year), 15):
        alts = [sky.sky(85, 0, d0 + h / 24.0)["sun"]["alt"] for h in range(24)]
        if min(alts) > 0:
            found = True
            break
    assert found


def test_moonlight_field():
    sky = make()
    s = sky.sky(10, 10, 123.45)
    assert 0.0 <= s["moonlight"] <= 1.0
