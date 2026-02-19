#pragma once

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural::stages {

void run_masks_stage(const PipelineParams& params, TerrainDataset& dataset);

}  // namespace world_engine::terrain::procedural::stages
