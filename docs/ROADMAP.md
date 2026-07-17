# WorldEngine Roadmap — from terrain kernel to novel-writing suite

Status: proposed 2026-07-17. Follows the completed tectonics rebuild
(TECTONICS_PLAN.md M0–M6). Milestones here continue the numbering (M7+).

The product ladder has four rungs, each independently useful:

1. **Terrain studio** (M7–M10): server + web UI to generate, view, tune and
   edit planets.
2. **Living world** (M11–M12): climate, biomes, hydro upgrades; resources,
   settlements, cultures, polities — all as editable database entities.
3. **Geo database & API** (M13): the world as a queryable, versioned,
   era-aware store that other tools (and LLM prompts) can consume.
4. **Novel suite** (M14+): manuscript editor + character/lore databases +
   timeline + consistency checks, with the world engine embedded.

---

## 1. Server (M7)

**Shape**: C++ kernel behind a stable C ABI, wrapped with pybind11, served by
FastAPI. Python is deliberate: the later rungs (entity store, name
generation, embeddings/RAG, export tooling) live naturally in Python, and the
kernel releases the GIL during generation.

Kernel work needed first:

- `Pipeline::run` progress callback (`std::function<void(stage, fraction)>`)
  replacing the current stdout prints; cancellation flag checked between
  stages and between multigrid levels.
- Parameter schema export: single source of truth generated from `params.h`
  (a small reflection table; emitted as JSON Schema by `we_get_schema()`), so
  the UI form is always in sync.
- Input-override layers (for editing, M10): optional raster inputs consumed
  by stages when present — `continent_seed_paint` (biases the crust-init
  threshold), `uplift_paint` (added to the tectonic uplift), per-plate motion
  overrides (axis/omega given by id). All layers participate in the stage
  cache digest.
- C ABI: `we_session_create/destroy`, `we_set_params_json`,
  `we_set_input_layer`, `we_run_async(stage_mask, progress_cb)`, `we_cancel`,
  `we_get_layer(name, format)`, `we_save(path)` / `we_load(path)`,
  `we_get_schema`.

Server endpoints (REST + WebSocket for progress):

```
POST /v1/sessions                     create (from scratch or from file)
GET  /v1/schema                       parameter JSON Schema
PUT  /v1/sessions/{id}/params         partial update; response: dirty stages
POST /v1/sessions/{id}/generate       {from_stage} -> job id
WS   /v1/jobs/{id}                    progress stream, cancel
GET  /v1/sessions/{id}/layers         names, types, ranges, hashes
GET  /v1/sessions/{id}/layer/{name}   raw f32/u8 (zstd), or PNG preview
GET  /v1/sessions/{id}/features/...   vector features (M11+): GeoJSON
POST /v1/sessions/{id}/save           .weworld container
POST /v1/sessions/{id}/edits/...      paint layers, feature CRUD
```

**World file format** (`.weworld`): a single SQLite database — portable,
transactional, embeddable in the desktop app later. Tables: `meta` (engine
version, seed, params JSON), `layers` (zstd-chunked rasters + geodesic cell
buffers), `features` (vector entities, R*Tree index), `entities` (lore),
`edits` (event log). Determinism note: params+seed regenerate the world on
the same engine version; baked layers make saves stable across versions.

## 2. Web frontend (M8, editing M10)

Stack: TypeScript + React + Three.js (raw WebGL2 shaders where it matters).

- **Globe view**: icosphere or cube-sphere mesh (~100–200k triangles),
  vertex displacement + normal mapping from the elevation texture,
  hypsometric tint / layer overlays composited in the fragment shader with
  client-side LUT recoloring (restyle without re-downloading data).
  Equirect 4096×2048 f32→RG16 textures are ~32 MB and fine for v1; a
  quadtree tile pyramid per cube face is the v2 zoom path.
- **2D map view**: same textures on a plane; this is where painting happens.
- **Picking**: ray–sphere intersection → lat/lon → point query panel
  (elevation, crust, age, plate, later climate/biome/polity).
- **Pipeline panel**: stage graph with per-stage params (schema-driven
  forms), dirty-stage highlighting, "regenerate from stage X" (the stage
  cache already exists server-side).
