#include <iostream>
#include <string>
#include <vector>

#include "world_engine/terrain/procedural/procedural_generator.h"

int main() {
  world_engine::terrain::procedural::PipelineParams p;
  p.seed = 77;
  p.width = 256;
  p.height = 128;
  p.erosion.multigrid_levels = 2;
  p.erosion.fixed_point_iterations = 4;

  world_engine::terrain::procedural::ProceduralGenerator g;
  g.generate(p);
  const auto& ds = g.dataset();

  const std::vector<std::string> required = {
      "elevation_primeval_m", "tectonic_elevation_m", "elevation_base_m", "elevation_eroded_m",
      "uplift_rate_m_per_yr", "crust_type", "flow_accumulation_m2", "river_mask", "ocean_mask",
      "lake_mask"};

  for (const auto& name : required) {
    if (!ds.has_layer(name)) {
      std::cerr << "Missing layer: " << name << "\n";
      return 1;
    }
  }

  if (static_cast<int>(ds.float_layer("elevation_eroded_m").size()) != ds.size()) {
    std::cerr << "Elevation layer size mismatch\n";
    return 1;
  }
  if (static_cast<int>(ds.u8_layer("ocean_mask").size()) != ds.size()) {
    std::cerr << "Ocean mask size mismatch\n";
    return 1;
  }
  return 0;
}
