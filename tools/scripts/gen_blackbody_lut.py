"""Offline generator for the blackbody T -> sRGB lookup table used by
tools/terrain_server/astro.py.  Run once and commit the JSON; the runtime
never needs a colour library.

Primary path uses colour-science (CIE 1931 2-deg CMFs).  Fallback is the
Wyman-Sloan-Shirley (JCGT 2013) multi-lobe Gaussian fit of the same CMFs,
which is accurate to well under 1/255 for smooth Planck spectra.

Colors are chromaticity-only: linear RGB is scaled so max(channel) == 1
before gamma; brightness is carried separately by magnitudes.
"""

import json
from pathlib import Path

import numpy as np

T_MIN, T_MAX, N = 1000.0, 40000.0, 96
OUT = Path(__file__).resolve().parents[1] / "terrain_server" / "data" / "blackbody_lut.json"

LAM = np.arange(380.0, 781.0, 1.0)  # nm
C2_NM_K = 1.4387769e7               # hc/k in nm*K


def planck(lam_nm: np.ndarray, t: float) -> np.ndarray:
    x = lam_nm * 1e-3  # scale-free shape; only relative spectrum matters
    return x ** -5.0 / np.expm1(C2_NM_K / (lam_nm * t))


def _lobe(lam, mu, s1, s2):
    s = np.where(lam < mu, s1, s2)
    return np.exp(-0.5 * ((lam - mu) * s) ** 2)


def cmf_wyman(lam: np.ndarray) -> np.ndarray:
    x = (0.362 * _lobe(lam, 442.0, 0.0624, 0.0374)
         + 1.056 * _lobe(lam, 599.8, 0.0264, 0.0323)
         - 0.065 * _lobe(lam, 501.1, 0.0490, 0.0382))
    y = (0.821 * _lobe(lam, 568.8, 0.0213, 0.0247)
         + 0.286 * _lobe(lam, 530.9, 0.0613, 0.0322))
    z = (1.217 * _lobe(lam, 437.0, 0.0845, 0.0278)
         + 0.681 * _lobe(lam, 459.0, 0.0385, 0.0725))
    return np.stack([x, y, z], axis=0)


XYZ_TO_SRGB = np.array([[3.2406, -1.5372, -0.4986],
                        [-0.9689, 1.8758, 0.0415],
                        [0.0557, -0.2040, 1.0570]])


def gamma(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def xyz_analytic(t: float) -> np.ndarray:
    return cmf_wyman(LAM) @ planck(LAM, t)


def build():
    temps = np.geomspace(T_MIN, T_MAX, N)
    xyz_fn, source = xyz_analytic, "wyman2013-analytic-cmf"
    try:
        import colour  # noqa: F401

        cmfs = colour.MSDS_CMFS["CIE 1931 2 Degree Standard Observer"]
        shape = colour.SpectralShape(380, 780, 1)

        def xyz_cs(t):
            sd = colour.sd_blackbody(t, shape)
            return np.asarray(colour.sd_to_XYZ(sd, cmfs=cmfs), dtype=float)

        xyz_fn, source = xyz_cs, "colour-science"
    except ImportError:
        pass

    rows = []
    for t in temps:
        xyz = np.asarray(xyz_fn(float(t)), dtype=float)
        lin = XYZ_TO_SRGB @ (xyz / max(xyz[1], 1e-12))
        lin = np.clip(lin, 0.0, None)
        lin /= max(float(lin.max()), 1e-12)
        rows.append([int(round(255 * v)) for v in gamma(lin)])

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "t_min": T_MIN, "t_max": T_MAX, "n": N, "spacing": "log",
        "source": source, "srgb": rows}), encoding="utf-8")
    anchors = {k: rows[int(round((np.log(k / T_MIN) / np.log(T_MAX / T_MIN)) * (N - 1)))]
               for k in (2000, 3500, 5800, 10000, 25000)}
    print(f"wrote {OUT} via {source}; anchors: {anchors}")


if __name__ == "__main__":
    build()
