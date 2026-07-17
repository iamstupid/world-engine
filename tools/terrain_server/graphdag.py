"""DAG pipeline executor (docs/PIPELINE_DAG_DESIGN.md, decided 2026-07-18).

ComfyUI-pattern: declarative node registry + topological execution +
content-addressed caching, with the heavy operators in C++ (weterrain).
Edges carry CellField(frequency) values — frequency is part of the type
and edges between mismatched frequencies are REJECTED at validate time
(the UI offers one-click resample insertion instead of implicit magic).

spec = {"nodes": [{"id", "type", "params": {...},
                   "inputs": {slot: "node_id.output"}}]}
"""

from __future__ import annotations

import hashlib
import json
from graphlib import CycleError, TopologicalSorter

import numpy as np

REGISTRY = {
    "noise": {
        "inputs": {}, "outputs": ["field"],
        "params": {"frequency": 176, "seed_offset": 0, "base_frequency": 0.8,
                   "octaves": 6, "lacunarity": 2.0, "gain": 0.5,
                   "amplitude_m": 800.0, "ridged": 0},
        "doc": "球面分形噪声（fBm/ridged），直接在格胞上求值"},
    "tectonics": {
        "inputs": {},
        "outputs": ["elevation", "uplift", "age", "crust"],
        "params": {"grid_frequency": 64, "plate_count": 14,
                   "simulation_steps": 125, "continent_ratio": 0.4},
        "doc": "拉格朗日板块模拟；输出正典地壳的四个格胞场"},
    "resample": {
        "inputs": {"src": "cell"}, "outputs": ["out"],
        "params": {"frequency": 176},
        "doc": "频率重采样（locate 重心插值）——上/下采样节点"},
    "math": {
        "inputs": {"a": "cell", "b": "cell"}, "outputs": ["out"],
        "params": {"op": "add", "ka": 1.0, "kb": 1.0},
        "doc": "out = op(ka*a, kb*b)；op: add/sub/mul/min/max/lerp(ka=t)"},
    "mask": {
        "inputs": {"src": "cell"}, "outputs": ["out"],
        "params": {"threshold": 0.0, "smooth": 0.0, "invert": 0},
        "doc": "阈值/软阈值掩膜（0..1）"},
    "paint": {
        "inputs": {}, "outputs": ["field"],
        "params": {"frequency": 176, "layer": "uplift_paint", "scale": 1.0},
        "doc": "作者画笔层（源节点）；引用会话中的画笔 blob"},
    "physics": {
        "inputs": {"z0": "cell", "uplift": "cell"},
        "outputs": ["elevation", "flow_accum", "ocean", "temperature",
                    "precip", "runoff", "vegetation", "biome"],
        "params": {"frequency": 176, "fixed_point_iterations": 6,
                   "multigrid_levels": 5, "time_years": 2e6,
                   "discontinuity_iterations": 16, "amplify": 1},
        "doc": "侵蚀/气候/水文/生态全物理段；raster 视图一并产出"},
    "export": {
        "inputs": {"field": "cell"}, "outputs": [],
        "params": {"name": "custom_field", "width": 0, "height": 0,
                   "as_cell_layer": 1},
        "doc": "导出任意场为 equirect raster 层和/或 cell 层"},
}

PHYSICS_CELL_OUT = {"elevation": "cell_elevation_m",
                    "flow_accum": "cell_flow_accum_m2", "ocean": "cell_ocean",
                    "temperature": "cell_temperature_c",
                    "precip": "cell_precip_mm", "runoff": "cell_runoff_mm",
                    "vegetation": "cell_vegetation", "biome": "cell_biome"}


class GraphError(Exception):
    pass


def _params(node):
    merged = dict(REGISTRY[node["type"]]["params"])
    merged.update(node.get("params", {}))
    return merged


def infer_frequencies(spec) -> dict:
    """Static frequency inference per node output; raises GraphError on
    mismatched edges (the hard-validation contract)."""
    nodes = {n["id"]: n for n in spec["nodes"]}
    deps = {n["id"]: {ref.split(".")[0] for ref in n.get("inputs", {}).values()}
            for n in spec["nodes"]}
    try:
        order = list(TopologicalSorter(deps).static_order())
    except CycleError as exc:
        raise GraphError(f"环依赖: {exc}") from exc
    freq: dict[str, int] = {}
    for nid in order:
        if nid not in nodes:
            raise GraphError(f"未知上游节点: {nid}")
        node = nodes[nid]
        p = _params(node)
        in_freqs = {}
        for slot, ref in node.get("inputs", {}).items():
            if "." not in ref:
                raise GraphError(f"{nid}.{slot} 输入须为 '节点.输出' 形式: {ref}")
            up, out_slot = ref.split(".", 1)
            if up not in freq:
                raise GraphError(f"{nid}.{slot} 引用了无输出的节点 {up}")
            if out_slot not in REGISTRY[nodes[up]["type"]]["outputs"]:
                raise GraphError(f"{nid}.{slot} 引用了不存在的输出 {ref}")
            in_freqs[slot] = freq[up]
        t = node["type"]
        if t not in REGISTRY:
            raise GraphError(f"未知节点类型: {t}")
        for slot in REGISTRY[t]["inputs"]:
            if slot not in in_freqs and not (t == "physics" and slot == "uplift"):
                raise GraphError(f"{nid} 缺少输入 {slot}")
        if t in ("noise", "paint", "resample"):
            freq[nid] = int(p["frequency"])
        elif t == "tectonics":
            freq[nid] = int(p["grid_frequency"])
        elif t in ("math",):
            if in_freqs["a"] != in_freqs["b"]:
                raise GraphError(
                    f"{nid}: 频率不匹配 a@F{in_freqs['a']} vs b@F{in_freqs['b']}"
                    "（请插入 resample 节点）")
            freq[nid] = in_freqs["a"]
        elif t == "mask":
            freq[nid] = in_freqs["src"]
        elif t == "physics":
            f = int(p["frequency"])
            freq[nid] = f  # z0/uplift may be any frequency: interpolated
        elif t == "export":
            freq[nid] = in_freqs["field"]
    return freq


