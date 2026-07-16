# Tectonic Terrain Generation — Rebuild Plan

Status: IMPLEMENTED (2026-07-17) - milestones M0-M6 landed on main; the failed
Eulerian experiment is archived on branch `experiment/tectonics-eulerian`.
See WORKLOG.md for the milestone summary and measurements.

## 1. Post-mortem of the failed experiment

The experiment (working-tree diff on `tectonics_stage.cpp`) ported the *equations* of
Cortial et al. 2019 (subduction uplift `u0·f(d)·g(v)·h(z)`, collision surge, ridge
generation, per-step erosion/dampening) but not the paper's *kinematics*. Root causes,
confirmed against the paper and the rendered outputs:

1. **Eulerian grid, Lagrangian model.** The paper moves crust *with* the plates
   (points carried by rigid rotations, global resampling every 10–60 steps). The
   experiment kept a fixed lat-lon raster: plate *boundaries* moved (warped-Voronoi
   scores re-evaluated against rotated seeds each step) while crust and elevation
   stayed put. Boundaries swept across static crust for 125 steps, re-stamping ridge
   and subduction profiles every step → concentric "onion-ring" artifacts across the
   oceans, smeared uplift bands, and continents that never drift.
2. **No crust transport at all**: no subduction consumption, no terrane transfer, no
   oceanic age, no resampling. Divergence overwrote elevation in place
   (`z = α·z + (1−α)·z_ridge`) repeatedly at whatever location the boundary happened
   to pass through.
3. **Polar degeneracy**: the bottom rows of the equirectangular grid degenerate to a
   single point; boundary tangent/velocity math there is unstable and pole blur
   smeared a permanent land ring across the south pole.
4. **Erosion decoupling**: `uplift_rate` was derived from a time-averaged
   convergent-strength proxy (tiny values), so the analytical erosion stage had
   almost nothing to carve against; final maps ≈ raw tectonic output.

Conclusion: the equations were fine; the missing piece is the **Lagrangian crust
model** (plates as point sets that move, get consumed, and get regenerated). That is
the core of the rebuild.

## 2. What we keep

- Stage-DAG pipeline, dataset/layer model, determinism rules, caching
  (`pipeline.cpp`, `TerrainDataset`, `TerrainDomain`) — unchanged contracts.
- Analytical erosion stage (Tzathas et al. 2024), hydrology (D∞), masks, CLI —
  downstream consumers stay raster-based on the lat-lon grid.
- `params.h` paper constants introduced by the experiment (Appendix A values) —
  correct and reusable.
- Noise stage as a *detail* layer (the experiment's inversion "tectonics drives
  large scale, noise adds detail" is the right call and matches the paper's
  amplification philosophy).

## 3. Target architecture for the tectonics stage

Internally point-based (Lagrangian), externally raster (unchanged stage contract).

```
canonical icosahedral geodesic grid (frequency F ≈ 100 → 10F²+2 ≈ 100k cells)
        │  plate partition: warped spherical Voronoi over P plate seeds
        ▼
per-point crust state (Table 1 of the paper):
  crust type, thickness(optional v1: skip), elevation z,
  oceanic: age a_o, ridge direction r
  continental: orogeny type o, orogeny age a_c, fold direction f
        │
        ▼
simulation loop, δt = 2 My, ~125 steps (250 My):
  1. rotate each plate's points rigidly:  p ← R(w_i, ω_i·δt)·p
  2. boundary classification between neighboring plates (gap vs overlap)
  3. overlap → subduction: uplift on overriding plate
       u_e = u0 · f(d) · g(v/v0) · h(z̃),  z̃ = (z−z_t)/(z_c−z_t), h = z̃²
     (older oceanic plate subducts; oceanic subducts under continental)
  4. continental-triangle interpenetration > 300 km → collision event:
       Δz = Δc · A · (1−(d/r)²)²,  r = r_c·sqrt((v/v0)·(A/A0));
       terrane detaches from P_i, sutures onto P_j; fold dir radial from centroid
  5. every K steps (K ∈ [10,60], adaptive on max plate speed): global resampling
     onto the canonical sample set:
       gap regions → new oceanic crust: z = α·z̄ + (1−α)·z_Γ, α = d_Γ/(d_Γ+d_P),
         age 0, ridge dir recorded; overlap → overriding plate wins (consumption)
  6. rifting (Poisson, λ = λ0·f(x_P)·A/A0, split into 2–4 warped Voronoi
     sub-plates with diverging motions); slab pull nudges w_i toward fronts
  7. every step: continental erosion −(z/z_c)·ε_c·δt, oceanic dampening
     −(1−z/z_t)·ε_o·δt, trench sediment fill +ε_t·δt
        │
        ▼
rasterize cell crust → lat-lon layers (barycentric over the geodesic
triangulation, which the grid provides natively):
  tectonic_elevation_m, crust_type, plate_id, oceanic_age_myr,
  fold_dir, orogeny_type, orogeny_age_myr, uplift_rate_m_per_yr
```

