#include "world_engine/terrain/procedural/pipeline.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "world_engine/terrain/procedural/stages/analytical_erosion_stage.h"
#include "world_engine/terrain/procedural/stages/hydrology_stage.h"
#include "world_engine/terrain/procedural/stages/masks_stage.h"
#include "world_engine/terrain/procedural/stages/noise_stage.h"
#include "world_engine/terrain/procedural/stages/tectonics_stage.h"

namespace world_engine::terrain::procedural {
namespace {
uint64_t fnv1a64(std::string_view text) {
  uint64_t h = 1469598103934665603ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  for (char c : text) {
    h ^= static_cast<uint8_t>(c);
    h *= kPrime;
  }
  return h;
}

std::string hex64(uint64_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << value;
  return oss.str();
}
}  // namespace

TerrainDataset Pipeline::run(const PipelineParams& params) {
  TerrainDataset dataset(TerrainDomain(params.width, params.height, params.radius_m));
  dataset.set_seed(params.seed);

  const std::string digest = params_digest(params);
  int stage_idx = 0;
  const int stage_total = 6;
  const auto run_stage = [&](const std::string& stage_name, const auto& stage_fn) {
    ++stage_idx;
    std::cout << "[Progress " << stage_idx << "/" << stage_total << "] " << stage_name << " started\n";
    const auto t0 = std::chrono::steady_clock::now();

    const std::string input_hash = dataset.deterministic_hash();
    const std::string key = stage_cache_key(stage_name, digest, input_hash);
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = stage_cache_.find(key);
      if (it != stage_cache_.end()) {
        dataset = it->second;
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "[Progress " << stage_idx << "/" << stage_total << "] " << stage_name
                  << " cache-hit (" << ms << " ms)\n";
        return;
      }
    }

    stage_fn(params, dataset);

    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      stage_cache_[key] = dataset;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Progress " << stage_idx << "/" << stage_total << "] " << stage_name << " done (" << ms
              << " ms)\n";
  };

  run_stage("noise", stages::run_noise_stage);
  run_stage("tectonics", stages::run_tectonics_stage);
  run_stage("combine", stages::run_combine_stage);
  run_stage("analytical_erosion", stages::run_analytical_erosion_stage);
  run_stage("hydrology", stages::run_hydrology_stage);
  run_stage("masks", stages::run_masks_stage);
  return dataset;
}

std::string Pipeline::params_digest(const PipelineParams& params) const {
  std::ostringstream oss;
  oss << params.seed << '|';
  oss << params.width << 'x' << params.height << '|';
  oss << params.radius_m << '|';
  oss << params.noise.base_frequency << ',' << params.noise.octaves << ','
      << params.noise.lacunarity << ',' << params.noise.gain << ','
      << params.noise.amplitude_m << '|';
  oss << params.tectonics.plate_count << ',' << params.tectonics.simulation_steps << ','
      << params.tectonics.dt_myr << ',' << params.tectonics.boundary_width_px
      << ',' << params.tectonics.uplift_scale_m << ',' << params.tectonics.ridge_scale_m
      << ',' << params.tectonics.tectonic_mix
      << '|';
  oss << params.erosion.fixed_point_iterations << ',' << params.erosion.multigrid_levels
      << ',' << params.erosion.k << ',' << params.erosion.m << ','
      << params.erosion.time_years << ',' << params.erosion.enable_hillslope << ','
      << params.erosion.hillslope_k << ',' << params.erosion.enable_thermal << ','
      << params.erosion.thermal_critical_slope << '|';
  oss << params.hydrology.river_area_threshold_m2 << ',' << params.hydrology.sea_level_m;
  return hex64(fnv1a64(oss.str()));
}

std::string Pipeline::stage_cache_key(const std::string& stage_name,
                                      const std::string& params_digest,
                                      const std::string& input_hash) const {
  const std::string token = stage_name + "|" + params_digest + "|" + input_hash;
  return stage_name + ":" + hex64(fnv1a64(token));
}

}  // namespace world_engine::terrain::procedural
