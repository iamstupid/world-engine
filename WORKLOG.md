# WorldEngine Terrain Audit & Improvement Log

## Date: 2026-02-20

## Environment
- Platform: Windows 10 IoT Enterprise LTSC 2021 (win32)
- Build system: CMake 3.31.6 + MSVC
- C++ Standard: C++20
- Resolution target: 4096x2048
- Default seed: 1337

## Issues Identified

### User-Reported Issues
1. **Rivers align too rigidly to grid** - Cardinal direction artifacts in river channels
2. **Rivers erode too far below sea level** - River valleys cut far below sea level
3. **Too many mountains** - Excessive mountain coverage, unrealistic elevation distribution

### Issues Found During Visual Inspection
4. **No visible river valleys in elevation map** - Erosion doesn't create distinct valley features
5. **Sparse/thin river network** - Rivers barely visible at full resolution
6. **Lack of coastal plains** - Abrupt land-to-mountain transitions, no lowland areas
7. **Non-bimodal elevation distribution** - Real Earth has peaks at -4km (ocean) and ~100m (land); current output is uniformly spread
8. **Performance is slow** - No parallelization (OpenMP), all loops single-threaded

## Root Cause Analysis

### Issue 1: Grid-aligned rivers
- `build_receivers()` in analytical_erosion_stage.cpp uses only **4-connected neighbors** (N/S/E/W)
- Priority flood filling also uses 4-connected neighbors
- Rivers can only flow in 4 cardinal directions, creating staircase patterns
- **Fix**: Add 8-connected neighbors (diagonals) with proper distance weighting

### Issue 2: Below-sea-level erosion
- Outlet cells are set to `z0[idx]` (ocean floor depth, e.g. -3000m)
- Land cells adjacent to outlets compute `target = z_new[r] + uplift*dist/a`
- When receiver `r` is ocean floor (-3000m), target becomes deeply negative
- `lambda = exp(-a*dt/dist)` → 0 for large drainage, so `z_new ≈ target ≈ -3000m`
- **Fix**: Use `sea_level_m` as effective outlet elevation in erosion solver

### Issue 3: Too many mountains
- Noise amplitude = 1500m creates broad high-elevation areas
- Tectonic uplift_scale = 2200m with boundary_width = 6px creates wide mountain zones
- No hypsometric adjustment (real Earth: bimodal distribution with lowland peak)
- No continental shelf or elevation bias
- **Fix**: Reduce amplitude, add hypsometric curve, narrow tectonic belts, add ocean bias

### Issue 8: Performance
- No OpenMP pragmas on any loops
- Noise evaluation, receiver building, hillslope all embarrassingly parallel
- **Fix**: Add OpenMP parallel for to independent loops

## Planned Changes

### Phase 1: Core Algorithm Fixes
- [ ] 8-neighbor connectivity in erosion `build_receivers()` and priority flood
- [ ] Sea level floor for outlet cells in erosion solver
- [ ] 8-neighbor priority flood in hydrology stage

### Phase 2: Parameter & Distribution Tuning
- [ ] Noise stage: domain warping, hypsometric curve, ocean bias
- [ ] Tectonics: narrower boundaries, reduced uplift, continent-ratio control
- [ ] Default parameter adjustments in params.h

### Phase 3: Output & Performance
- [ ] CLI: output intermediate stage maps (primeval, tectonic, base, eroded)
- [ ] OpenMP parallelization of key loops
- [ ] CMake: find and link OpenMP

### Phase 4: Iteration & Verification
- [ ] Build, run at preview resolution (1024x512) for quick iteration
- [ ] Visual inspection of intermediate outputs
- [ ] Tune parameters until naturalistic
- [ ] Final full-resolution (4096x2048) run and verification

## Changes Made

### analytical_erosion_stage.cpp — Complete Rewrite
- **8-neighbor connectivity**: Added `get_8_neighbors()` and `neighbor_distance_m()` helpers. Priority flood and receiver building now use all 8 directions with proper diagonal distance (sqrt(ew²+ns²)).
- **Slope-weighted receiver selection**: Steepest slope (dz/distance) wins among 8 neighbors, breaking grid bias.
- **Sea-level clamping**: Outlet cells use `sea_level_m` (not ocean floor depth). Land cells clamped to `sea_level_m` minimum. Ocean bathymetry restored from base after erosion.
- **OpenMP**: Parallel for on receiver building and hillslope diffusion.

### noise_stage.cpp — Complete Rewrite
- **Domain warping**: 3-axis offset using separate noise for organic coastlines.
- **Multi-layer blending**: continent (0.45) + main fBm (0.40) + ridged (0.15 * elevation mask).
- **Hypsometric curve**: Asymmetric power curves — ocean: t^0.55 (flat abyssal plains), land: t^1.5 (lowland-dominated with tall peaks).
- **Ocean bias**: -0.08 shift for ~56-60% ocean coverage.
- **OpenMP**: Parallel for on outer y loop.