Key data-structure decisions:

- **Canonical sample set = icosahedral geodesic grid** (user-provided neighbor
  indexing, `docs/references/sample_ico.hpp`; to be productionized as
  `world_engine/terrain/include/world_engine/terrain/geodesic_domain.h`).
  Layout: 3F+1 rows × 5 sectors, cap strides grow linearly / mid band constant,
  10F²+2 cells, 12 pentagons (degree 5), CW-ordered neighbor patterns per zone.
  This replaces both the Fibonacci sampling AND the deferred spherical-Delaunay
  plan: triangulation, adjacency, and near-uniform sampling come with the grid.
- **Storage**: flat row-major ring layout (rows concatenated via prefix-sum
  offsets: top cap `1 + 5r(r−1)/2`, mid band linear, bottom cap symmetric).
  Full-domain stencil sweeps read three contiguous row-streams (self, north row,
  south row) — locality on par with an equirectangular raster; OpenMP splits by
  row. Quadtree/D&C ordering rejected as a *storage* format (indirection tax on
  dense sweeps); if out-of-core tiling is needed later, chunk by row-band × sector.
- **Multigrid nesting for free**: frequency-F/2 cell (r, c) embeds at (2r, 2c) in
  frequency F, in all three zones — restriction/prolongation operators follow
  this map directly (used later when erosion migrates to this domain).
- **Distance-to-front**: maintained incrementally with the paper's overestimation
  update `d(p,t+δt) ← d(p,t) + ‖s(p)‖·δt`, reset at resampling.
- **Determinism**: all randomness from counter-based hashes of
  (seed, plate id / cell id, step) — same rules as the rest of the pipeline.

Productionization gaps in `sample_ico.hpp` (M1 work items): wrap into a class with
`F`, `emit()`/`NeighborRC`, index⇄(row,col,sec,col_rem) conversion; geometry layer
(cell centers on the unit sphere, cell areas, neighbor arc lengths); point→cell
lookup for rasterization; multigrid transfer operators.

Cell-center mapping (verified 2026-07-17, script `verify_warp.py` in session
scratchpad — port as a unit test in M1): warped-gnomonic barycentric map with
per-coordinate cubic warp `f(t) = t + α·t(1−t)(0.5−t)` plus symmetric joint term
`β·u·v·w`, constants α = 0.5372, β = −0.4637, then single normalize.
Measured: sub-triangle spherical area max/min = 1.048 (vs 1.97 unwarped),
CV = 0.011; vertices exact; edge points stay on the great-circle plane and are
u↔v symmetric (machine epsilon) → seamless across faces/sectors; Jacobian
strictly positive (1.22–1.28, no folding). Constants are ≈3% from the swept
optimum (0.540, −0.470) — not worth changing. Inverse map (point→(u,v)) by 2D
Newton from an inverse-gnomonic initial guess: exactly 3 iterations to
|P−Q| < 1e-14 on 500 random samples; implement in double with the analytic
Jacobian. Portability: replace `__builtin_unreachable()` (MSVC), enable
`/Zc:preprocessor` for the variadic macros, qualify `std::array`. Invariant tests:
neighbor symmetry, CW-consecutive neighbors mutually adjacent, cell count 10F²+2,
exactly 12 pentagons.

### Unspecified-in-paper items → chosen defaults (revisit freely)

| Item | Paper status | Default here |
|---|---|---|
| f(d) subduction distance profile | Fig 6, piecewise cubic, peak near front | smooth bump: rises 0→1 over 0.15·r_s, cubic falloff to 0 at r_s = 1800 km |
| z_Γ ridge profile | Fig 9 only | crest z_r = −1 km, exponential decay to z_a = −6 km over ~1000 km; long-term subsidence handled by ε_o dampening + age |
| λ0 (rifting rate) | unspecified | ~1 event per 150 My per Earth-area, exposed param |
| β (fold-dir update weight) | unspecified | small; fold dir mostly matters for amplification, not v1-critical |
| ε (slab pull) | "≪ 1" | 0 in v1 (constant axes), enable in M3 |
| A0 | avg initial plate area | 4πR²/plate_count |

## 4. Downstream coupling (the second half of the failure)

- `uplift_rate_m_per_yr` = tectonic uplift accumulated over the **last ~20 My**
  of the simulation (recent orogeny), not a 250 My average. This is what the
  analytical erosion solver consumes as `u(x)`; magnitude sanity: active belts
  ~0.5–1 mm/yr, matching Tzathas Table 1 (u = 1e-3 m/y).
