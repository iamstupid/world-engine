#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/terrain_dataset.h"

namespace world_engine::terrain::procedural {

// Progress callback: (stage_name, stage_index, stage_count, phase) where
// phase is 0.0 at stage start and 1.0 at stage end.
using ProgressFn = std::function<void(const std::string&, int, int, double)>;

class Pipeline {
 public:
  Pipeline() = default;
  TerrainDataset run(const PipelineParams& params);
  TerrainDataset run(const PipelineParams& params, const ProgressFn& progress,
                     const std::atomic<bool>* cancel);

  struct Cancelled {};  // thrown when the cancel flag is set between stages

 private:
  [[nodiscard]] std::string params_digest(const PipelineParams& params) const;
  [[nodiscard]] std::string stage_cache_key(const std::string& stage_name,
                                            const std::string& params_digest,
                                            const std::string& input_hash) const;

  std::unordered_map<std::string, TerrainDataset> stage_cache_;
  std::mutex cache_mutex_;
};

}  // namespace world_engine::terrain::procedural
