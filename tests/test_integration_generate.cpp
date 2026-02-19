#include <filesystem>
#include <fstream>
#include <iostream>

#include "world_engine/terrain/procedural/procedural_generator.h"

namespace {
bool write_marker(const std::filesystem::path& p, const std::string& s) {
  std::ofstream out(p, std::ios::binary);
  if (!out) {
    return false;
  }
  out << s;
  return true;
}
}  // namespace

int main() {
  world_engine::terrain::procedural::PipelineParams p;
  p.seed = 999;
  p.width = 192;
  p.height = 96;
  p.erosion.multigrid_levels = 2;
  p.erosion.fixed_point_iterations = 3;

  world_engine::terrain::procedural::ProceduralGenerator g;
  g.generate(p);

  const auto tmp = std::filesystem::path("test_output");
  std::filesystem::create_directories(tmp);
  const auto f1 = tmp / "world.hash";
  const auto f2 = tmp / "layers.txt";

  const bool ok1 = write_marker(f1, g.dataset().deterministic_hash());
  std::string layers;
  for (const auto& name : g.list_layers()) {
    layers += name + "\n";
  }
  const bool ok2 = write_marker(f2, layers);

  if (!ok1 || !ok2) {
    std::cerr << "Failed writing integration output files\n";
    return 1;
  }
  if (!std::filesystem::exists(f1) || !std::filesystem::exists(f2)) {
    std::cerr << "Expected integration output files are missing\n";
    return 1;
  }

  return 0;
}
