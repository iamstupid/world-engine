# WorldEngine Terrain Algorithms

## 1. Scope and Source Policy

This document records algorithm design and implementation assumptions.

Allowed algorithm sources:

- `docs/references/2019-Procedural-Tectonic-Planets.pdf`
- `docs/references/Analytical_Terrains_EG.pdf`
- other PDFs under `docs/references/` only when needed for supporting components

If a detail is ambiguous or omitted in those papers, we use the simplest deterministic interpretation and list it explicitly here.

## 2. Noise Seed Stage

### 2.1 Purpose

Generate a seamless primeval elevation prior to tectonics.

### 2.2 Method

- Library: FastNoiseLite (`third_party/FastNoiseLite/FastNoiseLite.h`).
- Noise type: `OpenSimplex2`.
- Fractal mode: `fBm`.
- Fractal controls:
  - octaves <- `params.noise.octaves`
  - lacunarity <- `params.noise.lacunarity`
  - gain <- `params.noise.gain`
- Sampling:
  - evaluate 3D noise on unit-sphere coordinates `(x, y, z)` to avoid longitudinal seams
  - scale coordinates by `params.noise.base_frequency`
- Output scaling:
  - multiply normalized noise by `params.noise.amplitude_m` to obtain meters

### 2.3 Assumptions

- Paper references focus on tectonics/erosion, not noise implementation.
- Noise stage is engineering initialization, not a tectonic/erosion scientific model.

## 3. Tectonics Stage (Procedural Tectonic Planets, Lagrangian)

Source:

- `2019-Procedural-Tectonic-Planets.pdf`

### 3.1 Model summary

Crust state lives on the cells of a canonical icosahedral geodesic grid
(`world_engine/terrain/geodesic_grid.h`, frequency F, 10F^2+2 cells; neighbor
indexing from `docs/references/sample_ico.hpp`; cell centers via the
area-equalizing warped-gnomonic map). Plates are cell sets carried by rigid
rotations `s(p) = omega (w x p)` (paper Section 3). Between global resamplings
the crust of plate P sampled at canonical cell i is physically located at
`delta_R_P * center(i)`; every `resample_interval_steps` the crust is
re-projected onto the canonical grid (paper Section 6).

Per-cell attributes (paper Table 1): plate id, crust type, elevation, oceanic
age, orogeny type (Andean/Himalayan) + age, recent-uplift EMA.

### 3.2 Interactions

- Subduction (4.1): convergent boundaries detected on the canonical
  configuration after each resample; overriding side by buoyancy (continental
  over oceanic, younger oceanic over older). Distance-to-front by Dijkstra
  within the overriding plate, limited to r_s. Per step:
  `u_e = u0 * f(d) * g(v) * h(z~)`, `h = z~^2` of the subducting side,
  `g = closing_speed / v0` (normal component; oblique shear produces no
  subduction flux).
- Oceanic crust generation (4.3) at resample: divergence gaps get
  `z = alpha * z_border + (1 - alpha) * z_ridge`,
  `alpha = d_ridge / (d_ridge + d_plate)` from two Dijkstra fields; the ridge
  is the gap skeleton (cells locally farthest from covered cells); age of new
  crust is `alpha * window`.
- Continental collision (4.2): interpenetration recorded during resample
  arbitration per plate pair; event fires when penetration > 300 km. Surge
  `dz = min(cap, delta_c * A) * (1 - (d/r)^2)^2` over
  `r = r_c * sqrt((v/v0)(A/A0))`; the losing terrane sutures onto the winner
  (plate id transfer); orogeny marked Himalayan.
- Rifting (4.4): per plate per resample, `P = lambda e^-lambda`,
  `lambda = lambda0 * (0.25 + 0.75 x_P) * A/A0 * window/100My`; splits into
  2-4 warped-Voronoi fragments with diverging rotations.
- Slab pull (4.1): subducting plates' axes drift toward their fronts,
  `w += eps * sum_k normalize(c x q_k)`.
- Per-step modifications (4.5): continental erosion `-(z/z_c) eps_c dt`,
  oceanic dampening `-(1 - z/z_t) eps_o dt` (trenches emerge: oldest crust
  sinks deepest and is consumed at fronts), trench sediment fill `+eps_t dt`
  below the abyssal reference.

### 3.3 Assumptions and deviations (explicit)

1. `f(d)` exact coefficients are not given (paper Fig 6): we use f(0)=0.6,
   peak 1 at 0.1 r_s, quartic Wendland decay to 0 at r_s (belts hug margins).
2. Ridge template z_G is the constant crest z_r; the alpha blend supplies the
   flank profile and age-driven dampening the long-term subsidence.
3. Collision area A is read as the INTERPENETRATION area, not the whole
   terrane area (delta_c * A with terrane areas reaches tens of km of surge
   for subcontinents; the overlap reading is self-consistent across sizes).
   A per-pair cooldown (3 resamples) and a surge cap regulate sustained
   convergence between large plates until they suture.
