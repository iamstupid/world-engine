#include "world_engine/terrain/procedural/stages/tectonics_stage.h"

// Lagrangian plate tectonics after Cortial et al. 2019, "Procedural Tectonic
// Planets" (docs/references/2019-Procedural-Tectonic-Planets.pdf).
//
// Crust state lives on the cells of a canonical icosahedral geodesic grid
// (GeodesicGrid). Plates are sets of cells carried by rigid rotations: between
// global resamplings, the crust sampled at canonical cell i of plate p is
// physically located at delta_R_p * center(i). Every resample_interval_steps
// the crust is re-projected onto the canonical grid (paper Section 6):
// overlaps consume the subducting side, gaps become fresh oceanic crust with
// the ridge profile (Section 4.3).
//
// Source anchors:
//   Section 3   plate motion s(p) = omega (w x p); crust attributes (Table 1)
//   Section 4.1 subduction uplift u_e = u0 * f(d) * g(v) * h(z~)
//   Section 4.3 oceanic crust generation, alpha-blended ridge profile
//   Section 4.5 continental erosion, oceanic dampening, trench sediments
//   Section 6   Fibonacci plate seeds, global resampling
//   Appendix A  reference constants
//
// Documented deviations (see docs/ALGORITHMS.md):
//   - f(d) exact cubic coefficients are not given in the paper (Fig 6 only):
//     we use f(0)=0.6 rising to 1 at 0.15*r_s, then a smooth cubic decay to
//     0 at r_s.
//   - Ridge template z_G is the constant crest elevation z_r; the alpha blend
//     against the bordering plate elevation provides the flank profile, and
//     age-driven oceanic dampening (4.5) provides long-term subsidence.
//   - Trenches are emergent (oldest crust sinks deepest via 4.5 dampening and
//     is consumed at fronts) rather than explicitly carved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <utility>
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
Vec3d cross(const Vec3d& a, const Vec3d& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d mul(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

double angular_dist(const Vec3d& a, const Vec3d& b) {
  return std::acos(std::clamp(dot(a, b), -1.0, 1.0));
}

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
  std::vector<float> uplift_ema_m_yr; // EMA of recent tectonic uplift rate

  void resize(int n) {
    plate.assign(n, 0);
    type.assign(n, 0);
    elevation_m.assign(n, 0.0f);
    age_myr.assign(n, 0.0f);
    orogeny_type.assign(n, 0);
    orogeny_age_myr.assign(n, 0.0f);
    uplift_ema_m_yr.assign(n, 0.0f);
  }
};

// Subduction front fields, recomputed after every resample (paper Section 6:
// distance-to-front tracking; static between resamples because the front is
// fixed in the overriding plate's own frame).
struct FrontFields {
  std::vector<float> dist_km;      // distance to nearest convergent front
  std::vector<float> speed_mm_yr;  // relative plate speed at that front
  std::vector<float> sub_znorm;    // normalized elevation of subducting side

  void resize(int n) {
    dist_km.assign(n, 1e30f);
    speed_mm_yr.assign(n, 0.0f);
    sub_znorm.assign(n, 0.0f);
  }
};

// Result of asking "which crust is physically at direction p right now?"
struct CrustSample {
  bool found = false;
  double coverage = 0.0;
  int plate = -1;
  int nearest_cell = -1;
  double elevation_m = 0.0;
  double age_myr = 0.0;
  double uplift_ema_m_yr = 0.0;
  double orogeny_age_myr = 0.0;
  uint8_t type = 0;
  uint8_t orogeny_type = 0;
  // Losing continental plate when two continental claims overlap here
  // (collision tracking, paper Sections 4.2 / 6).
  int cont_runner_up = -1;
};

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
    uplift += wgt * crust.uplift_ema_m_yr[cell];
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
  out.uplift_ema_m_yr = uplift / cov;
  out.orogeny_age_myr = oro_age / cov;
  out.type = (cont_w > 0.5 * cov) ? 1 : 0;
  out.orogeny_type = (best_cell >= 0) ? crust.orogeny_type[best_cell] : 0;
  return out;
}