def _node_key(node, upstream_keys):
    blob = json.dumps({"t": node["type"], "p": _params(node),
                       "u": upstream_keys}, sort_keys=True)
    return hashlib.blake2b(blob.encode(), digest_size=16).hexdigest()


def _build_params(session, overrides):
    import weterrain
    p = weterrain.Params()
    for k, v in session.params.items():
        p.set(k, float(v))
    for k, v in overrides.items():
        p.set(k, float(v))
    return p


def execute(spec, session, progress=None, cancel=None) -> dict:
    """Run the graph; returns a result dict merged like generate()'s.
    Per-node content-addressed cache lives on the session."""
    import weterrain
    infer_frequencies(spec)  # hard validation first
    nodes = {n["id"]: n for n in spec["nodes"]}
    deps = {n["id"]: {r.split(".")[0] for r in n.get("inputs", {}).values()}
            for n in spec["nodes"]}
    order = list(TopologicalSorter(deps).static_order())
    cache = getattr(session, "graph_cache", None)
    if cache is None:
        cache = session.graph_cache = {}
    values: dict[str, dict] = {}   # node id -> {output: (freq, np.ndarray)}
    keys: dict[str, str] = {}
    evaluated = []
    result = {"width": int(session.params.get("world.width", 1024)),
              "height": int(session.params.get("world.height", 512)),
              "layers": {}, "cell_layers": {}, "hash": ""}

    def cellbytes(arr):
        return np.asarray(arr, np.float32).tobytes()

    for i, nid in enumerate(order):
        if cancel is not None and getattr(cancel, "is_set", lambda: False)():
            raise RuntimeError("cancelled")
        node = nodes[nid]
        p = _params(node)
        upstream = {slot: keys[r.split(".")[0]] + ":" + r.split(".")[1]
                    for slot, r in node.get("inputs", {}).items()}
        key = _node_key(node, upstream)
        keys[nid] = key
        if progress:
            progress(nid, i, len(order))
        if key in cache:
            values[nid] = cache[key]
            continue
        evaluated.append(nid)

        def inp(slot):
            ref = node["inputs"][slot]
            up, out = ref.split(".")
            return values[up][out]

        t = node["type"]
        if t == "noise":
            seed = int(session.params.get("world.seed", 1337)) + int(p["seed_offset"])
            raw = weterrain.noise_cells(int(p["frequency"]), seed,
                                        float(p["base_frequency"]),
                                        int(p["octaves"]), float(p["lacunarity"]),
                                        float(p["gain"]), float(p["amplitude_m"]),
                                        bool(p["ridged"]))
            out = {"field": (int(p["frequency"]),
                             np.frombuffer(raw, np.float32))}
        elif t == "tectonics":
            wp = _build_params(session, {
                "tectonics.grid_frequency": p["grid_frequency"],
                "tectonics.plate_count": p["plate_count"],
                "tectonics.simulation_steps": p["simulation_steps"],
                "tectonics.continent_ratio": p["continent_ratio"]})
            paint = {k: v for k, v in getattr(session, "paint", {}).items()} or None
            data = weterrain.tectonics_cells(wp, paint)
            f = int(p["grid_frequency"])
            cl = data["cell_layers"]
            out = {"elevation": (f, np.frombuffer(cl["tect_elevation_m"]["data"], np.float32)),
                   "uplift": (f, np.frombuffer(cl["tect_uplift_m_yr"]["data"], np.float32)),
                   "age": (f, np.frombuffer(cl["tect_age_myr"]["data"], np.float32)),
                   "crust": (f, np.frombuffer(cl["tect_crust_type"]["data"], np.float32))}
        elif t == "resample":
            sf, arr = inp("src")
            raw = weterrain.resample_cells(sf, cellbytes(arr), int(p["frequency"]))
            out = {"out": (int(p["frequency"]), np.frombuffer(raw, np.float32))}
        elif t == "math":
            fa, a = inp("a")
            _, b = inp("b")
            ka, kb, op = float(p["ka"]), float(p["kb"]), p["op"]
            if op == "lerp":
                v = a * (1 - ka) + b * ka
            else:
                x, y = a * ka, b * kb
                v = {"add": x + y, "sub": x - y, "mul": x * y,
                     "min": np.minimum(x, y), "max": np.maximum(x, y)}[op]
            out = {"out": (fa, v.astype(np.float32))}
        elif t == "mask":
            f, a = inp("src")
            thr, sm = float(p["threshold"]), float(p["smooth"])
            if sm > 0:
                x = np.clip((a - thr) / sm + 0.5, 0, 1)
                v = x * x * (3 - 2 * x)
            else:
                v = (a > thr).astype(np.float32)
            if p["invert"]:
                v = 1.0 - v
            out = {"out": (f, v.astype(np.float32))}
        elif t == "paint":
            f = int(p["frequency"])
            blob = getattr(session, "paint", {}).get(p["layer"])
            if blob is None:
                v = np.zeros(10 * f * f + 2, np.float32)
            else:
                w0, h0, data0 = blob
                eq = np.frombuffer(data0, np.float32).reshape(h0, w0)
                g = weterrain.geodesic_graph(f)
                centers = np.frombuffer(g["centers_f64"], np.float64).reshape(-1, 3)
                lat = np.degrees(np.arcsin(np.clip(centers[:, 1], -1, 1)))
                lon = np.degrees(np.arctan2(centers[:, 2], centers[:, 0]))
                x = np.clip(((lon + 180) / 360 * w0).astype(int), 0, w0 - 1)
                y = np.clip(((90 - lat) / 180 * h0).astype(int), 0, h0 - 1)
                v = eq[y, x].astype(np.float32) * float(p["scale"])
            out = {"field": (f, v)}
        elif t == "physics":
            zf, z0 = inp("z0")
            upref = node.get("inputs", {}).get("uplift")
            wp = _build_params(session, {
                "world.physics_grid_frequency": p["frequency"],
                "erosion.fixed_point_iterations": p["fixed_point_iterations"],
                "erosion.multigrid_levels": p["multigrid_levels"],
                "erosion.time_years": p["time_years"],
                "erosion.discontinuity_iterations": p["discontinuity_iterations"],
                "amplify.enable": p["amplify"]})
            if upref:
                uf, up = inp("uplift")
                data = weterrain.physics_cells(wp, zf, cellbytes(z0), uf,
                                               cellbytes(up), result["width"],
                                               result["height"])
            else:
                data = weterrain.physics_cells(wp, zf, cellbytes(z0), 0, None,
                                               result["width"], result["height"])
            for lname, entry in data["layers"].items():
                result["layers"][lname] = entry
            for lname, entry in data["cell_layers"].items():
                result["cell_layers"][lname] = entry
            f = int(p["frequency"])
            out = {}
            for slot, lname in PHYSICS_CELL_OUT.items():
                out[slot] = (f, np.frombuffer(
                    data["cell_layers"][lname]["data"], np.float32))
        elif t == "export":
            f, arr = inp("field")
            w = int(p["width"]) or result["width"]
            h = int(p["height"]) or result["height"]
            raw = weterrain.rasterize_cells(f, cellbytes(arr), w, h)
            result["layers"][p["name"]] = {"dtype": "f32", "data": raw}
            if p["as_cell_layer"]:
                result["cell_layers"][f"cell_{p['name']}"] = {
                    "frequency": f, "dtype": "f32", "data": cellbytes(arr)}
            out = {}
        else:
            raise GraphError(f"未实现的节点: {t}")
        values[nid] = out
        cache[key] = out

    result["hash"] = hashlib.blake2b(
        json.dumps(sorted(keys.values())).encode(), digest_size=8).hexdigest()
    result["evaluated"] = evaluated
    return result


def default_template() -> dict:
    """The IGM-native main chain: tectonics -> resample -> +noise ->
    physics (combine happens in cell space; equirect only at export)."""
    return {"nodes": [
        {"id": "tect", "type": "tectonics",
         "params": {"grid_frequency": 64}},
        {"id": "up_hi", "type": "resample", "params": {"frequency": 176},
         "inputs": {"src": "tect.uplift"}},
        {"id": "base_hi", "type": "resample", "params": {"frequency": 176},
         "inputs": {"src": "tect.elevation"}},
        {"id": "detail", "type": "noise",
         "params": {"frequency": 176, "amplitude_m": 440.0, "seed_offset": 11}},
        {"id": "combine", "type": "math", "params": {"op": "add"},
         "inputs": {"a": "base_hi.out", "b": "detail.field"}},
        {"id": "phys", "type": "physics", "params": {"frequency": 176},
         "inputs": {"z0": "combine.out", "uplift": "up_hi.out"}},
    ]}