### tectonics_stage.cpp — Modified
- Sharper boundary detection (gap factor 18→35).
- Reduced divergent boundary effect (-0.55→-0.35).
- Fewer blur passes (boundary_width*4→*2 coarse, *6→*3 full).
- Signed sqrt response curve instead of tanh (preserves narrow peak shape).
- Continental shelf: quadratic ramps for land near sea level (0-200m) and shallow ocean (0 to -300m).

### params.h — Tuned Defaults
- noise: base_frequency 0.9→0.8, amplitude 1500→5000m
- tectonics: plate_count 18→14, boundary_width 6→5, uplift_scale 2200→5000, ridge_scale 1400→2000, tectonic_mix 0.32→0.50
- erosion: k 1e-6→1.2e-6, m 0.5→0.45, time_years 200000→200000, hillslope_k 0.025→0.02
- hydrology: river_threshold 3.5e10→1.5e10

### CMakeLists.txt (procedural) — OpenMP Support
- Added `find_package(OpenMP)` and conditional linking.

### main.cpp (terrain_cli) — Enhanced Output
- Intermediate stage output: primeval, tectonic, base (color + grayscale).
- Absolute elevation color ramp: ocean gradient (0→-6000m), land bands (green→yellow→orange→red→purple→white).

## Iteration History

| Run | Resolution | Elev Range (m) | Ocean % | Notes |
|-----|-----------|----------------|---------|-------|
| v2 | 1024x512 | -1110 to 851 | 59% | Range too compressed, hypsometric too aggressive |
| v3 | 1024x512 | -2699 to 1224 | 69% | Better range, mountains still too low |
| v4 | 1024x512 | -3160 to 1692 | 61% | Good lowlands, decent mountains, nice rivers |
| v5 | 1024x512 | -4514 to 2827 | 58% | Peaks near 3000m, good distribution |
| **Final** | **4096x2048** | **-4603 to 3007** | **56%** | **Peaks >3km, natural rivers, good coverage** |

## Final Results (4096x2048, seed 1337)

- **Elevation**: -4603m to 3007m (realistic Earth-like range)
- **Ocean coverage**: 55.9% (Earth is 71%; reasonable for a fictional world)
- **Performance**: Full pipeline in ~10 seconds (was minutes before OpenMP)
- **Rivers**: Natural dendritic patterns, no grid alignment
- **Mountains**: Concentrated along tectonic boundaries, lowlands dominant
- **Output**: 11 PNG files in `output_fullres/`

### Original Issues — Resolution Status
1. **Grid-aligned rivers** — FIXED (8-neighbor connectivity + slope-weighted selection)
2. **Below-sea-level erosion** — FIXED (sea-level clamping at outlets + land cells)
3. **Too many mountains** — FIXED (hypsometric curve + narrower tectonic belts + continental shelf)

---

# Lagrangian Tectonics Rebuild (M0-M5)

## Date: 2026-07-17

The Eulerian per-cell tectonics experiment (archived on branch
`experiment/tectonics-eulerian`) was replaced by a Lagrangian implementation
of Cortial et al. 2019 on an icosahedral geodesic grid. Plan and rationale:
`docs/TECTONICS_PLAN.md`; algorithm mapping: `docs/ALGORITHMS.md`.

## Milestones landed

- M0: metrics harness in the CLI (area-weighted ocean fraction, hypsometric
  percentiles/histogram, per-layer FNV hashes -> metrics.txt).
- M1: `GeodesicGrid` (ring-layout neighbor indexing, warped-gnomonic
  geometry, Newton locate) + invariant tests; crust transported by rigid
  plate rotations with periodic global resampling. Gates: continental drift
  measured, area conserved, no polar artifacts.
- M2: subduction uplift with distance-to-front Dijkstra, ridge generation in
  divergence gaps, age-driven ocean floor. Gate: ocean age/depth correlation
  -0.93...-0.97 (the quantitative "no re-stamping" check).
- M3: continental collision with terrane suturing, rifting, slab pull.
  Wilson-cycle behavior observed (supercontinent assembly then breakup).
- M4: erosion consumes the real recent-uplift EMA; Tzathas alignment (slope
  correction, hillslope in a(s), fixed-point EMA, jittered multigrid).
- M5: 4096x2048 reference run (seed 1337, --tect-freq 250) in ~23 s wall on
  32 threads (WSL2/gcc); bimodal hypsometry; ocean 74%.

## Verification

- `ctest`: 6 suites including test_geodesic (grid invariants) and
  test_tectonics (drift, conservation, age-depth correlation gates).
- Reference outputs in `output_reference/` (gitignored); metrics.txt captures
  layer hashes for regression comparison.
