#pragma once

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural::stages {

void run_tectonics_stage(const PipelineParams& params, TerrainDataset& dataset);
void run_combine_stage(const PipelineParams& params, TerrainDataset& dataset);

}  // namespace world_engine::terrain::procedural::stages
