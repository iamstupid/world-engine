#pragma once

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural::stages {

// Dense-physics path (M6): analytical erosion, hydrology and water masks
// computed on an icosahedral geodesic grid (no polar singularity, no
// cos(lat) anisotropy); the lat-lon raster is used only to sample stage
// inputs and to export the results. Active when
// params.physics_grid_frequency > 0; replaces the raster erosion, hydrology
// and masks stages.
void run_geodesic_physics_stage(const PipelineParams& params, TerrainDataset& dataset);

}  // namespace world_engine::terrain::procedural::stages
