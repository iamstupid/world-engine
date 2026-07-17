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
- M6: dense physics (erosion/hydrology/masks) on the geodesic grid
  (`--phys-freq`, F rounded to F0*2^(levels-1)); MFD accumulation; jittered
  spherical multigrid prolongation. Parity vs raster path at matched
  resolution: ocean fraction 68.5% vs 68.0%, hypsometric percentiles within
  ~15% on land / ~0.3% in the ocean; no pentagon or sector-boundary
  artifacts. Full scale F=704 (4.96M cells): 4.5 s for the whole physics
  block (vs 8.5 s raster) - the lat-lon raster is now export-only in this
  mode. Gate test: test_geodesic_physics (layers, water coverage, rivers,
  determinism).

---

# Studio & Civilization Sprint (M7-M11)

## Date: 2026-07-17

- M7: Pipeline progress callbacks + cancellation; param_schema X-macro table
  (schema JSON + string-keyed access); GeodesicGrid ring<->rhombus bijection
  (tested dense); TerrainDataset geodesic cell layers; pybind11 module
  (GIL-released generate, CancelFlag, geodesic_graph/atlas_map); FastAPI
  server (sessions, SSE progress, raw layers, point query, paint upload,
  features/entities); .weworld = SQLite+zstd.
- M8: no-build web studio (vanilla JS + vendored three.js): schema-driven
  forms, displaced globe, 2D map, client-side colormaps, save/load.
  Playwright e2e suite with screenshot artifacts (headless chromium +
  swiftshader).
- M9: geodesic climate (insolation/lapse/continentality temperature, zonal
  winds, upwind moisture advection with orographic rain shadows), runoff-
  weighted discharge, Koppen-lite biomes, vegetation; coastline
  naturalization (fractal crust seed, sea-band displacement noise,
  lowstand-erode-then-flood). Multi-basin ocean rule (>=2% components).
