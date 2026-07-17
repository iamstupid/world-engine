"""Rhombus-atlas views of geodesic cell layers (plan addendum b).

The canonical zero-waste layout: the 10*F^2+2 cells of a frequency-F
geodesic grid map bijectively onto 10 F x F rhombi (packed here as a
2F x 5F image, rhombus r at block (r // 5, r % 5)) plus the two pole
cells, which ride along in a header. weterrain.atlas_map(F) provides the
pixel -> cell-id map; packing any cell layer is then a numpy gather, and
unpacking is a scatter through the same map.
"""

from __future__ import annotations

import numpy as np

_MAPS: dict[int, np.ndarray] = {}
_POLES: dict[int, np.ndarray] = {}


def atlas_cell_map(freq: int) -> np.ndarray:
    """(2F, 5F) int32 image of cell ids (no pole cells)."""
    if freq not in _MAPS:
        import weterrain
        raw = np.frombuffer(weterrain.atlas_map(freq), dtype=np.int32)
        _MAPS[freq] = raw.reshape(2 * freq, 5 * freq)
    return _MAPS[freq]


def pole_cell_ids(freq: int) -> np.ndarray:
    """The two cell ids not covered by the rhombus atlas."""
    if freq not in _POLES:
        amap = atlas_cell_map(freq)
        n = 10 * freq * freq + 2
        seen = np.zeros(n, dtype=bool)
        seen[amap.ravel()] = True
        _POLES[freq] = np.nonzero(~seen)[0]
    return _POLES[freq]


def pack_cell_atlas(result: dict, name: str):
    """Cell layer -> (freq, (2F,5F) f32 atlas, (2,) f32 pole values)."""
    entry = result["cell_layers"][name]
    freq = int(entry["frequency"])
    vals = np.frombuffer(entry["data"], dtype=np.float32)
    amap = atlas_cell_map(freq)
    return freq, vals[amap], vals[pole_cell_ids(freq)]


def unpack_cell_atlas(freq: int, atlas: np.ndarray, poles: np.ndarray) -> np.ndarray:
    """Inverse of pack: (2F,5F) atlas + pole values -> per-cell array."""
    out = np.empty(10 * freq * freq + 2, dtype=np.float32)
    out[atlas_cell_map(freq)] = atlas
    out[pole_cell_ids(freq)] = poles
    return out
