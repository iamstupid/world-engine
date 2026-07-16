// M1 acceptance gates for the Lagrangian tectonics stage:
//  1. Continental crust is conserved by rigid drift + resampling (no
//     interactions are active in this configuration).
//  2. Continents actually drift: a significant fraction of pixels change
//     crust type over 100 My, and the continental centroid moves.
// These encode the failure modes of the abandoned Eulerian experiment
// (boundary re-stamping, static continents).

#include <cmath>
#include <cstdio>

#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/procedural/stages/tectonics_stage.h"
#include "world_engine/terrain/terrain_dataset.h"
#include "world_engine/terrain/terrain_domain.h"

using world_engine::terrain::TerrainDataset;
using world_engine::terrain::TerrainDomain;
using world_engine::terrain::Vec3d;
namespace procedural = world_engine::terrain::procedural;

namespace {

struct CrustStats {
  double land_fraction = 0.0;
  Vec3d centroid{0.0, 0.0, 0.0};
};

CrustStats crust_stats(const TerrainDataset& ds) {
  const auto& domain = ds.domain();
  const auto& crust = ds.u8_layer("crust_type");
  CrustStats st;
  double total = 0.0;
  double land = 0.0;
  for (int y = 0; y < domain.height(); ++y) {
    const double a = domain.cell_area_m2(y);
    for (int x = 0; x < domain.width(); ++x) {
      total += a;
      if (crust[domain.index(x, y)]) {
        land += a;
        const Vec3d p = domain.lat_lon_to_xyz(domain.center_lat_lon_deg(x, y));
        st.centroid.x += a * p.x;
        st.centroid.y += a * p.y;
        st.centroid.z += a * p.z;
      }
    }
  }
  st.land_fraction = land / total;
  const double n = std::sqrt(st.centroid.x * st.centroid.x +
                             st.centroid.y * st.centroid.y +
                             st.centroid.z * st.centroid.z);
  if (n > 0.0) {
    st.centroid = {st.centroid.x / n, st.centroid.y / n, st.centroid.z / n};
  }
  return st;
}

TerrainDataset run_tectonics(int steps) {
  procedural::PipelineParams params;
  params.seed = 4242;
  params.width = 128;
  params.height = 64;
  params.tectonics.grid_frequency = 24;
  params.tectonics.plate_count = 6;
  params.tectonics.simulation_steps = steps;
  params.tectonics.resample_interval_steps = 10;
  TerrainDataset ds(TerrainDomain(params.width, params.height, params.radius_m));
  ds.set_seed(params.seed);
  procedural::stages::run_tectonics_stage(params, ds);
  return ds;
}

}  // namespace

int main() {
  const TerrainDataset before = run_tectonics(0);
  const TerrainDataset after = run_tectonics(50);  // 100 My at dt = 2 My

  const CrustStats sb = crust_stats(before);
  const CrustStats sa = crust_stats(after);

  int failures = 0;

  // Gate 1: conservation of continental area under drift.
  const double delta = std::abs(sa.land_fraction - sb.land_fraction);
  std::printf("land fraction: t0=%.4f t100My=%.4f delta=%.4f\n", sb.land_fraction,
              sa.land_fraction, delta);
  if (delta > 0.03) {
    std::printf("FAIL: continental area not conserved by drift/resampling\n");
    ++failures;
  }

  // Gate 2a: pixels change (continents move).
  const auto& cb = before.u8_layer("crust_type");
  const auto& ca = after.u8_layer("crust_type");
  int changed = 0;
  for (size_t i = 0; i < cb.size(); ++i) {
    changed += (cb[i] != ca[i]) ? 1 : 0;
  }
  const double changed_frac = static_cast<double>(changed) / cb.size();
  std::printf("crust_type pixels changed over 100 My: %.4f\n", changed_frac);
  if (changed_frac < 0.03) {
    std::printf("FAIL: continents did not drift\n");
    ++failures;
  }

  // Gate 2b: the continental centroid moves by a measurable angle.
  const double cosang = sb.centroid.x * sa.centroid.x + sb.centroid.y * sa.centroid.y +
                        sb.centroid.z * sa.centroid.z;
  const double angle = std::acos(std::min(1.0, std::max(-1.0, cosang)));
  std::printf("continental centroid displacement: %.4f rad\n", angle);
  if (angle < 0.01) {
    std::printf("FAIL: continental centroid did not move\n");
    ++failures;
  }

  // Gate 3 (M2): oceanic age structure. Ridge renewal + age-driven dampening
  // must produce ocean floor whose depth increases with age (the Eulerian
  // re-stamping bug produced elevation decoupled from any age field).
  {
    const auto& age = after.float_layer("oceanic_age_myr");
    const auto& elev = after.float_layer("tectonic_elevation_m");
    const auto& crust = after.u8_layer("crust_type");
    double sa = 0.0;
    double se = 0.0;
    double saa = 0.0;
    double see = 0.0;
    double sae = 0.0;
    int n = 0;
    double young = 0.0;
    double old_crust = 0.0;
    for (size_t i = 0; i < age.size(); ++i) {
      if (crust[i] != 0) {
        continue;
      }
      const double a = age[i];
      const double e = elev[i];
      sa += a;
      se += e;
      saa += a * a;
      see += e * e;
      sae += a * e;
      ++n;
      if (a < 15.0) {
        young += 1.0;
      }
      if (a > 60.0) {
        old_crust += 1.0;
      }
    }
    const double cov = sae / n - (sa / n) * (se / n);
    const double var_a = saa / n - (sa / n) * (sa / n);
    const double var_e = see / n - (se / n) * (se / n);
    const double corr = cov / std::sqrt(std::max(1e-9, var_a * var_e));
    std::printf("ocean age/elevation correlation: %.3f (young frac %.3f, old frac %.3f)\n",
                corr, young / n, old_crust / n);
    if (corr > -0.3) {
      std::printf("FAIL: ocean depth not age-driven (re-stamping or dead ridges)\n");
      ++failures;
    }
    if (young / n < 0.02) {
      std::printf("FAIL: no young oceanic crust - ridges inactive\n");
      ++failures;
    }
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("test_tectonics passed\n");
  return 0;
}
