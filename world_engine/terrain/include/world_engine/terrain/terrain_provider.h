#pragma once

#include <string>
#include <vector>

#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain {

class ITerrainProvider {
 public:
  virtual ~ITerrainProvider() = default;

  virtual const TerrainDataset& dataset() const = 0;
  virtual float sample_layer(const std::string& layer_name, double lat_deg,
                             double lon_deg) const = 0;
  virtual float sample_elevation(double lat_deg, double lon_deg) const = 0;
  virtual std::vector<std::string> list_layers() const = 0;
};

}  // namespace world_engine::terrain
