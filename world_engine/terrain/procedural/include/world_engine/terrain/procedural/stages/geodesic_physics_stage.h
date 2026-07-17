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

// Graph-operator variant (DAG pipeline / IGM-ization): z0 and uplift come
// in as geodesic cell fields at their own frequencies (interpolated per
// multigrid level via locate) instead of being sampled from rasters.
// Pass null pointers to fall back to the raster inputs.
void run_geodesic_physics_stage_fields(const PipelineParams& params,
                                       TerrainDataset& dataset,
                                       int z0_freq,
                                       const std::vector<float>* z0_cells,
                                       int up_freq,
                                       const std::vector<float>* up_cells);

}  // namespace world_engine::terrain::procedural::stages