// Ownership arbitration at a point covered by several plates (or none).
// Continental crust always overrides oceanic (buoyancy, paper Section 4.1);
// between two oceanic plates the younger one overrides (the older, denser
// plate subducts); otherwise higher interpolation coverage wins.
CrustSample sample_world(const GeodesicGrid& grid, const Crust& crust,
                         const std::vector<Plate>& plates, const Vec3d& p) {
  CrustSample best;
  double best_score = -1.0;
  int cont_best = -1;
  int cont_second = -1;
  double cont_best_cov = 0.0;
  double cont_second_cov = 0.0;
  for (int pl = 0; pl < static_cast<int>(plates.size()); ++pl) {
    const CrustSample s = sample_plate(grid, crust, plates, pl, p);
    if (!s.found) {
      continue;
    }
    double score = s.coverage;
    if (s.type == 1) {
      score += 10.0;
      if (s.coverage > cont_best_cov) {
        cont_second = cont_best;
        cont_second_cov = cont_best_cov;
        cont_best = pl;
        cont_best_cov = s.coverage;
      } else if (s.coverage > cont_second_cov) {
        cont_second = pl;
        cont_second_cov = s.coverage;
      }
    } else {
      score += 0.15 * std::max(0.0, 1.0 - s.age_myr / 80.0);  // younger overrides
    }
    if (score > best_score) {
      best_score = score;
      best = s;
    }
  }
  // Meaningful continental interpenetration only (both claims substantial).
  if (best.type == 1 && cont_second >= 0 && cont_second_cov > 0.25 &&
      best.plate == cont_best) {
    best.cont_runner_up = cont_second;
  }
  return best;
}

// Subduction distance transfer f(d) (paper Fig 6; exact coefficients are
// unspecified): f(0) = 0.6, peak 1 at 0.1*r_s, then a quartic Wendland decay
// that reaches 0 at r_s with most of its mass inside 0.4*r_s — this keeps
// orogens as belts hugging the margin instead of blanketing continents.
double front_distance_transfer(double d_km, double rs_km) {
  const double dn = d_km / rs_km;
  if (dn >= 1.0) {
    return 0.0;
  }
  if (dn < 0.1) {
    const double t = dn / 0.1;
    const double s = t * t * (3.0 - 2.0 * t);
    return 0.6 + 0.4 * s;
  }
  const double t = (dn - 0.1) / 0.9;
  const double omt = 1.0 - t;
  return omt * omt * omt * omt * (1.0 + 4.0 * t);
}

}  // namespace

