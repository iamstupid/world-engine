// M6 gates for the geodesic dense-physics path: the pipeline with
// physics_grid_frequency > 0 must produce the full layer contract,
// plausible water coverage, a river network, and be deterministic.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "world_engine/terrain/procedural/procedural_generator.h"

int main() {
  world_engine::terrain::procedural::PipelineParams p;
  p.seed = 909;
  p.width = 256;
  p.height = 128;
  p.physics_grid_frequency = 48;
  p.tectonics.grid_frequency = 24;
  p.erosion.multigrid_levels = 3;
  p.erosion.fixed_point_iterations = 4;

  world_engine::terrain::procedural::ProceduralGenerator g1;
  g1.generate(p);
  const auto& ds1 = g1.dataset();

  int failures = 0;
  const std::vector<std::string> required = {
      "elevation_primeval_m", "tectonic_elevation_m", "elevation_base_m",
      "elevation_eroded_m",   "uplift_rate_m_per_yr", "crust_type",
      "flow_accumulation_m2", "river_mask",           "ocean_mask",
      "lake_mask"};
  for (const auto& name : required) {
    if (!ds1.has_layer(name)) {
      std::printf("FAIL: missing layer %s\n", name.c_str());
      ++failures;
    }
  }
  if (failures != 0) {
    return 1;
  }

  const auto& ocean = ds1.u8_layer("ocean_mask");
  const auto& river = ds1.u8_layer("river_mask");
  int ocean_count = 0;
  int river_count = 0;
  for (size_t i = 0; i < ocean.size(); ++i) {
    ocean_count += ocean[i] ? 1 : 0;
    river_count += river[i] ? 1 : 0;
  }
  const double ocean_frac = static_cast<double>(ocean_count) / ocean.size();
  std::printf("ocean fraction %.3f, river pixels %d\n", ocean_frac, river_count);
  if (ocean_frac < 0.2 || ocean_frac > 0.95) {
    std::printf("FAIL: implausible ocean fraction\n");
    ++failures;
  }
  if (river_count < 50) {
    std::printf("FAIL: no meaningful river network\n");
    ++failures;
  }

  // Determinism.
  world_engine::terrain::procedural::ProceduralGenerator g2;
  g2.generate(p);
  if (g1.dataset().deterministic_hash() != g2.dataset().deterministic_hash()) {
    std::printf("FAIL: geodesic physics path is not deterministic\n");
    ++failures;
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("test_geodesic_physics passed\n");
  return 0;
}
