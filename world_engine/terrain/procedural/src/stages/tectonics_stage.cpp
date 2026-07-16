#include "world_engine/terrain/procedural/stages/tectonics_stage.h"

// Lagrangian plate tectonics after Cortial et al. 2019, "Procedural Tectonic
// Planets" (docs/references/2019-Procedural-Tectonic-Planets.pdf).
//
// Crust state lives on the cells of a canonical icosahedral geodesic grid
// (GeodesicGrid). Plates are sets of cells carried by rigid rotations: between
// global resamplings, the crust sampled at canonical cell i of plate p is
// physically located at delta_R_p * center(i). Every resample_interval_steps
// the crust is re-projected onto the canonical grid (paper Section 6):
// overlaps consume the subducting side, gaps become fresh oceanic crust at
// the ridge (Section 4.3).
//
// Source anchors used below:
//   Section 3   plate motion s(p) = omega (w x p); crust attributes (Table 1)
//   Section 4.1 subduction uplift u_e = u0 * f(d) * g(v) * h(z~)
//   Section 4.3 oceanic crust generation, alpha-blended ridge profile
//   Section 4.5 continental erosion, oceanic dampening, trench sediments
//   Section 6   Fibonacci plate seeds, global resampling
//   Appendix A  reference constants

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "FastNoiseLite.h"
#include "world_engine/terrain/geodesic_grid.h"

namespace world_engine::terrain::procedural::stages {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- math ----

struct Mat3 {
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// R^T p (inverse of a rotation).
Vec3d apply_inv(const Mat3& r, const Vec3d& p) {
  return {r.m[0] * p.x + r.m[3] * p.y + r.m[6] * p.z,
          r.m[1] * p.x + r.m[4] * p.y + r.m[7] * p.z,
          r.m[2] * p.x + r.m[5] * p.y + r.m[8] * p.z};
}

Mat3 axis_angle(const Vec3d& axis, double angle) {
  const double c = std::cos(angle);
  const double s = std::sin(angle);
  const double t = 1.0 - c;
  const double x = axis.x;
  const double y = axis.y;
  const double z = axis.z;
  Mat3 r;
  r.m[0] = t * x * x + c;
  r.m[1] = t * x * y - s * z;
  r.m[2] = t * x * z + s * y;
  r.m[3] = t * x * y + s * z;
  r.m[4] = t * y * y + c;
  r.m[5] = t * y * z - s * x;
  r.m[6] = t * x * z - s * y;
  r.m[7] = t * y * z + s * x;
  r.m[8] = t * z * z + c;
  return r;
}

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Deterministic splitmix64-style hash -> [0, 1).
double unit_rand(uint64_t key) {
  key += 0x9E3779B97F4A7C15ULL;
  key = (key ^ (key >> 30)) * 0xBF58476D1CE4E5B9ULL;
  key = (key ^ (key >> 27)) * 0x94D049BB133111EBULL;
  key = key ^ (key >> 31);
  return static_cast<double>(key >> 11) / 9007199254740992.0;
}

Vec3d random_unit_vec(uint64_t key) {
  const double z = 2.0 * unit_rand(key) - 1.0;
  const double phi = 2.0 * kPi * unit_rand(key + 0x51ULL);
  const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
  return {r * std::cos(phi), r * std::sin(phi), z};
}

// Fibonacci sphere sampling (paper Section 6) for plate seeds.
std::vector<Vec3d> fibonacci_sphere(int n) {
  std::vector<Vec3d> pts;
  pts.reserve(n);
  const double golden = (1.0 + std::sqrt(5.0)) * 0.5;
  for (int i = 0; i < n; ++i) {
    const double u = (i + 0.5) / static_cast<double>(n);
    const double z = 1.0 - 2.0 * u;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double phi = 2.0 * kPi * static_cast<double>(i) / golden;
    pts.push_back({r * std::cos(phi), r * std::sin(phi), z});
  }
  return pts;
}

// -------------------------------------------------------------- plates ----

struct Plate {
  Vec3d axis{0.0, 0.0, 1.0};      // rotation axis w (unit)
  double omega_rad_myr = 0.0;     // angular speed
  Mat3 delta;                     // rotation accumulated since last resample
};

// Crust state on the canonical geodesic cells (paper Table 1).
struct Crust {
  std::vector<int32_t> plate;
  std::vector<uint8_t> type;          // 0 = oceanic, 1 = continental
  std::vector<float> elevation_m;
  std::vector<float> age_myr;         // oceanic crust age
  std::vector<uint8_t> orogeny_type;  // 0 none, 1 Andean, 2 Himalayan
  std::vector<float> orogeny_age_myr;
  std::vector<float> uplift_window_m; // uplift accumulated in trailing window