void run_tectonics_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& domain = dataset.domain();
  const auto& tp = params.tectonics;

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
  const double cell_spacing_km =
      std::sqrt(4.0 * kPi / n_cells) * radius_km;  // mean center spacing

  // ---- Plate initialization ----
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

  // Linear surface velocity of plate p at unit direction c, in mm/yr (km/My).
  const auto plate_velocity_mm_yr = [&](int p, const Vec3d& c) {
    return mul(cross(plates[p].axis, c), plates[p].omega_rad_myr * radius_km);
  };

  // ---- Crust initialization ----
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
      // Young planet: randomized initial ocean age so dampening and ridge
      // renewal do not act in lockstep.
      crust.age_myr[i] = static_cast<float>(unit_rand(key + 2000003ULL) * 60.0);
    }
  }

  // ---- Front fields: convergent boundary detection + distance BFS ----
  // Runs on the canonical configuration (immediately after init/resample,
  // when all plate deltas are identity).
  FrontFields fronts;
  const auto compute_front_fields = [&](double window_myr) {
    fronts.resize(n_cells);
    using QE = std::pair<double, int>;  // (distance_km, cell)
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> queue;

    // Plate centroids (for slab pull, paper Section 4.1).
    const int np = static_cast<int>(plates.size());
    std::vector<Vec3d> centroid(np, Vec3d{0.0, 0.0, 0.0});
    std::vector<int> members(np, 0);
    for (int i = 0; i < n_cells; ++i) {
      const int p = crust.plate[i];
      const Vec3d& c = grid.cell_center(i);
      centroid[p].x += c.x;
      centroid[p].y += c.y;
      centroid[p].z += c.z;
      ++members[p];
    }
    for (int p = 0; p < np; ++p) {
      if (members[p] > 0) {
        const double n = std::sqrt(dot(centroid[p], centroid[p]));
        if (n > 1e-9) {
          centroid[p] = mul(centroid[p], 1.0 / n);
        }
      }
    }
    std::vector<Vec3d> pull(np, Vec3d{0.0, 0.0, 0.0});
    std::vector<int> pull_n(np, 0);

    std::array<int, 6> nb{};
    for (int i = 0; i < n_cells; ++i) {
      const int pi = crust.plate[i];
      const Vec3d& ci = grid.cell_center(i);
      const int m = grid.neighbors(i, nb);
      double best_speed = 0.0;
      double best_znorm = 0.0;
      bool overriding = false;
      for (int k = 0; k < m; ++k) {
        const int j = nb[k];
        const int pj = crust.plate[j];
        if (pj == pi) {
          continue;
        }
        // Convergence: relative velocity of the neighbor plate toward us.
        const Vec3d rel = sub(plate_velocity_mm_yr(pj, ci), plate_velocity_mm_yr(pi, ci));
        Vec3d dir = sub(grid.cell_center(j), ci);
        dir = sub(dir, mul(ci, dot(dir, ci)));  // tangent component
        const double dn = std::sqrt(dot(dir, dir));
        if (dn < 1e-12) {
          continue;
        }
        dir = mul(dir, 1.0 / dn);
        const double closing_mm_yr = -dot(rel, dir);  // >0 when j approaches i
        if (closing_mm_yr <= 1.0) {
          continue;  // not convergent (or negligibly so)
        }
        // Who overrides? Continental over oceanic; younger oceanic over
        // older. Continental-continental convergence is NOT sustained
        // subduction: in the paper it resolves as a discrete collision event
        // with terrane suturing (Section 4.2, lands in M3), which terminates
        // the boundary. Pumping u_e there for the whole run causes runaway
        // uplift, so it is excluded here.
        bool i_overrides = false;
        if (crust.type[i] == 1 && crust.type[j] == 0) {
          i_overrides = true;
        } else if (crust.type[i] == 0 && crust.type[j] == 0) {
          i_overrides = crust.age_myr[i] <= crust.age_myr[j];
        }
        if (!i_overrides) {
          continue;
        }
        overriding = true;
        // Slab pull (Section 4.1): the subducting plate's axis drifts so its
        // motion pulls it toward this front point.
        {
          const Vec3d slab_dir = cross(centroid[pj], ci);
          const double sn = std::sqrt(dot(slab_dir, slab_dir));
          if (sn > 1e-9) {
            pull[pj].x += slab_dir.x / sn;
            pull[pj].y += slab_dir.y / sn;
            pull[pj].z += slab_dir.z / sn;
            ++pull_n[pj];
          }
        }
        // g(v) uses the normal closing speed: oblique (shear-dominated)
        // boundaries produce little subduction flux.
        if (closing_mm_yr > best_speed) {
          best_speed = closing_mm_yr;
          const double zn = (static_cast<double>(crust.elevation_m[j]) - tp.trench_depth_m) /
                            (tp.max_continental_m - tp.trench_depth_m);
          best_znorm = std::clamp(zn, 0.0, 1.0);
        }
      }
      if (overriding) {
        fronts.dist_km[i] = static_cast<float>(0.5 * cell_spacing_km);
        fronts.speed_mm_yr[i] = static_cast<float>(best_speed);
        fronts.sub_znorm[i] = static_cast<float>(best_znorm);
        queue.push({fronts.dist_km[i], i});
      }
    }

    // Dijkstra within each overriding plate, limited to r_s.
    while (!queue.empty()) {
      const auto [d, i] = queue.top();
      queue.pop();
      if (d > fronts.dist_km[i] + 1e-6) {
        continue;
      }
      if (d > tp.subduction_distance_km) {
        continue;
      }
      const int m = grid.neighbors(i, nb);
      for (int k = 0; k < m; ++k) {
        const int j = nb[k];
        if (crust.plate[j] != crust.plate[i]) {
          continue;  // uplift propagates within the overriding plate only
        }
        const double step_km =
            angular_dist(grid.cell_center(i), grid.cell_center(j)) * radius_km;
        const double nd = d + step_km;
        if (nd < fronts.dist_km[j]) {
          fronts.dist_km[j] = static_cast<float>(nd);
          fronts.speed_mm_yr[j] = fronts.speed_mm_yr[i];
          fronts.sub_znorm[j] = fronts.sub_znorm[i];
          queue.push({nd, j});
        }
      }
    }

    // Apply slab pull: w <- normalize(w + strength * (window/100My) * dir).
    if (window_myr > 0.0 && tp.slab_pull_strength > 0.0) {
      for (int p = 0; p < np; ++p) {
        if (pull_n[p] == 0) {
          continue;
        }
        const double pn = std::sqrt(dot(pull[p], pull[p]));
        if (pn < 1e-9) {
          continue;  // fronts on opposite sides cancel
        }
        const Vec3d dir = mul(pull[p], 1.0 / pn);
        const double blend = tp.slab_pull_strength * (window_myr / 100.0) *
                             std::min(1.0, pn / std::max(1, pull_n[p]) * 4.0);
        Vec3d w = plates[p].axis;
        w.x += blend * dir.x;
        w.y += blend * dir.y;
        w.z += blend * dir.z;
        const double wn = std::sqrt(dot(w, w));
        if (wn > 1e-9) {
          plates[p].axis = mul(w, 1.0 / wn);
        }
      }
    }
  };

  // ---- Global resampling (paper Sections 4.3 / 6) ----
  const int steps = std::max(0, tp.simulation_steps);
  const int resample_every = std::max(1, tp.resample_interval_steps);
  double window_myr = 0.0;  // simulated time since last resample

  int resample_counter = 0;
  // Collision cooldown per (winner, loser) pair: sustained convergence of two
  // large continental plates would otherwise re-fire a full-surge event every
  // resample (progressive accretion overcounting).
  std::vector<std::pair<int64_t, int>> collision_history;
  const auto global_resample = [&](double elapsed_myr) {
    ++resample_counter;
    Crust next;
    next.resize(n_cells);
    std::vector<uint8_t> is_gap(n_cells, 0);
    std::vector<int32_t> overlap_loser(n_cells, -1);

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
        next.uplift_ema_m_yr[i] = static_cast<float>(s.uplift_ema_m_yr);
        overlap_loser[i] = s.cont_runner_up;
      } else {
        is_gap[i] = 1;
      }
    }

    // Oceanic crust generation in divergence gaps (Section 4.3):
    //   alpha = d_ridge / (d_ridge + d_plate)
    //   z = alpha * z_border + (1 - alpha) * z_ridge_crest
    // The ridge is the skeleton of the gap: cells locally farthest from any
    // covered cell.
    std::array<int, 6> nb{};
    using QE = std::pair<double, int>;
    std::vector<float> d_plate(n_cells, 1e30f);
    std::vector<float> z_border(n_cells, 0.0f);
    std::vector<int32_t> border_plate(n_cells, 0);
    {
      std::priority_queue<QE, std::vector<QE>, std::greater<QE>> queue;
      for (int i = 0; i < n_cells; ++i) {
        if (is_gap[i]) {
          continue;
        }
        const int m = grid.neighbors(i, nb);
        bool borders_gap = false;
        for (int k = 0; k < m; ++k) {
          if (is_gap[nb[k]]) {
            borders_gap = true;
            break;
          }
        }
        if (borders_gap) {
          d_plate[i] = 0.0f;
          z_border[i] = next.elevation_m[i];
          border_plate[i] = next.plate[i];
          queue.push({0.0, i});
        }
      }
      while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > d_plate[i] + 1e-6) {
          continue;
        }
        const int m = grid.neighbors(i, nb);
        for (int k = 0; k < m; ++k) {
          const int j = nb[k];
          if (!is_gap[j]) {
            continue;
          }
          const double step_km =
              angular_dist(grid.cell_center(i), grid.cell_center(j)) * radius_km;
          const double ndist = d + step_km;
          if (ndist < d_plate[j]) {
            d_plate[j] = static_cast<float>(ndist);
            z_border[j] = z_border[i];
            border_plate[j] = border_plate[i];
            queue.push({ndist, j});
          }
        }
      }
    }
    // Ridge skeleton: gap cells whose d_plate is a local maximum.
    std::vector<float> d_ridge(n_cells, 1e30f);
    {
      std::priority_queue<QE, std::vector<QE>, std::greater<QE>> queue;
      for (int i = 0; i < n_cells; ++i) {
        if (!is_gap[i]) {
          continue;
        }
        const int m = grid.neighbors(i, nb);
        bool local_max = true;
        for (int k = 0; k < m; ++k) {
          if (is_gap[nb[k]] && d_plate[nb[k]] > d_plate[i] + 1e-3f) {
            local_max = false;
            break;
          }
        }
        if (local_max) {
          d_ridge[i] = 0.0f;
          queue.push({0.0, i});
        }
      }
      while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d > d_ridge[i] + 1e-6) {
          continue;
        }
        const int m = grid.neighbors(i, nb);
        for (int k = 0; k < m; ++k) {
          const int j = nb[k];
          if (!is_gap[j]) {
            continue;
          }
          const double step_km =
              angular_dist(grid.cell_center(i), grid.cell_center(j)) * radius_km;
          const double ndist = d + step_km;
          if (ndist < d_ridge[j]) {
            d_ridge[j] = static_cast<float>(ndist);
            queue.push({ndist, j});
          }
        }
      }
    }
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_cells; ++i) {
      if (!is_gap[i]) {
        continue;
      }
      const double dp = d_plate[i];
      const double dr = d_ridge[i];
      const double alpha = (dp + dr > 1e-6) ? dr / (dr + dp) : 0.0;
      const double zg = tp.ridge_crest_m;
      next.plate[i] = border_plate[i];
      next.type[i] = 0;
      next.elevation_m[i] =
          static_cast<float>(alpha * static_cast<double>(z_border[i]) + (1.0 - alpha) * zg);
      // Crust nearer the border was created earlier in the window.
      next.age_myr[i] = static_cast<float>(alpha * elapsed_myr);
      next.orogeny_type[i] = 0;
      next.orogeny_age_myr[i] = 0.0f;
      next.uplift_ema_m_yr[i] = 0.0f;
    }

    // ---- Continental collision events (paper Section 4.2 / Section 6) ----
    // Interpenetration cells were recorded during arbitration. An event
    // fires per (winner, loser) plate pair once the overlap penetrates
    // deeper than collision_interpen_km; the loser's colliding terrane is
    // sutured onto the winner and an instantaneous, compactly supported
    // elevation surge is applied.
    {
      std::vector<std::pair<int64_t, int>> pair_cells;
      for (int i = 0; i < n_cells; ++i) {
        if (overlap_loser[i] >= 0 && overlap_loser[i] != next.plate[i]) {
          const int64_t key =
              (static_cast<int64_t>(next.plate[i]) << 24) | overlap_loser[i];
          pair_cells.push_back({key, i});
        }
      }
      std::sort(pair_cells.begin(), pair_cells.end());
      const double uplift_tau_yr = std::max(1.0, tp.uplift_window_myr) * 1e6;
      const double a0_km2 = 4.0 * kPi * radius_km * radius_km / plate_count;
      int events = 0;

      size_t lo = 0;
      std::array<int, 6> nb{};
      while (lo < pair_cells.size() && events < 2) {
        size_t hi = lo;
        while (hi < pair_cells.size() && pair_cells[hi].first == pair_cells[lo].first) {
          ++hi;
        }
        const int64_t pair_key = pair_cells[lo].first;
        const int winner = static_cast<int>(pair_key >> 24);
        const int loser = static_cast<int>(pair_key & 0xFFFFFF);
        std::vector<int> overlap;
        overlap.reserve(hi - lo);
        for (size_t k = lo; k < hi; ++k) {
          overlap.push_back(pair_cells[k].second);
        }
        lo = hi;

        // Cooldown: at most one event per plate pair every 3 resamples.
        bool on_cooldown = false;
        for (const auto& [key, when] : collision_history) {
          if (key == pair_key && resample_counter - when < 3) {
            on_cooldown = true;
            break;
          }
        }
        if (on_cooldown) {
          continue;
        }

        // Penetration depth: distance-to-edge inside the overlap set.
        std::vector<uint8_t> in_set(n_cells, 0);
        for (int c : overlap) {
          in_set[c] = 1;
        }
        using QE = std::pair<double, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> queue;
        std::vector<float> depth(n_cells, 1e30f);
        for (int c : overlap) {
          const int m = grid.neighbors(c, nb);
          for (int k = 0; k < m; ++k) {
            if (!in_set[nb[k]]) {
              depth[c] = 0.0f;
              queue.push({0.0, c});
              break;
            }
          }
        }
        double max_depth = 0.0;
        while (!queue.empty()) {
          const auto [d, i] = queue.top();
          queue.pop();
          if (d > depth[i] + 1e-6) {
            continue;
          }
          max_depth = std::max(max_depth, d);
          const int m = grid.neighbors(i, nb);
          for (int k = 0; k < m; ++k) {
            const int j = nb[k];
            if (!in_set[j]) {
              continue;
            }
            const double nd =
                d + angular_dist(grid.cell_center(i), grid.cell_center(j)) * radius_km;
            if (nd < depth[j]) {
              depth[j] = static_cast<float>(nd);
              queue.push({nd, j});
            }
          }
        }
        if (max_depth < tp.collision_interpen_km) {
          continue;  // still interpenetrating; keep tracking
        }

        // Terrane R: the loser's continental region connected to the overlap.
        std::vector<int> terrane;
        {
          std::vector<int> stack = overlap;
          std::vector<uint8_t> seen(n_cells, 0);
          for (int c : overlap) {
            seen[c] = 1;
          }
          while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int m = grid.neighbors(i, nb);
            for (int k = 0; k < m; ++k) {
              const int j = nb[k];
              if (seen[j] || next.plate[j] != loser || next.type[j] != 1) {
                continue;
              }
              seen[j] = 1;
              terrane.push_back(j);
              stack.push_back(j);
            }
          }
        }

        // Surge geometry uses the INTERPENETRATION area, not the whole
        // terrane: the doubled crust in the overlap drives the uplift.
        // (Reading A as the full terrane area makes delta_c * A explode to
        // tens of km for subcontinents; with the overlap area the formula is
        // self-consistent across terrane sizes: ~300 km x front length gives
        // Himalayan-scale surges and belt-scale influence radii.)
        Vec3d q{0.0, 0.0, 0.0};
        double area_km2 = 0.0;
        for (int c : overlap) {
          const Vec3d& pc = grid.cell_center(c);
          const double a = grid.cell_area_sr(c) * radius_km * radius_km;
          q.x += a * pc.x;
          q.y += a * pc.y;
          q.z += a * pc.z;
          area_km2 += a;
        }
        const double qn = std::sqrt(dot(q, q));
        if (qn < 1e-9 || area_km2 <= 0.0) {
          continue;
        }
        q = mul(q, 1.0 / qn);

        // r = r_c * sqrt((v/v0) * (A/A0)); surge = min(cap, delta_c * A).
        const Vec3d rel = sub(plate_velocity_mm_yr(loser, q),
                              plate_velocity_mm_yr(winner, q));
        const double v = std::sqrt(dot(rel, rel));
        const double r_km =
            std::min(tp.collision_distance_km,
                     tp.collision_distance_km *
                         std::sqrt(std::max(1e-6, (v / tp.max_plate_speed_mm_yr) *
                                                       (area_km2 / a0_km2))));
        const double surge_m = std::min(
            tp.collision_max_uplift_m, tp.collision_coeff_per_km * area_km2 * 1000.0);

        // Influence region: Minkowski offset of the overlap by r (Dijkstra
        // across all plates), surge with Wendland falloff (1-(d/r)^2)^2.
        std::vector<float> infl(n_cells, 1e30f);
        while (!queue.empty()) {
          queue.pop();
        }
        for (int c : overlap) {
          infl[c] = 0.0f;
          queue.push({0.0, c});
        }
        while (!queue.empty()) {
          const auto [d, i] = queue.top();
          queue.pop();
          if (d > infl[i] + 1e-6 || d > r_km) {
            continue;
          }
          const int m = grid.neighbors(i, nb);
          for (int k = 0; k < m; ++k) {
            const int j = nb[k];
            const double nd =
                d + angular_dist(grid.cell_center(i), grid.cell_center(j)) * radius_km;
            if (nd < infl[j]) {
              infl[j] = static_cast<float>(nd);
              queue.push({nd, j});
            }
          }
        }
        for (int i = 0; i < n_cells; ++i) {
          if (infl[i] > r_km) {
            continue;
          }
          const double t = infl[i] / r_km;
          const double falloff = (1.0 - t * t) * (1.0 - t * t);
          const double dz = surge_m * falloff;
          if (dz < 25.0) {
            continue;
          }
          next.elevation_m[i] = static_cast<float>(
              std::min(tp.max_continental_m, static_cast<double>(next.elevation_m[i]) + dz));
          if (next.type[i] == 1 && dz > 200.0) {
            next.orogeny_type[i] = 2;  // Himalayan
            next.orogeny_age_myr[i] = 0.0f;
          }
          next.uplift_ema_m_yr[i] += static_cast<float>(dz / uplift_tau_yr);
        }

        // Suture: the terrane transfers to the winning plate.
        for (int c : terrane) {
          next.plate[c] = winner;
        }
        collision_history.push_back({pair_key, resample_counter});
        std::printf(
            "  [tectonics] collision: plate %d sutures terrane of plate %d "
            "(overlap %.0f km2, surge %.0f m, r %.0f km)\n",
            winner, loser, area_km2, surge_m, r_km);
        ++events;
      }
    }

    // ---- Rifting events (paper Section 4.4) ----
    // P = lambda * exp(-lambda), lambda = lambda0 * f(x_P) * A/A0 scaled by
    // the elapsed window; fragments get diverging rotations away from the
    // parent centroid.
    {
      const int np = static_cast<int>(plates.size());
      std::vector<int> count(np, 0);
      std::vector<int> cont(np, 0);
      std::vector<Vec3d> centroid(np, Vec3d{0.0, 0.0, 0.0});
      for (int i = 0; i < n_cells; ++i) {
        const int p = next.plate[i];
        ++count[p];
        cont[p] += next.type[i];
        const Vec3d& c = grid.cell_center(i);
        centroid[p].x += c.x;
        centroid[p].y += c.y;
        centroid[p].z += c.z;
      }
      const double a0_cells = static_cast<double>(n_cells) / plate_count;
      int rifts = 0;
      for (int p = 0; p < np && rifts < 2; ++p) {
        if (count[p] < 128 || static_cast<int>(plates.size()) >= 60) {
          continue;
        }
        const double x_p = static_cast<double>(cont[p]) / count[p];
        const double lambda = tp.rift_events_per_100myr * (elapsed_myr / 100.0) *
                              (0.25 + 0.75 * x_p) * (count[p] / a0_cells);
        const uint64_t key = seed * 0x9E3779B1ULL +
                             static_cast<uint64_t>(p) * 0xC2B2AE35ULL +
                             static_cast<uint64_t>(resample_counter) * 0x165667B1ULL;
        if (unit_rand(key) >= lambda * std::exp(-lambda)) {
          continue;
        }
        // Fragment count n in [2, 4] (paper 4.4).
        const int n_frag = 2 + static_cast<int>(unit_rand(key + 1) * 3.0) % 3;
        std::vector<int> members;
        members.reserve(count[p]);
        for (int i = 0; i < n_cells; ++i) {
          if (next.plate[i] == p) {
            members.push_back(i);
          }
        }
        // Fragment seeds: random member cells; warped-distance partition.
        std::vector<Vec3d> frag_seed(n_frag);
        std::vector<Vec3d> frag_warp(n_frag);
        std::vector<double> frag_phase(n_frag);
        for (int f = 0; f < n_frag; ++f) {
          frag_seed[f] =
              grid.cell_center(members[static_cast<size_t>(unit_rand(key + 10 + f) *
                                                           members.size()) %
                                       members.size()]);
          frag_warp[f] = random_unit_vec(key + 40 + f);
          frag_phase[f] = unit_rand(key + 70 + f) * 2.0 * kPi;
        }
        std::vector<int> frag_plate(n_frag, p);
        for (int f = 1; f < n_frag; ++f) {
          frag_plate[f] = static_cast<int>(plates.size());
          Plate fresh;
          fresh.omega_rad_myr =
              (tp.min_plate_speed_frac +
               (1.0 - tp.min_plate_speed_frac) * unit_rand(key + 100 + f)) *
              omega_max_rad_myr;
          plates.push_back(fresh);
        }
        std::vector<Vec3d> frag_centroid(n_frag, Vec3d{0.0, 0.0, 0.0});
        for (int i : members) {
          const Vec3d& c = grid.cell_center(i);
          int best = 0;
          double best_score = -1e30;
          for (int f = 0; f < n_frag; ++f) {
            const double score = dot(c, frag_seed[f]) +
                                 0.05 * std::sin(11.0 * dot(c, frag_warp[f]) + frag_phase[f]);
            if (score > best_score) {
              best_score = score;
              best = f;
            }
          }
          next.plate[i] = frag_plate[best];
          frag_centroid[best].x += c.x;
          frag_centroid[best].y += c.y;
          frag_centroid[best].z += c.z;
        }
        // Diverging rotations: velocity at each fragment centroid points away
        // from the parent centroid.
        Vec3d cp = centroid[p];
        const double cpn = std::sqrt(dot(cp, cp));
        cp = (cpn > 1e-9) ? mul(cp, 1.0 / cpn) : Vec3d{0.0, 0.0, 1.0};
        for (int f = 0; f < n_frag; ++f) {
          Vec3d cf = frag_centroid[f];
          const double cfn = std::sqrt(dot(cf, cf));
          if (cfn < 1e-9) {
            continue;
          }
          cf = mul(cf, 1.0 / cfn);
          Vec3d away = sub(cf, cp);
          away = sub(away, mul(cf, dot(away, cf)));  // tangent at cf
          const double an = std::sqrt(dot(away, away));
          if (an < 1e-6) {
            plates[frag_plate[f]].axis = random_unit_vec(key + 130 + f);
            continue;
          }
          away = mul(away, 1.0 / an);
          Vec3d axis = cross(cf, away);
          const double axn = std::sqrt(dot(axis, axis));
          if (axn > 1e-9) {
            plates[frag_plate[f]].axis = mul(axis, 1.0 / axn);
          }
        }
        std::printf("  [tectonics] rift: plate %d splits into %d fragments\n", p,
                    n_frag);
        ++rifts;
      }
    }

    crust = std::move(next);
    for (Plate& p : plates) {
      p.delta = Mat3{};
    }
  };

  // ---- Simulation loop ----
  compute_front_fields(0.0);
  const double dt = tp.dt_myr;
  const double ema_alpha = std::clamp(dt / std::max(1.0, tp.uplift_window_myr), 0.0, 1.0);

  for (int s = 0; s < steps; ++s) {
    // 1. Advance rigid rotations (paper Section 3).
    for (Plate& p : plates) {
      const Mat3 stepR = axis_angle(p.axis, p.omega_rad_myr * dt);
      Mat3 acc;
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          acc.m[3 * r + c] = stepR.m[3 * r + 0] * p.delta.m[0 + c] +
                             stepR.m[3 * r + 1] * p.delta.m[3 + c] +
                             stepR.m[3 * r + 2] * p.delta.m[6 + c];
        }
      }
      p.delta = acc;
    }
    window_myr += dt;

    // 2. Per-cell crust evolution.
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_cells; ++i) {
      double z = crust.elevation_m[i];
      double uplift_rate_m_yr = 0.0;

      // Subduction uplift (Section 4.1): u_e = u0 * f(d) * g(v) * h(z~).
      const double d_km = fronts.dist_km[i];
      if (d_km < tp.subduction_distance_km) {
        const double f = front_distance_transfer(d_km, tp.subduction_distance_km);
        const double g = std::min(1.0, fronts.speed_mm_yr[i] / tp.max_plate_speed_mm_yr);
        const double zn = fronts.sub_znorm[i];
        const double h = zn * zn;
        const double ue_mm_yr = tp.subduction_uplift_mm_yr * f * g * h;
        z += ue_mm_yr * dt * 1000.0;  // mm/yr == m/ky -> m over dt My
        uplift_rate_m_yr = ue_mm_yr * 1e-3;
        if (ue_mm_yr > 0.02) {
          if (crust.orogeny_type[i] == 0) {
            crust.orogeny_type[i] = 1;  // Andean
          }
          crust.orogeny_age_myr[i] = 0.0f;
        }
      }

      // Elevation modifications (Section 4.5).
      if (crust.type[i] == 1) {
        if (z > 0.0) {
          z -= (z / tp.max_continental_m) * tp.continental_erosion_mm_yr * dt * 1000.0;
        }
      } else {
        z -= (1.0 - z / tp.trench_depth_m) * tp.oceanic_dampening_mm_yr * dt * 1000.0;
        if (z < tp.abyssal_m) {
          z += tp.sediment_accretion_mm_yr * dt * 1000.0;  // trench sediments
        }
        crust.age_myr[i] += static_cast<float>(dt);
      }
      if (crust.type[i] == 1 && crust.orogeny_type[i] != 0 && uplift_rate_m_yr <= 0.0) {
        crust.orogeny_age_myr[i] += static_cast<float>(dt);
      }

      crust.elevation_m[i] = static_cast<float>(std::clamp(z, tp.trench_depth_m,
                                                           tp.max_continental_m));
      crust.uplift_ema_m_yr[i] += static_cast<float>(
          (uplift_rate_m_yr - crust.uplift_ema_m_yr[i]) * ema_alpha);
    }

    // 3. Global resampling (+ collision/rift events, slab pull).
    if ((s + 1) % resample_every == 0) {
      global_resample(window_myr);
      compute_front_fields(window_myr);
      window_myr = 0.0;
    }
  }
  if (window_myr > 0.0) {
    global_resample(window_myr);
  }

  // ---- Rasterize canonical crust to the lat-lon dataset ----
  // After the final resample all plate deltas are identity, so a single
  // locate() per pixel suffices.
  auto& plate_layer = dataset.i32_layer("plate_id");
  auto& tectonic = dataset.float_layer("tectonic_elevation_m");
  auto& uplift = dataset.float_layer("uplift_rate_m_per_yr");
  auto& crust_layer = dataset.u8_layer("crust_type");
  auto& age_layer = dataset.float_layer("oceanic_age_myr");

  const int w = domain.width();
  const int h = domain.height();

  #pragma omp parallel for schedule(dynamic, 8)
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int idx = domain.index(x, y);
      const Vec3d p = domain.lat_lon_to_xyz(domain.center_lat_lon_deg(x, y));
      const auto lk = grid.locate(p);
      double elev = 0.0;
      double age = 0.0;
      double up = 0.0;
      int best = 0;
      for (int k = 0; k < 3; ++k) {
        const int cell = lk.cell[k];
        elev += lk.weight[k] * crust.elevation_m[cell];
        age += lk.weight[k] * crust.age_myr[cell];
        up += lk.weight[k] * crust.uplift_ema_m_yr[cell];
        if (lk.weight[k] > lk.weight[best]) {
          best = k;
        }
      }
      const int bc = lk.cell[best];
      plate_layer[idx] = crust.plate[bc];
      crust_layer[idx] = crust.type[bc];
      tectonic[idx] = static_cast<float>(elev);
      age_layer[idx] = (crust.type[bc] == 0) ? static_cast<float>(age) : 0.0f;
      // Small continental floor keeps the erosion solver's uplift field
      // non-degenerate away from active margins.
      const float floor = (crust.type[bc] == 1) ? 1e-5f : 0.0f;
      uplift[idx] = std::max(static_cast<float>(up), floor);
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
