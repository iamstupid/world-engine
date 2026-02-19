#include <iostream>

#include "world_engine/terrain/procedural/procedural_generator.h"

int main() {
  world_engine::terrain::procedural::PipelineParams p;
  p.seed = 2026;
  p.width = 256;
  p.height = 128;
  p.erosion.multigrid_levels = 2;
  p.erosion.fixed_point_iterations = 4;

  world_engine::terrain::procedural::ProceduralGenerator g1;
  world_engine::terrain::procedural::ProceduralGenerator g2;
  g1.generate(p);
  g2.generate(p);

  const auto h1 = g1.dataset().deterministic_hash();
  const auto h2 = g2.dataset().deterministic_hash();
  if (h1 != h2) {
    std::cerr << "Determinism hash mismatch: " << h1 << " vs " << h2 << "\n";
    return 1;
  }
  return 0;
}