  void resize(int n) {
    plate.assign(n, 0);
    type.assign(n, 0);
    elevation_m.assign(n, 0.0f);
    age_myr.assign(n, 0.0f);
    orogeny_type.assign(n, 0);
    orogeny_age_myr.assign(n, 0.0f);
    uplift_window_m.assign(n, 0.0f);
  }
};

// Result of asking "which crust is physically at direction p right now?"
struct CrustSample {
  bool found = false;
  double coverage = 0.0;  // interpolation weight owned by the winning plate
  int plate = -1;
  int nearest_cell = -1;  // cell of the winning plate with max weight
  double elevation_m = 0.0;
  double age_myr = 0.0;
  double uplift_window_m = 0.0;
  double orogeny_age_myr = 0.0;
  uint8_t type = 0;
  uint8_t orogeny_type = 0;
};

// Query the crust of a single plate at direction p (world frame): inverse
// rotate into the plate's resample frame and interpolate over cells that the
// plate owns. Returns coverage 0 if the plate has no crust there.
CrustSample sample_plate(const GeodesicGrid& grid, const Crust& crust,
                         const std::vector<Plate>& plates, int plate_id,
                         const Vec3d& p) {
  const Vec3d q = apply_inv(plates[plate_id].delta, p);
  const auto lk = grid.locate(q);
  CrustSample out;
  double cov = 0.0;
  double elev = 0.0;
  double age = 0.0;
  double uplift = 0.0;
  double oro_age = 0.0;
  double cont_w = 0.0;
  double best_w = 0.0;
  int best_cell = -1;
  for (int k = 0; k < 3; ++k) {
    const int cell = lk.cell[k];
    const double wgt = lk.weight[k];
    if (crust.plate[cell] != plate_id) {
      continue;
    }
    cov += wgt;
    elev += wgt * crust.elevation_m[cell];
    age += wgt * crust.age_myr[cell];
    uplift += wgt * crust.uplift_window_m[cell];
    oro_age += wgt * crust.orogeny_age_myr[cell];
    if (crust.type[cell] == 1) {
      cont_w += wgt;
    }
    if (wgt > best_w) {
      best_w = wgt;
      best_cell = cell;
    }
  }
  if (cov <= 1e-9) {
    return out;
  }
  out.found = true;
  out.coverage = cov;
  out.plate = plate_id;
  out.nearest_cell = best_cell;
  out.elevation_m = elev / cov;
  out.age_myr = age / cov;
  out.uplift_window_m = uplift / cov;
  out.orogeny_age_myr = oro_age / cov;
  out.type = (cont_w > 0.5 * cov) ? 1 : 0;
  out.orogeny_type = (best_cell >= 0) ? crust.orogeny_type[best_cell] : 0;
  return out;
}

// Ownership arbitration at a point covered by several plates (or none).
// Continental crust always overrides oceanic crust (continental material is
// buoyant and does not subduct — paper Section 4.1); within the same crust
// class the higher interpolation coverage wins. M2 adds subduction uplift and
// ridge generation on top of this rule.
CrustSample sample_world(const GeodesicGrid& grid, const Crust& crust,
                         const std::vector<Plate>& plates, const Vec3d& p) {
  CrustSample best;
  double best_score = -1.0;
  for (int pl = 0; pl < static_cast<int>(plates.size()); ++pl) {
    const CrustSample s = sample_plate(grid, crust, plates, pl, p);
    if (!s.found) {
      continue;
    }
    const double score = s.coverage + (s.type == 1 ? 10.0 : 0.0);
    if (score > best_score) {
      best_score = score;
      best = s;
    }
  }
  return best;
}

}  // namespace

void run_tectonics_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& domain = dataset.domain();
  const auto& tp = params.tectonics;

