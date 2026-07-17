// M6 gates for the geodesic dense-physics path: the pipeline with
// physics_grid_frequency > 0 must produce the full layer contract,
// plausible water coverage, a river network, and be deterministic.
// Addendum-b gates: attribute-modulated amplification adds roughness when
// the grid outresolves the raster; the Tzathas 5.3 optimization pass
// reduces cliff seams between cells draining to different outlets.

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "world_engine/terrain/geodesic_grid.h"
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

  // ---- Amplification + discontinuity-correction gates (F=224) ----
  world_engine::terrain::procedural::PipelineParams q;
  q.seed = 909;
  q.width = 256;
  q.height = 128;
  q.physics_grid_frequency = 224;
  q.tectonics.grid_frequency = 48;
  q.erosion.multigrid_levels = 3;
  q.erosion.fixed_point_iterations = 4;

  auto run_variant = [&](bool amp, int disc_iters, int levels, int fp_iters) {
    auto qq = q;
    qq.amplify.enable = amp;
    qq.amplify.base_amplitude_m = 200.0;      // boosted: gates test the
    qq.amplify.mountain_amplitude_m = 1200.0; // mechanism, not the defaults
    qq.erosion.discontinuity_iterations = disc_iters;
    qq.erosion.multigrid_levels = levels;
    qq.erosion.fixed_point_iterations = fp_iters;
    world_engine::terrain::procedural::ProceduralGenerator g;
    g.generate(qq);
    return g.dataset();  // copy; the generator itself is not movable
  };
  const auto ds_a = run_variant(false, 0, 3, 4);
  const auto ds_b = run_variant(true, 0, 3, 4);
  // Under-iterated single grid reproduces the paper's Fig-4 condition
  // (discontinuities between basins); 5.3 must reduce them.
  const auto ds_d = run_variant(true, 0, 1, 2);
  const auto ds_e = run_variant(true, 24, 1, 2);

  const world_engine::terrain::GeodesicGrid grid(224);
  const float sea = q.hydrology.sea_level_m;
  const auto roughness = [&](const world_engine::terrain::TerrainDataset& ds) {
    const auto& cl = ds.cell_layer("cell_elevation_m");
    double sum = 0.0;
    long count = 0;
    std::array<int, 6> nb{};
    for (int i = 0; i < grid.cell_count(); ++i) {
      if (cl.data[i] <= sea) {
        continue;
      }
      const int m = grid.neighbors(i, nb);
      double mean = 0.0;
      for (int k = 0; k < m; ++k) {
        mean += cl.data[nb[k]];
      }
      sum += std::abs(cl.data[i] - mean / m);
      ++count;
    }
    return sum / std::max(1L, count);
  };
  const auto rms_delta = [&](const world_engine::terrain::TerrainDataset& x,
                             const world_engine::terrain::TerrainDataset& y) {
    const auto& cx = x.cell_layer("cell_elevation_m");
    const auto& cy = y.cell_layer("cell_elevation_m");
    double s = 0.0;
    for (size_t i = 0; i < cx.data.size(); ++i) {
      const double d = cx.data[i] - cy.data[i];
      s += d * d;
    }
    return std::sqrt(s / cx.data.size());
  };
  const double rough_a = roughness(ds_a);
  const double rough_b = roughness(ds_b);
  const double delta_ab = rms_delta(ds_a, ds_b);
  std::printf("land roughness: amp off %.2f m, amp on %.2f m, rms delta %.1f m\n",
              rough_a, rough_b, delta_ab);
  if (!(rough_b > rough_a * 1.03) || delta_ab < 40.0) {
    std::printf("FAIL: amplification does not inject detail\n");
    ++failures;
  }

  // Seam metric: big cliffs between land neighbors (absolute drop).
  const auto seams = [&](const world_engine::terrain::TerrainDataset& ds,
                         float threshold_m) {
    const auto& cl = ds.cell_layer("cell_elevation_m");
    long count = 0;
    std::array<int, 6> nb{};
    for (int i = 0; i < grid.cell_count(); ++i) {
      if (cl.data[i] <= sea) {
        continue;
      }
      const int m = grid.neighbors(i, nb);
      for (int k = 0; k < m; ++k) {
        if (cl.data[nb[k]] > sea && cl.data[i] - cl.data[nb[k]] > threshold_m) {
          ++count;
        }
      }
    }
    return count;
  };
  const long seams_d = seams(ds_d, 400.0f);
  const long seams_e = seams(ds_e, 400.0f);
  const double delta_de = rms_delta(ds_d, ds_e);
  std::printf("cliff seams (single-grid): without 5.3 %ld, with 5.3 %ld, rms %.1f m\n",
              seams_d, seams_e, delta_de);
  if (delta_de < 0.5) {
    std::printf("FAIL: discontinuity correction is a no-op\n");
    ++failures;
  }
  if (seams_d > 100 && !(seams_e < seams_d)) {
    std::printf("FAIL: discontinuity correction does not reduce seams\n");
    ++failures;
  }

  const auto ocean_frac_of = [&](const world_engine::terrain::TerrainDataset& ds) {
    const auto& om = ds.u8_layer("ocean_mask");
    long c = 0;
    for (size_t i = 0; i < om.size(); ++i) {
      c += om[i] ? 1 : 0;
    }
    return static_cast<double>(c) / om.size();
  };
  const double of_a = ocean_frac_of(ds_a);
  const double of_b = ocean_frac_of(ds_b);
  std::printf("ocean fraction: baseline %.3f, amplified %.3f\n", of_a, of_b);
  if (std::abs(of_a - of_b) > 0.10) {
    std::printf("FAIL: amplification shifted ocean fraction too far\n");
    ++failures;
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("test_geodesic_physics passed\n");
  return 0;
}
