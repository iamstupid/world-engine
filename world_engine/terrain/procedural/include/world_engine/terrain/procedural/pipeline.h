#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural {

class Pipeline {
 public:
  Pipeline() = default;
  TerrainDataset run(const PipelineParams& params);

 private:
  [[nodiscard]] std::string params_digest(const PipelineParams& params) const;
  [[nodiscard]] std::string stage_cache_key(const std::string& stage_name,
                                            const std::string& params_digest,
                                            const std::string& input_hash) const;

  std::unordered_map<std::string, TerrainDataset> stage_cache_;
  std::mutex cache_mutex_;
};

}  // namespace world_engine::terrain::procedural