4. Continental-continental convergence produces no sustained subduction
   uplift; it resolves as discrete collision events (prevents runaway
   plateaus; the paper's forced subduction is an unmodeled transient).
5. Sub-cell crust transport within a resample window is exact (rigid
   rotation); resampling interpolates barycentrically over the geodesic
   lattice triangle (planar weights).
6. Determinism: all randomness from splitmix64 hashes of
   (seed, entity id, step/resample counter); OpenMP loops are order-free.

## 4. Analytical Erosion Stage (Analytical Terrains)

Source:

- `Analytical_Terrains_EG.pdf`

### 4.1 Key elements used

- Stream power advection `dz/dt = u - k A^m ||grad z||` with n = 1 solved
  analytically along receiver chains (paper Eqs 3-18): fixed-point iteration
  between drainage graph and elevations, multigrid coarse-to-fine.
- Uplift input is the tectonic recent-uplift EMA layer
  (`uplift_rate_m_per_yr`), i.e. active margins and fresh sutures erode
  against real uplift.
- Receiver selection: random among lower neighbors proportional to slope,
  over the 8-neighborhood (paper recommends drop-proportional over 4
  neighbors; the 8-neighbor slope-weighted variant suppresses grid bias
  further and is kept deliberately).
- Single-receiver slope correction (paper 4.3 inset):
  `a(x) = k A^m * dist * ||grad z|| / (z - z_r)`, gradient from per-axis
  downstream differences, clamped [0.5, 2.5].
- Hillslope enters the advection coefficient (paper Eq 26):
  `a += (k_h / C) A^{-h}` with Hack constants C = 1.5, h = 0.6.
- Thermal: post-pass critical-slope clamp along receiver edges (deviation:
  the paper folds it into the interleaved analytical solve; the clamp is the
  stable simplification).
- Fixed-point EMA damping (paper 5.1) between iterations.
- Multigrid prolongation with deterministic +-0.25 cell jitter (paper 5.2).
- Depression handling: priority-flood carving from sea-level outlets before
  receiver construction; outlets pin z = sea level.

### 4.2 Assumptions (explicit)

1. Boundary/outlet mask from `sea_level` with a low-quantile fallback.
2. Ocean bathymetry is restored from the base elevation after erosion (the
   solver only sculpts land).
3. Erosion horizon defaults to 2 My (landscape response time): major channels
   approach steady state while ridges stay young.
4. Optimization-based discontinuity correction (paper 5.3) is not
   implemented; multigrid + EMA handle basin-boundary discontinuities.

### 4.3 Geodesic dense-physics path (M6)

With `physics_grid_frequency > 0` the erosion solver, hydrology and water
masks run on a dense icosahedral geodesic grid instead of the lat-lon raster
(`geodesic_physics_stage`). The receiver-graph algorithms transfer directly:
5/6-neighbor cell graph, great-circle edge lengths, exact cell areas; the
multigrid hierarchy uses the F/2 -> F frequency nesting and jittered
spherical prolongation; hydrology uses multiple-flow-direction accumulation
(slope-proportional over all lower neighbors). Inputs are sampled from the
raster layers bilinearly; results are rasterized back through locate().
This removes the polar singularity and cos(lat) anisotropy entirely; parity
with the raster path at matched resolution is within a few percent on ocean
fraction and hypsometric percentiles (rivers render slightly wider due to
MFD). Gate test: tests/test_geodesic_physics.cpp.

## 5. Hydrology, Rivers, Lakes, Ocean Masks

Primary source:

- `Analytical_Terrains_EG.pdf` (drainage/receiver requirements for erosion loop)

Supporting source in repo (optional for extended flow partitioning):

- `Water Resources Research - 1997 - Tarboton - A new method for the determination of flow directions and upslope areas in...pdf`

Baseline implementation:

- single receiver per cell for erosion-coupled solve (Analytical Terrains stage)
- deterministic depression fill from outlets before receiver selection
- hydrology stage uses D-Infinity flow direction:
  - facet-based direction angle per cell
  - split-flow partition to two adjacent downslope neighbors
  - contributing area accumulated with weighted propagation
- river mask from accumulation threshold
- ocean mask from sea-level threshold + connected component selection of the largest below-sea-level basin
- lake mask for below-sea-level or closed inland depressions not connected to ocean

Assumptions:

- Because Analytical Terrains focuses on erosion solver structure, full lake physics is not fully specified; we implement minimal deterministic basin labeling for visualization/export.
- Erosion stage keeps its paper-consistent single-receiver update loop, while hydrology products use D-Infinity from Tarboton (1997).

## 6. Parameters and Units

Core units:

- elevation: meters
- uplift: meters per year
- time: years
- drainage area: square meters

Core parameters:

- global seed (integer)
- tectonics step count and step duration
- plate count and motion ranges
- erosion coefficients (`k`, `m`, time horizon)
- sea level
- river threshold

## 7. File-Level Citation Rule for Code

Every source file implementing tectonics or analytical erosion includes comments in this form:

```cpp
// Source: 2019-Procedural-Tectonic-Planets.pdf, Section X.Y, Eq (N) / Fig N
// Source: Analytical_Terrains_EG.pdf, Section X.Y, Eq (N) / Fig N
```

When equation numbering is referenced from extracted text as `Eqn. N`, code comments use `Eq (N)` consistently.

## 8. Open Assumption Log

This section is updated when implementation introduces a necessary simplification.

1. Internal raster neighborhood for analytical erosion is 4-connected for deterministic topological ordering and receiver selection.
2. Multigrid schedule starts with V-cycle-like passes and fixed level count; this is parameterized.
3. Tectonic interpolation uses deterministic local weighted interpolation in baseline implementation; spherical triangulation acceleration can be added without changing stage contracts.