- Base elevation = rasterized tectonic z + detail noise, with noise amplitude
  modulated by orogeny type/age (mini-amplification; full exemplar/Gabor
  amplification from the paper's Section 5 is out of scope for now).
- Erosion-stage alignment fixes while we're in there (deviations from Tzathas
  found in the current implementation):
  - receiver choice: paper wants *random proportional to elevation drop* among
    lower 4-neighbors (precomputed randomness), not 8-neighbor steepest;
    plus the slope correction `a(x) = k·A^m · δx·‖∇z‖/(z−z_r)` — together these
    remove grid bias the "proper" way;
  - multigrid upsampling: add the ±0.25-cell jitter, and the exponential moving
    average between fixed-point iterations to kill oscillation at small t;
  - hillslope coefficient form `a = k·A^m + (k_h/C)·A^(−h)` (Hack constants
    C = 1.5, h = 0.6) — verify current code matches.

## 5. Milestones and acceptance gates

Each milestone lands behind the existing stage contract, at 1024×512 preview
resolution, with PNG dumps + metrics printed by the CLI. **A milestone is done only
when its gate passes** — the gates encode exactly the failure modes of the dead
experiment.

- **M0 — hygiene.** Archive the failed experiment on a branch
  (`experiment/tectonics-eulerian`), start clean from `main` + kept pieces
  (params constants, noise-as-detail, CLI stage dumps). Add a metrics harness to
  the CLI: ocean fraction, hypsometric histogram, elevation min/max, per-layer
  hash. Gate: green build + tests, deterministic hash at 512×256.
- **M1 — plates that move.** Geodesic grid productionized (see gaps list above)
  with invariant tests; plate partition, rigid rotation, rasterization; crust
  initialized (continental patches at continent_ratio, base elevations + jitter).
  No interactions yet.
  Gate: grid invariant tests green; continents visibly drift — centroid of a
  tracked landmass moves by the analytic prediction (ω·R·T within tolerance) over
  250 My; no polar ring; seams absent at the dateline and at sector boundaries.
- **M2 — subduction + ridges + resampling.** Boundary classification,
  subduction uplift with distance-to-front, oceanic generation at gaps, global
  resampling, per-step erosion/dampening/sediment, oceanic age.
  Gate: oceanic age increases monotonically away from active ridges (no
  re-stamping / onion rings — assert no age inversions above threshold); mountain
  belts hug convergent margins; trenches present offshore.
- **M3 — discrete events.** Continental collision with terrane transfer,
  rifting, slab pull. Gate: two colliding continents suture (single plate id
  region, Himalayan-type belt at the suture); a supercontinent rifts within a
  250 My run at default λ0.
- **M4 — coupled pipeline.** Recent-uplift map into analytical erosion; erosion
  stage alignment fixes; hydrology/masks re-tuned. Gate: dendritic valleys carved
  into the orogens; hypsometric histogram bimodal (ocean mode near −4 km, land
  mode < +500 m); ocean fraction within ±5% of target.
- **M5 — full-res + docs.** 4096×2048 run, performance pass (OpenMP over
  cells; target: full pipeline < 60 s), parameter exposure in CLI, update
  `docs/ALGORITHMS.md` sections 3/4 to describe the Lagrangian model, refresh
  WORKLOG. Gate: seed-1337 full-res render reviewed and archived as the new
  reference output.
- **M6 (post-backbone) — dense physics on the geodesic domain.** Migrate
  analytical erosion + hydrology from the lat-lon raster to a dense geodesic
  grid (F ≈ 700, ~4.9M cells ≈ 9.6 km — matches 4096×2048 equatorial detail at
  ~40% fewer cells, no polar singularity, no cos(lat) anisotropy). Erosion is
  receiver-graph based and D∞ is facet-based, so both generalize; multigrid uses
  the (2r,2c) frequency nesting. Lat-lon becomes export/preview resampling only.
  Gate: parity run vs the lat-lon implementation at matched resolution (same
  seed, comparable hypsometry/river statistics), plus no artifacts along sector
  boundaries or at the 12 pentagons.

## 6. References

- `docs/references/2019-Procedural-Tectonic-Planets.pdf` — Cortial et al., CGF 38(2)
  2019. Full implementation spec extracted (data model Table 1, interactions §4.1–4.5,
  resampling §6, constants Appendix A/Table 3).
- `docs/references/Analytical_Terrains_EG.pdf` — Tzathas et al., CGF 43(2) 2024.
  Analytical SPL erosion (Eqs 1–18), multigrid §5.2, hillslope/thermal §6,
  optimization correction §5.3/Appendix A, defaults Table 1.
