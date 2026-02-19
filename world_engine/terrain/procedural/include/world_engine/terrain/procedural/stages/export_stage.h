#pragma once

#include <string>

#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural::stages {

bool export_layer_as_pgm16(const TerrainDataset& dataset, const std::string& layer_name,
                           const std::string& path);
bool export_layer_as_pgm8(const TerrainDataset& dataset, const std::string& layer_name,
                          const std::string& path);

}  // namespace world_engine::terrain::procedural::stages
