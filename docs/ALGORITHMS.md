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

## 3. Tectonics Stage (Procedural Tectonic Planets)

Source:

- `2019-Procedural-Tectonic-Planets.pdf`

### 3.1 Key Paper Elements Used

- Plate representation as spherical triangulation with attributes and barycentric interpolation.
- Plate motion as rigid geodetic rotation.
- Surface speed relation on sphere:
  - `s(p) = omega * (w x p)` (paper text in model description).
- Plate initialization from centroid partition (Voronoi-style) and optional warped boundaries.
- Tectonic interaction families:
  - subduction
  - continental collision
  - oceanic crust generation at diverging boundaries
  - rifting events
- Near-uniform sphere sampling via Fibonacci distribution (implementation section).

### 3.2 Implementation Mapping

- Domain sampling:
  - generate spherical sample points
  - assign initial plate IDs by nearest centroid/geodesic distance
- Distance warping:
  - add deterministic low-frequency warped score terms per plate to avoid rigid Voronoi borders
- Plate motion:
  - each plate stores axis `w` and angular speed `omega`
  - update per-step velocity field using rigid-rotation expression
  - evaluate interactions over multiple time samples across total tectonic duration
- Boundary classification:
  - derive top-2 plate influence per cell from warped scores
  - compute relative motion along local boundary tangent from those two plates
  - classify convergent/divergent behavior and accumulate interaction
- Elevation/uplift update:
  - apply paper-inspired transfer profile factors for subduction uplift
  - apply collision uplift on convergent continental contacts
  - apply ridge uplift and oceanic crust renewal on divergent oceanic contacts
- Interpolation:
  - tectonic interaction is simulated on a coarse spherical raster and upsampled to target grid
  - additional smoothing suppresses plate-cell imprint artifacts

### 3.3 Assumptions (Explicit)

- The paper is phenomenological and does not define one unique discretization for all operators.
- When exact transfer-function constants are not uniquely specified, we use normalized monotone curves with exposed parameters.
- We implement deterministic coarse-grid time sampling for tectonic evolution to keep full-resolution runtime practical.
- Rifting trigger defaults to parameterized deterministic probability per step from seed and plate state.
- Default tectonic duration is `250 My` (`simulation_steps=250`, `dt_myr=1`), matching the requested long-duration setting.

## 4. Analytical Erosion Stage (Analytical Terrains)

Source:

- `Analytical_Terrains_EG.pdf`

### 4.1 Key Paper Elements Used

- Stream power PDE with uplift and drainage area.
- Analytical treatment in `n = 1` case.
- Method of characteristics and recursive evaluation strategy (paper equations in Section 4).
- Fixed-point alternation between:
  - drainage/receiver graph
  - elevation update from analytical formulas
- Multigrid-inspired coarse-to-fine acceleration (paper Section 5.2).
- Receiver strategy recommendation:
  - random choice among lower neighbors to reduce axis-aligned artifacts (paper discussion and results section).
- Hillslope and thermal extensions in Section 6:
  - hillslope influence through modified local coefficients
  - thermal contribution under critical-slope condition

### 4.2 Implementation Mapping

- Build receiver map from current elevation and boundary mask.
- Compute drainage accumulation in topological order.
- Evaluate analytical update along receiver chains using recursive cached terms.
- Repeat fixed-point iterations until convergence threshold or max iterations.
- Run multigrid schedule:
  - coarse solve
  - upsample
  - fine correction

Optional extensions:

- hillslope coefficient term enabled by parameter
- thermal term enabled when local slope exceeds critical threshold

### 4.3 Assumptions (Explicit)

- If analytical recursion becomes numerically unstable near boundary singular cases, clamp to boundary-consistent fallback.
- If random receiver tie-breaks exist, random values are pre-generated deterministically from global seed and cell index.
- We use 4-neighborhood receivers on raster for baseline; this matches paper implementation framing on regular grids.
- Boundary/outlet mask for analytical erosion is built from `sea_level` with a low-quantile fallback to guarantee at least one outlet set.
- Before receiver construction, we run a deterministic depression-fill step from outlets to avoid fragmented sink basins and guarantee outlet-connected flow paths.
- Convergence stop criterion defaults to max absolute elevation delta below epsilon.
- Optimization-based discontinuity correction is optional and initially omitted from MVP unless explicitly enabled.

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