  // Output layers (raster contract unchanged from the previous pipeline).
  if (!dataset.has_layer("plate_id")) {
    dataset.create_i32_layer("plate_id", "id", "Tectonic plate assignment");
  }
  if (!dataset.has_layer("tectonic_elevation_m")) {
    dataset.create_float_layer("tectonic_elevation_m", "m",
                               "Crust elevation from Lagrangian plate simulation");
  }
  if (!dataset.has_layer("uplift_rate_m_per_yr")) {
    dataset.create_float_layer("uplift_rate_m_per_yr", "m/yr",
                               "Recent tectonic uplift rate for analytical erosion");
  }
  if (!dataset.has_layer("crust_type")) {
    dataset.create_u8_layer("crust_type", "", "Crust type: 0=oceanic, 1=continental");
  }
  if (!dataset.has_layer("oceanic_age_myr")) {
    dataset.create_float_layer("oceanic_age_myr", "My", "Oceanic crust age");
  }

  const GeodesicGrid grid(std::max(4, tp.grid_frequency));
  const int n_cells = grid.cell_count();
  const double radius_km = domain.radius_m() / 1000.0;
  // v0 in mm/yr equals km/My; omega_max = v0 / R.
  const double omega_max_rad_myr = tp.max_plate_speed_mm_yr / radius_km;
  const uint64_t seed = static_cast<uint64_t>(static_cast<uint32_t>(params.seed));

  // ---- Plate initialization: Fibonacci seeds + warped Voronoi partition ----
  const int plate_count = std::max(2, tp.plate_count);
  const auto seeds = fibonacci_sphere(plate_count);
  std::vector<Plate> plates(plate_count);
  std::vector<Vec3d> warp_dir1(plate_count);
  std::vector<Vec3d> warp_dir2(plate_count);
  std::vector<double> phase1(plate_count);
  std::vector<double> phase2(plate_count);
  for (int p = 0; p < plate_count; ++p) {
    const uint64_t k0 = seed * 1315423911ULL + static_cast<uint64_t>(p) * 2654435761ULL;
    plates[p].axis = random_unit_vec(k0);
    const double frac = tp.min_plate_speed_frac +
                        (1.0 - tp.min_plate_speed_frac) * unit_rand(k0 + 7);
    plates[p].omega_rad_myr = frac * omega_max_rad_myr;
    warp_dir1[p] = random_unit_vec(k0 + 21);
    warp_dir2[p] = random_unit_vec(k0 + 37);
    phase1[p] = unit_rand(k0 + 51) * 2.0 * kPi;
    phase2[p] = unit_rand(k0 + 63) * 2.0 * kPi;
  }

  const auto plate_score = [&](int p, const Vec3d& c) {
    const double warp = 0.05 * std::sin(9.0 * dot(c, warp_dir1[p]) + phase1[p]) +
                        0.03 * std::sin(17.0 * dot(c, warp_dir2[p]) + phase2[p]);
    return dot(c, seeds[p]) + warp;
  };

  // ---- Crust initialization on canonical cells ----
  Crust crust;
  crust.resize(n_cells);

  FastNoiseLite continent_noise(params.seed + 9973);
  continent_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  continent_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  continent_noise.SetFractalOctaves(4);
  continent_noise.SetFractalLacunarity(2.0f);
  continent_noise.SetFractalGain(0.5f);
  continent_noise.SetFrequency(static_cast<float>(tp.continent_noise_freq));

