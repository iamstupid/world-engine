"""Phase DAG executor (docs/MAP_SUITE_DESIGN.md §1.9⑤): the entity-plane
counterpart of graphdag. A phase is a deterministic function
(fields, upstream phase outputs, constraint spec, seed) -> {records,
cell_layers, features, state}. Each phase's inputs are hashed; a phase whose
hash is unchanged since the last run is skipped and its cached output
replayed. Because phases are pure, hash(inputs) identifies the output — no
output hashing needed, and downstream hashes chain through upstream input
hashes. This is what makes scoped reseed cheap: editing a late constraint
recomputes only its downstream cone (e.g. touching the polity spec leaves
species/settlement/culture phases cached).

Records from ALL phases are applied to the worldstore in ONE
apply_generation call per kind-batch at the end — apply_generation retires
absent gen entities per kind, so per-phase application of the same kind
would retire earlier phases' entities.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field


class PhaseError(Exception):
    pass


def digest(*parts) -> str:
    h = hashlib.blake2b(digest_size=16)
    for p in parts:
        h.update(p if isinstance(p, bytes) else str(p).encode())
        h.update(b"\x00")
    return h.hexdigest()


def spec_get(spec: dict, path: str):
    """Dotted-path lookup into the constraint spec; missing -> None."""
    cur = spec
    for key in path.split("."):
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


@dataclass
class Phase:
    """name: unique id; fn(ctx, upstream: dict[name, out], spec) -> out dict
    with optional keys records/cell_layers/features/state/report.
    fields: cell-layer names read (their bytes enter the hash).
    spec_keys: dotted paths into the spec that this phase reads (its
    provenance signature on the C-node side)."""
    name: str
    fn: object
    upstream: tuple = ()
    fields: tuple = ()
    spec_keys: tuple = ()
    version: int = 1


@dataclass
class RunResult:
    outputs: dict = field(default_factory=dict)
    report: list = field(default_factory=list)

    def collect(self, key: str) -> list:
        out = []
        for name in self.outputs:
            out.extend(self.outputs[name].get(key) or [])
        return out

    def collect_layers(self) -> dict:
        out = {}
        for name in self.outputs:
            out.update(self.outputs[name].get("cell_layers") or {})
        return out


def run(phases: list[Phase], ctx, spec: dict, cache: dict) -> RunResult:
    """ctx must provide .seed and .field_hash(name)->str. cache is mutated
    in place (session-owned): {phase_name: {"hash", "out"}}."""
    by_name = {p.name: p for p in phases}
    res = RunResult()
    hashes: dict[str, str] = {}
    for p in phases:
        for u in p.upstream:
            if u not in hashes:
                raise PhaseError(f"phase {p.name}: upstream {u} not yet run "
                                 "(phase list must be topologically ordered)")
        sub = {k: spec_get(spec, k) for k in p.spec_keys}
        h = digest(p.name, p.version, ctx.seed,
                   json.dumps(sub, sort_keys=True, ensure_ascii=False),
                   *[ctx.field_hash(f) for f in p.fields],
                   *[hashes[u] for u in p.upstream])
        hashes[p.name] = h
        entry = cache.get(p.name)
        if entry is not None and entry["hash"] == h:
            out, cached = entry["out"], True
        else:
            out = by_name[p.name].fn(
                ctx, {u: res.outputs[u] for u in p.upstream}, spec) or {}
            cache[p.name] = {"hash": h, "out": out}
            cached = False
        res.outputs[p.name] = out
        res.report.append({"phase": p.name, "cached": cached,
                           **(out.get("report") or {})})
    # drop cache entries for phases no longer in the list (era count shrank)
    for stale in [k for k in cache if k not in by_name]:
        del cache[stale]
    return res
