#pragma once

#include <string>
#include <vector>

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/procedural/pipeline.h"
#include "world_engine/terrain/terrain_provider.h"

namespace world_engine::terrain::procedural {

class ProceduralGenerator final : public ITerrainProvider {
 public:
  ProceduralGenerator() = default;

  void generate(const PipelineParams& params);
  [[nodiscard]] const PipelineParams& params() const { return params_; }

  const TerrainDataset& dataset() const override { return dataset_; }
  float sample_layer(const std::string& layer_name, double lat_deg,
                     double lon_deg) const override;
  float sample_elevation(double lat_deg, double lon_deg) const override;
  std::vector<std::string> list_layers() const override;

 private:
  Pipeline pipeline_;
  PipelineParams params_{};
  TerrainDataset dataset_{};
};

}  // namespace world_engine::terrain::procedural