  std::vector<float> cont_noise(n_cells);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n_cells; ++i) {
    const Vec3d& c = grid.cell_center(i);
    cont_noise[i] = continent_noise.GetNoise(static_cast<float>(c.x),
                                             static_cast<float>(c.y),
                                             static_cast<float>(c.z));
    int best = 0;
    double best_score = -1e30;
    for (int p = 0; p < plate_count; ++p) {
      const double s = plate_score(p, c);
      if (s > best_score) {
        best_score = s;
        best = p;
      }
    }
    crust.plate[i] = best;
  }

  std::vector<float> sorted_noise = cont_noise;
  std::sort(sorted_noise.begin(), sorted_noise.end());
  const int thr_idx = std::clamp(
      static_cast<int>((1.0 - tp.continent_ratio) * n_cells), 0, n_cells - 1);
  const float threshold = sorted_noise[thr_idx];

  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n_cells; ++i) {
    const uint64_t key = seed * 7919ULL + static_cast<uint64_t>(i);
    if (cont_noise[i] > threshold) {
      crust.type[i] = 1;
      const double interior = std::min((cont_noise[i] - threshold) * 3.0, 1.0) * 400.0;
      const double jitter = (unit_rand(key) - 0.5) * 300.0;
      crust.elevation_m[i] = static_cast<float>(tp.continental_base_m + interior + jitter);
    } else {
      crust.type[i] = 0;
      const double jitter = (unit_rand(key + 1000003ULL) - 0.5) * 300.0;
      crust.elevation_m[i] = static_cast<float>(tp.oceanic_base_m + jitter);
      // Young planet: give initial ocean floor a random age so dampening and
      // ridge renewal (M2) do not act in lockstep.
      crust.age_myr[i] = static_cast<float>(unit_rand(key + 2000003ULL) * 60.0);
    }
  }

  // ---- Simulation loop ----
  // M1 scope: rigid motion + periodic global resampling. Interactions
  // (subduction, collision, ridges, rifting) land in M2/M3.
  const int steps = std::max(0, tp.simulation_steps);
  const int resample_every = std::max(1, tp.resample_interval_steps);

  const auto global_resample = [&]() {
    Crust next;
    next.resize(n_cells);
    #pragma omp parallel for schedule(dynamic, 256)
    for (int i = 0; i < n_cells; ++i) {
      const Vec3d& c = grid.cell_center(i);
      const CrustSample s = sample_world(grid, crust, plates, c);
      if (s.found) {
        next.plate[i] = s.plate;
        next.type[i] = s.type;
        next.elevation_m[i] = static_cast<float>(s.elevation_m);
        next.age_myr[i] = static_cast<float>(s.age_myr);
        next.orogeny_type[i] = s.orogeny_type;
        next.orogeny_age_myr[i] = static_cast<float>(s.orogeny_age_myr);
        next.uplift_window_m[i] = static_cast<float>(s.uplift_window_m);
      } else {
        // Gap between diverging plates. M1 placeholder: fresh abyssal ocean
        // assigned to the nearest plate by warped score; M2 replaces this
        // with the ridge profile of paper Section 4.3.
        int best = 0;
        double best_score = -1e30;
        for (int p = 0; p < plate_count; ++p) {
          const double sc = plate_score(p, c);
          if (sc > best_score) {
            best_score = sc;
            best = p;
          }
        }
        next.plate[i] = best;
        next.type[i] = 0;
        next.elevation_m[i] = static_cast<float>(tp.abyssal_m);
        next.age_myr[i] = 0.0f;
      }
    }
    crust = std::move(next);
    for (Plate& p : plates) {
      p.delta = Mat3{};
    }
  };

  for (int s = 0; s < steps; ++s) {
    // 1. Advance rigid rotations (paper Section 3).
    for (Plate& p : plates) {
      const Mat3 stepR = axis_angle(p.axis, p.omega_rad_myr * tp.dt_myr);
      Mat3 acc;
      // acc = stepR * delta
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          acc.m[3 * r + c] = stepR.m[3 * r + 0] * p.delta.m[0 + c] +
                             stepR.m[3 * r + 1] * p.delta.m[3 + c] +
                             stepR.m[3 * r + 2] * p.delta.m[6 + c];
        }
      }
      p.delta = acc;
    }

    // 2. Crust ages advance (paper Table 1).
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_cells; ++i) {
      if (crust.type[i] == 0) {
        crust.age_myr[i] += static_cast<float>(tp.dt_myr);
      } else if (crust.orogeny_type[i] != 0) {
        crust.orogeny_age_myr[i] += static_cast<float>(tp.dt_myr);
      }
    }

    // 3. Global resampling (paper Sections 4.3 / 6).
    if ((s + 1) % resample_every == 0 && s + 1 < steps) {
      global_resample();
    }
  }

  // ---- Rasterize crust to the lat-lon dataset ----
  auto& plate_layer = dataset.i32_layer("plate_id");
  auto& tectonic = dataset.float_layer("tectonic_elevation_m");
  auto& uplift = dataset.float_layer("uplift_rate_m_per_yr");
  auto& crust_layer = dataset.u8_layer("crust_type");
  auto& age_layer = dataset.float_layer("oceanic_age_myr");

  const int w = domain.width();
  const int h = domain.height();
  const double uplift_window_yr = std::max(1.0, tp.uplift_window_myr * 1e6);

  #pragma omp parallel for schedule(dynamic, 8)
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int idx = domain.index(x, y);
      const Vec3d p = domain.lat_lon_to_xyz(domain.center_lat_lon_deg(x, y));
      const CrustSample s = sample_world(grid, crust, plates, p);
      if (s.found) {
        plate_layer[idx] = s.plate;
        crust_layer[idx] = s.type;
        tectonic[idx] = static_cast<float>(s.elevation_m);
        age_layer[idx] = (s.type == 0) ? static_cast<float>(s.age_myr) : 0.0f;
        uplift[idx] = static_cast<float>(s.uplift_window_m / uplift_window_yr);
        // Placeholder until M2 accumulates real subduction uplift: keep a
        // small continental floor so the erosion solver has a nonzero field.
        if (s.type == 1) {
          uplift[idx] = std::max(uplift[idx], 1e-4f);
        }
      } else {
        plate_layer[idx] = -1;
        crust_layer[idx] = 0;
        tectonic[idx] = static_cast<float>(tp.abyssal_m);
        age_layer[idx] = 0.0f;
        uplift[idx] = 0.0f;
      }
    }
  }
}

void run_combine_stage(const PipelineParams& params, TerrainDataset& dataset) {
  if (!dataset.has_layer("elevation_base_m")) {
    dataset.create_float_layer("elevation_base_m", "m",
                               "Tectonic + noise detail base elevation");
  }

  const auto& noise = dataset.float_layer("elevation_primeval_m");
  const auto& tectonic = dataset.float_layer("tectonic_elevation_m");
  auto& base = dataset.float_layer("elevation_base_m");
  const float mix = static_cast<float>(std::clamp(params.tectonics.noise_detail_mix, 0.0, 1.0));
  const float sea = params.hydrology.sea_level_m;

  for (int i = 0; i < dataset.size(); ++i) {
    // Tectonics drives the large scale; noise adds detail.
    float z = tectonic[i] + noise[i] * mix;

    // Continental shelf morphology (quadratic ramps near sea level).
    if (z > sea && z < sea + 200.0f) {
      const float t = (z - sea) / 200.0f;
      z = sea + t * t * 200.0f;
    } else if (z < sea && z > sea - 300.0f) {
      const float t = (sea - z) / 300.0f;
      z = sea - t * t * 300.0f;
    }

    base[i] = z;
  }
}

}  // namespace world_engine::terrain::procedural::stages
