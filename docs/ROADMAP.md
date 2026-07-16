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
