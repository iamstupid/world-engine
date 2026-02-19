# WorldEngine Terrain Architecture

## 1. Scope

This document defines system architecture only:

- module boundaries
- interfaces
- data flow
- caching and determinism
- build/runtime components (CLI, server, UI)

Algorithm equations and implementation assumptions are documented in `docs/ALGORITHMS.md`.

## 2. Constraints

- Heavy terrain generation compute runs in C++.
- Single-seed determinism across runs and thread counts.
- Seamless spherical world output with at least `4096x2048`.
- Terrain interfaces live under `world_engine/terrain`.
- Procedural implementation lives under `world_engine/terrain/procedural`.

## 3. Repository Structure

```text
world_engine/
  terrain/
    include/world_engine/terrain/
      terrain_types.h
      terrain_domain.h
      terrain_dataset.h
      terrain_provider.h
    src/
  terrain/procedural/
    include/world_engine/terrain/procedural/
      params.h
      pipeline.h
      procedural_generator.h
      stages/
        noise_stage.h
        tectonics_stage.h
        analytical_erosion_stage.h
        hydrology_stage.h
        masks_stage.h
        export_stage.h
    src/
tools/
  terrain_cli/
  terrain_server/
ui/
  terrain_editor/
docs/
  ARCHITECTURE.md
  ALGORITHMS.md
  API.md
```

## 4. Core Interfaces

### 4.1 `TerrainDomain`

Responsibilities:

- define spherical grid geometry and world radius
- convert `lat/lon <-> xyz`
- provide neighbor and wrap behavior on global raster
- provide area/metric helpers per cell

Primary domain:

- equirectangular raster (`width x height`) with spherical metric correction
- periodic longitude, clamped latitude poles

### 4.2 `TerrainDataset`

Responsibilities:

- store typed named layers with shared dimensions
- expose metadata and provenance
- provide deterministic layer hashing for tests/cache keys

Core layers:

- elevation (`primeval/base/eroded`)
- uplift
- flow accumulation and receiver/direction fields
- ocean, river, lake masks

### 4.3 `ITerrainProvider`

Responsibilities:

- sample by coordinate (`sample_layer`, `sample_elevation`)
- list layers and metadata
- return tiles or full-layer exports

Implemented by `procedural::ProceduralGenerator`.

## 5. Pipeline Architecture

The procedural generator is a stage DAG with hash-addressed results.

Stage key:

`hash(stage_name, stage_version, seed, stage_params, input_hashes)`

Stages:

1. Noise seed.
2. Tectonics.
3. Base elevation combine.
4. Analytical erosion.
5. Hydrology products.
6. Water masks.
7. Export and tiling.

Each stage has:

- declared input layers
- declared output layers
- deterministic implementation
- optional cache read/write

## 6. Caching and Incremental Rebuild

Two-level cache:

- memory cache for active process/session
- disk cache for persistent re-use by parameter hash

Invalidation:

- parameter changes invalidate only dependent stages
- unchanged upstream stages are reused

## 7. Determinism Model

- global 32-bit seed controls all random branches
- per-stage random streams derived from hash(seed, stage_id, index)
- fixed iteration orders and deterministic reductions
- no atomics in order-sensitive reductions

## 8. Runtime Components

### 8.1 C++ Library

Contains domain, dataset, interfaces, and procedural pipeline.

### 8.2 CLI (`tools/terrain_cli`)

Functions:

- generate world from parameter JSON + seed
- export elevation/masks
- report summary stats

### 8.3 Python Module + FastAPI (`tools/terrain_server`)

Functions:

- session creation and parameter updates
- stage execution and status
- full image and tile serving
- GeoJSON river serving

### 8.4 Web UI (`ui/terrain_editor`)

Functions:

- 2D equirectangular viewer with overlays
- layer switching and diagnostics
- parameter form from backend schema
- regeneration workflow

## 9. API and Data Contracts

Server contracts are specified in `docs/API.md` and are centered on:

- session lifecycle
- parameter schema + update
- build trigger + status
- layer/tile retrieval
- export retrieval

## 10. Testing Strategy

Unit tests:

- coordinate conversion invariants
- deterministic output hash on low resolution
- layer presence and type checks

Integration tests:

- CLI generation/export smoke
- server session build + tile fetch smoke