- **Editing (M10)**: brush painting on the 2D canvas → upload as override
  layers → partial regeneration. Plate inspector: click a plate, drag its
  Euler pole / speed, lock a continent ("keep this landmass, reroll the
  rest" = lock continent_seed_paint in its footprint).
- Load/save: file picker over `.weworld`; thumbnails from the preview PNG.

## 3. Ecosphere (M9)

All fields computed on the geodesic grid (the M6 graph machinery: BFS
distance fields, upwind sweeps, accumulation), rasterized for export.

- **Temperature**: latitude insolation curve, lapse rate by elevation,
  continentality (distance-to-ocean BFS), two seasonal snapshots (tilt ±).
- **Winds**: zonal band model (trades/westerlies/polar easterlies) with
  noise-perturbed band edges; per-cell tangent wind vectors.
- **Precipitation**: moisture advection on the cell graph — ocean cells
  evaporate (T-dependent), moisture advects downwind (10–20 relaxation
  sweeps), precipitates by convection (ITCZ), frontal bands, and orographic
  lift (along-wind elevation gain), leaving rain shadows. (Smith–Barstad
  spectral model is the later upgrade; the graph advection fits our domain
  and is a few hundred lines.)
- **Hydrology upgrade**: discharge = accumulation of (precip − evap) instead
  of raw area (one-line weight change in the MFD pass); endorheic basins
  get evaporation balance → salt lakes; ice caps/glacier mask from T.
- **Biomes**: Köppen-style classification from seasonal T/P → biome_id;
  vegetation density (NPP proxy f(T, P)); soil fertility (floodplain +
  sediment + volcanic proxies — we already carry orogeny type/age).
- **Fauna/"种群"**: not simulated. Per-biome habitat/fauna tag tables plus a
  *sapient population potential* field f(fertility, water, climate comfort,
  coast) that drives M11. Wildlife stays descriptive metadata the author can
  edit — a novel needs "wolves live in these mountains", not a predator-prey
  ODE.

## 4. Civilization layer (M11)

Everything generated here becomes **editable database entities** with stable
ids, not baked pixels.

- **Resources**: arable (fertility × slope × climate), freshwater
  (discharge/lakes), harbors (coastal concavity + shelf), ore (orogeny
  type/age: young arcs → metals, old shields → iron/gems), timber (forest
  biomes), salt/fish/etc. Raster suitability + sampled point deposits.
- **Settlements**: weighted Poisson-disk seeding on population potential;
  bonus for river confluences/mouths, harbors, passes (saddles on the
  drainage-divide network), oases. Size hierarchy: Zipf ranks refined by a
  few gravity-model iterations; capitals, towns, villages.
- **Routes**: least-cost paths (slope, biome friction, river-crossing
  penalty) on a neighbor graph of settlements → road network; sea lanes
  between harbors; fords/bridges/passes emerge as named POI candidates.
- **Cultures**: N hearths region-grown over a friction field (mountains,
  deserts, straits are barriers); a language-family split tree per culture
  drives the **name generator** (syllable/phonology tables) so toponyms and
  person names are culturally coherent.
- **Polities**: capitals expand by control cost = friction + distance decay
  − border-affinity bonuses (rivers, drainage divides, desert gaps — the
  "common geopolitical dividing lines"); produces borders that read like
  real ones. Hierarchy (empire/kingdom/province), disputed marches where
  costs tie, enclaves suppressed. Optional later: a light history sim
  (era-stamped wars/splits/foundings) — v1 is static plausibility.
- **Regeneration vs edits**: every entity carries
  `{gen_fields, user_fields, locked}` + provenance (seed, engine version).
  Re-rolling the civ layer updates unlocked gen_fields only; user edits and
  locked entities survive. This is the contract that makes iteration safe.

## 5. Geographic database & API (M13, schema from M11)

**Entity model**

- `layers`: raster/cell fields (physical truth).
- `features`: typed geometry (point/line/polygon on the sphere) — cities,
  rivers (extracted as polylines with discharge/width), roads, borders,
  regions, POIs; R*Tree spatial index.
- `entities`: lore records (polity, culture, settlement, region, deposit…)
  referencing features; attributes + markdown body + tags.
- **Time**: story-calendar support (custom calendars!) and per-entity /
  per-attribute validity intervals (`valid_from/valid_to` in story time) +
  named eras. Queries take an `as_of` date: borders, city existence, names
  all resolve per era. Implementation: temporal columns + era snapshot
  materialization for the map renderer.

**Query API the novelist actually needs**

- `describe(point|region, as_of)` → structured context pack (terrain,
  climate, biome, nearest features, polity chain, culture, notes) — exactly
  what gets pasted into an LLM prompt or a side panel.
- `route(a, b, mode, as_of)` → path, distance, **travel time** by mode
  (walk/horse/cart/ship), and a narrative itinerary ("crosses the X pass,
  fords the Y"). The single most valuable consistency tool for fiction.
- `reachable(from, days, mode)` → isochrone region.
- `neighbors(polity)`, `border(a, b)`, `contains(point)`.
- `find(name|tag|fulltext|semantic)`; toponym registry with rename history.
- `render(region, style, as_of)` → labeled map export (book inserts).
- CRUD with the lock/provenance rules above; every mutation event-logged
  (undo, sync, and "what changed since" for collaborators).

## 6. Novel suite (M14+)

**Character database**: identity + aliases (per era), demographics
(culture/faction/species refs into the geo DB), appearance, voice/diction
notes, personality, skills, possessions; **relationship graph** (typed,
time-varying edges); **whereabouts timeline** (time interval → place ref);
**knowledge ledger** (who learned what, when — enables "character cites a
fact before learning it" checks); goals/secrets/arcs; scene appearances
(links into the manuscript).

**Other stores**: events (timeline atoms linking characters+places+time),
items, organizations, scenes/chapters metadata.

**Consistency engine** (the payoff of all the structure):

- travel feasibility: whereabouts A→B vs `route()` travel time;
- presence conflicts (two places at once), age/date arithmetic in the custom
  calendar;
- knowledge violations from the ledger;
- era violations (mentioning a city before its founding, dead character
  speaking — all from validity intervals).

**Workflow** (one world, many stories):

1. *Worldbuild*: generate → tune → edit in the studio; civ layer; lock what
   you love.
2. *Curate*: name things, write encyclopedia entries, set eras & calendar.
3. *Plan*: timeline board, character arcs, outline linked to places/dates.
4. *Write*: editor with entity-mention detection; context sidebar shows the
   scene's place (map inset via `describe`), present characters, current
   era; travel calculator inline.
5. *Check*: consistency linter report per chapter.
6. *Export*: manuscript + styled region maps.

**Packaging**: local-first desktop app (Tauri) embedding the same FastAPI
backend and SQLite stores; the web studio is reused as the map/world module.
FTS5 for search; embeddings optional later for semantic recall. LLM assists
(entry drafting, describe-to-prose) are plug-in consumers of the same
`describe`/query API — the API is designed to feed prompts from day one.

---

## Milestone order & rough weight

| M | Scope | Depends on | Weight |
|---|-------|-----------|--------|
| M7 | C ABI + pybind11 + FastAPI (sessions/jobs/layers/save) + progress callbacks + schema export | — | M |
| M8 | Web studio v1: globe + 2D + schema forms + generate/save/load | M7 | M–L |
| M9 | Climate/biomes/hydro-upgrade on geodesic grid | — (parallel to M7/M8) | M |
| M10 | Editing: paint overrides, plate inspector, partial regen | M7+M8 | M |
| M11 | Resources/settlements/cultures/polities/names + feature extraction (river polylines, coastlines) | M9 | L |
| M12 | Entity store with locks/provenance/eras; GeoJSON import/export | M11 schema | M |
| M13 | Query API: describe/route/reachable/render + toponyms + calendars | M12 | M |
| M14+ | Novel suite: characters, timeline, editor, consistency linter, Tauri packaging | M13 | L+ |

Suggested immediate next step: M7 kernel plumbing (progress callback, schema
export, C ABI) — it unblocks both the server and the editing story, and it
is pure engine work in the codebase we just stabilized.

---

## Addendum (2026-07-17b): canonical IGM storage, high-res, topologies, project model

Decisions from design review; these refine M7+ scopes.

### Canonical storage: rhombus-atlas IGM, equirect demoted to export

- 10 rhombi (north k: pole,V_k,W_k,V_k+1; south k: V_k+1,W_k,pole,W_k+1) of
  N x N cells + 2 pole cells = 10N^2+2 exactly: the atlas is dense with zero
  waste. Ring layout (CPU sim / neighbor code) <-> rhombus layout (storage,
  GPU textures, chunking) are two indexings of the same cells; conversion is
  pure index math (add to GeodesicGrid).
- Atlas needs 1-texel gutters per rhombus for filtering (~0.1% at N=2048)
  and separate pole values. Pipeline stages exchange cell buffers directly;
  the equirect raster is produced only by an export stage.
- `.weworld` layers: chunked per rhombus (256^2 chunks), mip pyramid,
  u16 quantization + intra-rhombus prediction + zstd for elevation
  (~20-40 MB/layer at N=2048 vs 168 MB raw f32); virtual-texture streaming
  (view-driven chunk fetch) for the client.

### High-resolution pipeline (target N=2048, ~3.9 km equatorial edge)

- Prefer N=2048 over 2000: power-of-two alignment for multigrid nesting,
  chunks and mips (8x8 chunks of 256^2 per rhombus).
- Variable-precision generation: tectonics at F=500 (large-scale physics has
  no information below ~500 km anyway) -> amplification upsample to 2048
  with detail noise MODULATED BY crust attributes (anisotropic ridged noise
  along fold directions on young orogens, subdued noise on old shields,
  age-banded transform-fault noise on ocean floor) -> dense hydro/erosion at
  full resolution (extrapolated ~40-60 s CPU; GPU compute later).
- Coastline naturalization (three orthogonal fixes):
  1. more octaves + domain warp in the crust seed noise (fractal crust
     boundary);
  2. combine-stage coastline displacement noise, amplitude decaying with
     |z - sea| (fractalizes the sea-level contour only);
  3. drowned-valley flooding order: erode with lowstand outlets (sea -120 m),
     then flood to 0 -> rias, fjords, estuaries, shelf archipelagos.
     Later: coastal-process stage (wave erosion, deltas).

### World topologies

`WorldDomain` interface = cell graph (adjacency/areas/edge lengths/3D
embedding) + planetary context (insolation parameter, gravity direction, sky
model). The M6 migration made erosion/hydrology/masks graph-native, so they
run on any topology; climate advection likewise. Tectonics remains a
sphere-only macro-terrain source; other topologies use authored + noise
macro terrain.

- Sphere: geodesic grid (full pipeline incl. tectonics).
- Plane/"位面": bounded square grid; authored sun path.
- Torus: doubly-wrapped rectangular grid (intrinsically flat, uniform).
- Halo ring: narrow band, one wrap axis; day/night via shadow geometry.
- O'Neill cylinder: circumferential wrap, sun-line lighting, Coriolis-driven
  climate.

Sky model per world: star catalog (procedural + nameable), rotation/orbit
elements, moons, bound to the world calendar. Query
`sky(observer, datetime)` -> sun/moon/planet positions, phases, eclipses,
constellation visibility; renders in the globe viewer and a planetarium
view; astronomical events feed the timeline (festivals, omens, moonlight
conditions). Non-spherical skies are topology-aware (Halo arch, cylinder
opposite-side vista).

### Project model and story-state

- Project = root unit. Children: Worlds[] (domain snapshots + geo DB + sky +
  calendars), shared entity stores, Stories[].
- Canon = base world + ordered changesets committed at story-calendar dates
  (the existing validity-interval machinery); one world serves many stories;
  named branches for AU/parallel canon later (git-for-world-state, linear
  canon first).
- Entity kinds (beyond geography): organizations (sects/guilds/churches/
  houses), power systems (cultivation realm ladders / magic systems / tech
  trees - one DAG-with-rules schema serves all three), items/artifacts with
  custody chains, species/bestiary, languages/naming, religions/pantheons,
  calendars/festivals, laws/customs per culture region, economy (currencies,
  goods, trade routes), information/rumors, event hierarchy, and story-side
  plot devices: foreshadowing tracker (plant/payoff pairs with unresolved
  warnings).

### Agentic workspace

- All AI writes go to STAGING changesets; the author reviews diffs before
  canon merge; AI-generated provenance tags everywhere.
- Tool surface: geo queries (describe/route/reachable/viewshed), entity
  CRUD, timeline, sky, calculators, combat simulator, canon RAG search.
- New novelist-facing queries: `viewshed(point)` ("what can be seen") and
  `news_arrival(event, place)` - earliest information arrival via route()
  travel times per era infrastructure, auto-feeding character knowledge
  ledgers ("能听到的消息" as a consistency check).
- Combat simulation: power-system rules as data + deterministic seeded
  resolver + Monte Carlo win-rate + per-round log for prose adaptation.
- Fact checking: canon RAG + physics estimation tools.

### Milestone impact

- M7 gains: rhombus-atlas cell-buffer persistence in `.weworld`, ring<->
  rhombus index conversion, export-only equirect stage.
- M8 globe renders from the rhombus atlas (virtual texture path).
- M9 adds amplification (attribute-modulated detail upsample) and the three
  coastline fixes.
- M12+ entity schema per the expanded kind list; sky model lands with M13
  queries; agentic workspace is M14+ alongside the editor.

---

## Addendum (2026-07-17c): vector hydrology, refinement pyramid, city maps

### Rivers are vector features, period

Raster resolution can never represent rivers (a 30 m channel is sub-pixel at
any global grid); cartography draws rivers as lines at every scale. Four
layers, the river network being the consistency backbone across all LODs:

1. **Vector extraction** (M11, from existing sim data): trace super-threshold
   channels from the receiver/MFD graphs into directed polyline trees;
   per-vertex hydraulic attributes: discharge Q, width ~ a*Q^0.5, depth ~
   c*Q^0.4, Strahler order, monotone elevation profile. Lakes as polygons;
   knickpoints as waterfall POIs. Globe rendering: anti-aliased vector
   strokes, width by discharge.
2. **Deterministic meander refinement**: per-reach seeded sub-cell shape —
   sinuosity from slope/discharge (oxbows in floodplains, braiding flags);
   identical geometry at every zoom and revisit.
3. **DEM conditioning (stream burning)**: any locally generated raster gets
   the refined river vectors carved in (channel, banks, floodplain) so
   raster terrain and vector hydrology always agree.
4. **On-demand local re-simulation** (new: refinement pyramid): for a region
   of interest (~200x200 km), re-run erosion/hydrology on a tangent-plane
   projection at 100-400 m cells with boundary conditions sampled from the
   global solution (edge elevations pinned, inflow discharges injected);
   deterministic region seeds; tile cache. Sub-threshold tributaries
   generated as constrained space-filling trees per catchment (Genevaux
   2013), each draining into the correct parent reach.

Resolution pyramid: global N=2048 (topology + discharge truth) -> region
tiles 100-400 m (valley morphology) -> city patches 1-10 m (vector-first).

### City maps (M15)

No off-the-shelf library generates cities on given terrain (CityEngine is
commercial; Parish & Muller 2001 L-system roads and Chen 2008 tensor-field
streets are the academic basis). We compose our own generator; mature parts
we do adopt: CGAL (straight skeleton parceling), Clipper2 (offsets/booleans),
shapely/GEOS on the Python side, MVT tiles + MapLibre GL for 2D city map
rendering, GeoPackage export for QGIS interop.

Pipeline: site model (region-tile terrain + river vectors: fords, bridges,
quays; M11 roads define the gates) -> macro structure by culture/era
(organic vs planned vs radial; wall rings per era = city growth history via
the existing era machinery) -> tensor-field street synthesis (coast/river
tangents, contour-following on slopes, radial cores, grid districts) ->
blocks from street-graph faces, straight-skeleton/OBB parceling, land use by
accessibility+slope+rules (market near gates/harbor, elites upwind/uphill,
industry downstream) -> building footprints + landmark grammar -> all
semantics as entities (districts, named streets, POIs: inns, gates, bridges,
wells) -> earthworks write-back (cut-and-fill terracing, embankments) so the
local DEM and the city co-adapt.

### Milestone impact

- M9/M11 gain vector hydrology (extraction + refinement + conditioning).
- New M11.5: refinement pyramid (region tiles, boundary-conditioned local
  re-simulation, tributary synthesis).
- New M15: city map generation (after civ layer + refinement pyramid).

---

## Addendum (2026-07-17d): era-styled city generation

### Literature map (no turnkey paper; three usable bodies)

1. Grammar line (style = rule corpus): Parish & Muller 2001 (L-system
   roads); Muller et al. 2006 CGA shape grammars (CityEngine); proven on
   historic reconstruction (procedural Pompeii, Rome Reborn); Vanegas et
   al. 2012 parcel generation.
2. Growth/time line (era = time slice of growth): Weber et al. 2009
   "Interactive Geometric Simulation of 4D Cities" (closest prior art);
   Emilien et al. 2012 village growth on terrain (pre-industrial forms);
   agent-based land use (Lechner et al.); Venice Time Machine methodology.
3. Urban morphology (the actual per-era parameters): Kostof (organic /
   grid / baroque typology), Conzen school (burgage plot modules 6-10 m,
   wall rings, fringe belts), Hakim (Islamic medina rules), and for Chinese
   settings the lifang ward system -> Song street-market transition ->
   courtyard modules — a ready-made style-pack sequence.
   ML line (Neural Turtle Graphics 2019, CityDreamer 2024) not the main
   path (weak style control, no semantic vectors), but inverse fitting of
   style-pack parameters from real old-town OSM data (Vanegas 2012 inverse
   design) is how packs get calibrated.

### Model: palimpsest growth replay

City = growth simulation replayed through the era sequence, one style pack
per era. Old street skeletons persist across eras; wall rings, old town vs
new districts, widened streets, abandoned quarters (declining eras) all
emerge. `city_map(as_of=era)` = replay slice — plugs directly into the
existing era/validity machinery.

### Architecture: three exposure layers

1. Engine (C++, not scriptable): site model, tensor-field solver,
   street-graph/parcel geometry (CGAL/Clipper2), growth replay, entity
   persistence.
2. Style packs (declarative JSON/TOML): street pattern weights, block and
   parcel modules, land-use rule tables, wall policy, building-grammar
   selection, naming culture refs. Most era differences live HERE — data is
   validatable, interpolatable between eras, and safely LLM-generatable.
3. Script hooks (user's "mount JS"): embedded QuickJS sandbox (seeded RNG
   injected; no Date/Math.random/IO; pure functions), plus a WASM channel
   and server-side Python plugins. Hook surface: score_site,
   tensor_contrib(pos), street_step_rule, parcel_split_policy(block),
   assign_land_use(parcel), building_grammar(parcel), decorate_poi.
   Precedent: CGA is itself a DSL — "style as script" is industry-validated;
   we host generic JS/WASM instead of inventing a DSL.

Style pack + optional hooks = the community mod format ("Song-dynasty water
town pack", "steampunk industrial pack").

---

## Addendum (2026-07-17e): full astronomy model — IMPLEMENTED

Worlds attach to a specific star. `tools/terrain_server/astro.py` owns the
hierarchy **Galaxy → StarSystem (3D position, pc) → Star (mass-derived
physics) → Planet (Kepler elements, AU) → Moon**, replacing skymodel v1
(its behavior gates live on in `tests/py/test_astro.py`).

### What is in

- **Galaxy generators**: spiral (log-spiral arms, exponential disc, bulge,
  halo), cluster (King profile), irregular (Gaussian blobs). Two-tier
  sampling: full-IMF local neighborhood (2400 systems / 70 pc) around the
  home system + a luminous field skeleton (2600) tracing galaxy shape.
- **Stellar physics**: Kroupa IMF → mass → L (piecewise M-L), R, T_eff,
  M_bol → M_V (Reed BC), spectral class, binaries as luminosity-summed
  companions. Colors: blackbody T → sRGB LUT committed at
  `tools/terrain_server/data/blackbody_lut.json` (generator script uses
  analytic Wyman-2013 CIE CMFs; anchors match published blackbody tables
  to a few 8-bit units).
- **Home system**: Kepler two-body orbits (Newton solver), home planet
  period locked to the world calendar year, planet types split at the
  snow line, reflected-light magnitudes (validated Venus anchor ≈ −4),
  moons with phases; eclipse scanning inherits v1 semantics and the
  inclined default moon yields realistic eclipse seasons; planet-planet
  conjunction scanning.
- **Observer-dependent skies**: constellations are home-culture-fixed
  memberships whose shapes recompute per observer system —
  `/astro/sky_from?system=N` returns the distorted foreign sky
  (cross-system parallax, ~11× near/far angular-shift ratio at the
  nearest neighbor).
- **Server**: `/sky`, `/eclipses` (Observatory-backed, superset shape),
  `PUT /astro/spec` (overrides, persisted in `.weworld` meta),
  `/astro/galaxy`, `/astro/system/{idx}`, `/astro/events`. Naked-eye
  systems and home bodies are WorldStore entities (kinds `star_system` /
  `planet` / `moon`) — renames survive regeneration.
- **Store contract fix**: `apply_generation` retirement is scoped to the
  batch's kinds so independent generators (civ / astro) cannot retire
  each other's entities.

### Deferred

- Companion-star orbits & multi-star home systems (lighting/eclipse
  effects on the home sky).
- Proper motion over story time; galactic band rendering (client-side
  from `/astro/galaxy`).
- Star-chart UI (web-side; consumes `/astro/galaxy` + `/astro/sky_from`).