- M10: paint editing chain: uplift_paint (erosion uplift) +
  continent_seed_paint applied to the FINAL canonical crust (painting
  targets today's map; t=0 application let plate drift carry paint away).
- M11: civ layer on the geodesic graph (Python/scipy): population potential,
  Poisson-disk settlements with Zipf ranks, culture hearth region growing
  over friction, polity expansion from capitals, syllable namegen per
  culture, vector rivers traced from the flow graph, MST least-cost roads,
  border midpoint chains; entities + GeoJSON features persisted in .weworld
  and rendered as studio overlays.
- Full e2e: 8/8 passing (form, generation, globe/map pixels, layers, query,
  overlays, save/load, paint round trip).

---

# Entity Store & Query Engine (M12-M13 backend)

## Date: 2026-07-17

- worldstore.py (M12): stable-id entities with gen/user attribute layers,
  field/entity locks, validity intervals + eras + custom calendars, edit
  log, and apply_generation merge semantics (user edits and locks survive
  regeneration; absent gen entities retire unless user-touched; stable ids
  from (namespace, kind, gen_key)). Serialized into .weworld
  (store_entities/store_meta tables).
- geoquery.py (M13): GeoIndex over the geodesic cell graph - route()
  travel days by mode (walk/horse/cart/ship; slope/biome/road/river-
  crossing costs) with narrative itineraries and polity transit notes;
  reachable() isochrones; describe() context packs resolving polity/
  culture/nearest settlement through the store (as_of-aware); news_arrival()
  courier-vs-ship information lag; viewshed() horizon + visible peaks.
  Polity/culture identity rides in cell layers as capital/hearth cell ids.
- civ.py now writes cell_polity_capital / cell_culture_hearth / cell_road
  layers and merges all generated entities through the store; settlement
  features render MERGED names (author renames flow to the map).
- Server: /entities (find + as_of), PUT /entities/{id} (edit + lock),
  /route /describe /reachable /news /viewshed endpoints.
- Tests: tests/py 14/14 (store semantics, calendars, merge protection,
  route/describe/news/viewshed gates, end-to-end regen-merge); e2e 8/8
  unchanged.

---

# Sky Model, Border Polylines, Ortho Views

## Date: 2026-07-17

- skymodel.py: seeded star catalog (1400 stars, named constellations),
  rotation/tilt/orbit frames, sky(observer, t) -> sun alt/az + day/twilight/
  night, moon positions/phases/moonlight, visible constellations; eclipse
  scanner (solar/lunar). Endpoints /sky and /eclipses; year length follows
  the world calendar. Tests: determinism, day/night cycle, phase range,
  eclipse detection, diurnal constellation rotation, midnight sun at 85N.
- civ borders upgraded from midpoint dots to proper DUAL-EDGE polylines
  (segments connect centroids of the two lattice triangles sharing each
  boundary edge, chained into LineStrings).
- terrain_cli --ortho-views: orthographic globe snapshots (both poles + two
  equatorial views) with limb shading - human-viewable exports without the
  web studio.
- tests/py: 22 passing.

---

# Full Astronomy Model (absorbs skymodel v1)

## Date: 2026-07-17

- astro.py: seeded Universe -> galaxy of ~5000 star systems with real 3D
  positions (pc): spiral (log-spiral arms + bulge/disc/halo), cluster
  (King profile rejection sampling) and irregular generators; two-tier
  sampling (full-IMF local neighborhood within 70 pc + luminous field
  skeleton). Stellar physics from Kroupa IMF mass: L/R/T via main-sequence
  relations, M_bol -> M_V via Reed-style bolometric correction (sun-like
  check lands at Mv 4.82 vs real 4.83), spectral class, blackbody sRGB
  tint from a committed LUT (tools/scripts/gen_blackbody_lut.py; Wyman 2013
  analytic CIE CMFs - colour-science is numpy-2-only, incompatible with
  our scipy stack).
- Home system: Kepler orbits (Newton solver) for 4-8 planets; home planet
  period == calendar year by construction (a from Kepler III); reflected-
  light apparent magnitudes (Venus-like anchor peaks at -4); geocentric
  conjunction scanning. Moons keep v1 circular model -> phases, eclipse
  scanner; the default 5-deg lunar inclination now produces natural
  eclipse SEASONS (~176 d apart) instead of monthly eclipses.
- Constellations are home-culture-fixed memberships (MST shapes); viewed
  from another system their shapes distort (cross-system parallax: nearest
  system sees ~11x the angular shift for near vs far stars) - the
  interstellar-fiction feature.
- Server: /sky and /eclipses now served by the Observatory (superset
  response: + planets with mags/phase angles, + naked-eye stars with RGB);
  new /astro/spec (PUT overrides, persisted in .weworld meta),
  /astro/galaxy, /astro/system/{idx}, /astro/sky_from?system= (foreign-sky
  parallax view), /astro/events (eclipses + conjunctions). Naked-eye
  systems + home planets/moons register through WorldStore.apply_generation
  so author renames survive regeneration and show in the galaxy map.
- worldstore fix: apply_generation retirement is now scoped to the kinds
  present in the batch - previously a second generator (astro) would have
  retired ALL of civ's entities and vice versa.
- skymodel.py deleted; its 7 behavior gates ported into
  tests/py/test_astro.py (20 gates total: Kepler residual/closure/third
  law, IMF, LUT monotonicity, catalogs, parallax, galaxy morphology,
  store merge isolation). tests/py 35/35, e2e 8/8.

---

# Backend Batch: N=2048 Amplification, Tzathas 5.3, Vector Rivers, Refinement Pyramid, Novel Kit, Astro Leftovers

## Date: 2026-07-17

- Amplification stage (addendum b): when the geodesic physics grid
  outresolves the export raster, cells gain attribute-modulated fractal
  detail evaluated on the sphere (fBm plains + ridged orogens, modulated by
  uplift rate and relief; faint abyssal hills). Octaves are capped per
  multigrid level (~3 cell spacings), so coarse grids behave exactly as
  before. New "amplify" param group; digest/schema updated.
- Big-F validation: F=1024 = 10.5M cells, 25 s wall, 2.0 GB peak;
  **F=2048 = 41.9M cells (~3.9 km), 116 s wall, 8.2 GB peak** on 32 cores
  (parallel stable sorts via __gnu_parallel). N=2048 is practical.
- Tzathas 5.3 optimization-based altitude correction: gradient descent in
  elevation-difference space (d >= 0 preserves the network), river term
  lam_r=1/3 vs discontinuity term lam_d=2/3 over non-connected neighbors,
  gradients accumulated down the receiver tree and divided by upstream
  node count. Pure-gather gradient (no atomics) keeps runs bitwise
  deterministic. erosion.discontinuity_iterations (default 16).
- Rhombus-atlas endpoints (addendum b): atlas.py packs any cell layer into
  the 10 NxN zero-waste layout via weterrain.atlas_map (exact bijection +
  2 pole cells); /cell_layer/{name} raw and /cell_atlas/{name}.
- Vector rivers (addendum c): rivers.py — extraction with hydraulic
  attributes (width from Leopold hydraulic geometry), deterministic
  meander refinement (wavelength ~11 widths, endpoints pinned, bounded
  amplitude), stream-burning into 'elevation_conditioned_m' (monotone bed
  profiles), sub-threshold tributaries that trace the flow tree onto the
  channel network. civ.py now consumes rivers.build();
  minor_river features added.
- M11.5 refinement pyramid: refine.py + /refine endpoint — tiles at
  arbitrary bbox/scale with the 1-px border LOCKED to the global field,
  world-anchored value-noise detail (overlapping tiles agree), local
  priority-flood + D8 + analytical stream-power carving. Gates: boundary
  continuity, determinism, relief gain, pit reduction.
- M14 skeleton: novelkit.py — Project root, story-dated changesets
  (staged -> author merge; preview on a store clone; drop of merged
  refused), characters with location trails (validity intervals), travel
  consistency (route-time violations incl. ship fallback) and news-arrival
  knowledge checks. projects saved as .weproj JSON.
- Astro leftovers: optional wide companion for the home star (second sun
  with Kepler orbit, mag ~ -16 class), proper motion (25 km/s dispersion;
  sky_from(epoch_yr) drifts charts across eras ~0.1-1 deg/millennium for
  nearby stars). Fresh RNG streams keep default catalogs identical.
- Tests: C++ 7/7 (new amplification/5.3 gates at F=224 incl. single-grid
  Fig-4 reproduction), tests/py 51/51 (rivers 6, refine 2, novelkit 6,
  astro +2), e2e 8/8 (param group count 5 -> 6).
