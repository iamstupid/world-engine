#include "world_engine/terrain/procedural/stages/tectonics_stage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace world_engine::terrain::procedural::stages {
namespace {
// Source: 2019-Procedural-Tectonic-Planets.pdf, Section 4, model paragraph.
// Surface speed relation: s(p) = omega * (w x p).
// Source: 2019-Procedural-Tectonic-Planets.pdf, Section 6.
// Initial near-uniform plate seeds use Fibonacci sphere sampling.

struct V3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double norm(const V3& v) { return std::sqrt(dot(v, v)); }

V3 normalize(const V3& v) {
  const double n = norm(v);
  if (n <= 1e-12) {
    return {0.0, 1.0, 0.0};
  }
  return {v.x / n, v.y / n, v.z / n};
}

V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 add(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 mul(const V3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }

V3 rotate_axis_angle(const V3& v, const V3& axis_unit, double angle) {
  const double c = std::cos(angle);
  const double s = std::sin(angle);
  const V3 term1 = mul(v, c);
  const V3 term2 = mul(cross(axis_unit, v), s);
  const V3 term3 = mul(axis_unit, dot(axis_unit, v) * (1.0 - c));
  return add(add(term1, term2), term3);
}

uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x ^= (x >> 31);
  return x;
}

double unit_rand01(uint64_t key) {
  const uint64_t h = splitmix64(key);
  return static_cast<double>((h >> 11) & ((1ULL << 53) - 1)) *
         (1.0 / static_cast<double>(1ULL << 53));
}

std::vector<V3> fibonacci_sphere_points(int n) {
  std::vector<V3> pts;
  pts.reserve(n);
  constexpr double kPi = 3.14159265358979323846;
  const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
  for (int i = 0; i < n; ++i) {
    const double u = (i + 0.5) / static_cast<double>(n);
    const double y = 1.0 - 2.0 * u;
    const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
    const double theta = 2.0 * kPi * i / phi;
    pts.push_back({std::cos(theta) * r, y, std::sin(theta) * r});
  }
  return pts;
}

int north_neighbor(const TerrainDomain& domain, int x, int y) {
  if (y > 0) {
    return domain.index(x, y - 1);
  }
  return domain.index(x + domain.width() / 2, 1);
}

int south_neighbor(const TerrainDomain& domain, int x, int y) {
  if (y < domain.height() - 1) {
    return domain.index(x, y + 1);
  }
  return domain.index(x + domain.width() / 2, domain.height() - 2);
}

void blur_scalar_field(const TerrainDomain& domain, std::vector<float>& field, int passes) {
  std::vector<float> tmp(field.size(), 0.0f);
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < domain.height(); ++y) {
      for (int x = 0; x < domain.width(); ++x) {
        const int c = domain.index(x, y);
        const int l = domain.index(x - 1, y);
        const int r = domain.index(x + 1, y);
        const int u = north_neighbor(domain, x, y);
        const int d = south_neighbor(domain, x, y);
        tmp[c] = (field[c] * 2.0f + field[l] + field[r] + field[u] + field[d]) / 6.0f;
      }
    }
    field.swap(tmp);
  }
}

std::vector<float> upsample_bilinear(const std::vector<float>& src, int sw, int sh, int dw, int dh) {
  std::vector<float> out(dw * dh, 0.0f);
  for (int y = 0; y < dh; ++y) {
    const float v = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
    const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, sh - 1);
    const int y1 = std::clamp(y0 + 1, 0, sh - 1);
    const float ty = std::clamp(v - static_cast<float>(y0), 0.0f, 1.0f);
    for (int x = 0; x < dw; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
      int x0 = static_cast<int>(std::floor(u));
      int x1 = x0 + 1;
      const float tx = std::clamp(u - static_cast<float>(x0), 0.0f, 1.0f);
      x0 = (x0 % sw + sw) % sw;
      x1 = (x1 % sw + sw) % sw;

      const float c00 = src[y0 * sw + x0];
      const float c10 = src[y0 * sw + x1];
      const float c01 = src[y1 * sw + x0];
      const float c11 = src[y1 * sw + x1];
      const float c0 = c00 + (c10 - c00) * tx;
      const float c1 = c01 + (c11 - c01) * tx;
      out[y * dw + x] = c0 + (c1 - c0) * ty;
    }
  }
  return out;
}

std::vector<int32_t> upsample_nearest_i32(const std::vector<int32_t>& src, int sw, int sh, int dw, int dh) {
  std::vector<int32_t> out(dw * dh, 0);
  for (int y = 0; y < dh; ++y) {
    int sy = static_cast<int>((static_cast<double>(y) + 0.5) * static_cast<double>(sh) /
                              static_cast<double>(dh));
    sy = std::clamp(sy, 0, sh - 1);
    for (int x = 0; x < dw; ++x) {
      int sx = static_cast<int>((static_cast<double>(x) + 0.5) * static_cast<double>(sw) /
                                static_cast<double>(dw));
      sx = (sx % sw + sw) % sw;
      out[y * dw + x] = src[sy * sw + sx];
    }
  }
  return out;
}
}  // namespace

void run_tectonics_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& domain = dataset.domain();
  const int n = dataset.size();

  if (!dataset.has_layer("plate_id")) {
    dataset.create_i32_layer("plate_id", "id", "Tectonic plate assignment");
  }
  if (!dataset.has_layer("tectonic_elevation_m")) {
    dataset.create_float_layer("tectonic_elevation_m", "m",
                               "Large-scale tectonic elevation contribution");
  }
  if (!dataset.has_layer("uplift_rate_m_per_yr")) {
    dataset.create_float_layer("uplift_rate_m_per_yr", "m/yr",
                               "Tectonic uplift rate used by analytical erosion");
  }

  auto& plate_id = dataset.i32_layer("plate_id");
  auto& tectonic = dataset.float_layer("tectonic_elevation_m");
  auto& uplift = dataset.float_layer("uplift_rate_m_per_yr");

  const int plate_count = std::max(2, params.tectonics.plate_count);
  const auto seeds0 = fibonacci_sphere_points(plate_count);

  std::vector<V3> ang_vel(plate_count);
  std::vector<V3> warp_dir1(plate_count);
  std::vector<V3> warp_dir2(plate_count);
  std::vector<double> phase1(plate_count, 0.0);
  std::vector<double> phase2(plate_count, 0.0);
  for (int p = 0; p < plate_count; ++p) {
    const uint64_t k0 = static_cast<uint64_t>(params.seed) * 1315423911ULL +
                        static_cast<uint64_t>(p) * 2654435761ULL;
    const double u1 = unit_rand01(k0 + 1);
    const double u2 = unit_rand01(k0 + 2);
    const double u3 = unit_rand01(k0 + 3);
    const double z = 2.0 * u1 - 1.0;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double theta = 2.0 * 3.14159265358979323846 * u2;
    const V3 axis = normalize(V3{r * std::cos(theta), z, r * std::sin(theta)});
    const double omega = 0.25 + 0.75 * u3;
    ang_vel[p] = mul(axis, omega);

    const auto rand_dir = [&](uint64_t k) -> V3 {
      const double a1 = unit_rand01(k + 10);
      const double a2 = unit_rand01(k + 11);
      const double zz = 2.0 * a1 - 1.0;
      const double rr = std::sqrt(std::max(0.0, 1.0 - zz * zz));
      const double tt = 2.0 * 3.14159265358979323846 * a2;
      return {rr * std::cos(tt), zz, rr * std::sin(tt)};
    };
    warp_dir1[p] = rand_dir(k0 + 21);
    warp_dir2[p] = rand_dir(k0 + 37);
    phase1[p] = unit_rand01(k0 + 51) * 6.283185307179586;
    phase2[p] = unit_rand01(k0 + 63) * 6.283185307179586;
  }

  const int cw = std::min(domain.width(), 1024);
  const int ch = std::min(domain.height(), 512);
  const TerrainDomain cdom(cw, ch, domain.radius_m());
  const int cn = cw * ch;
  std::vector<V3> cpos(cn);
  for (int y = 0; y < ch; ++y) {
    for (int x = 0; x < cw; ++x) {
      const int i = y * cw + x;
      const auto ll = cdom.center_lat_lon_deg(x, y);
      const auto p = cdom.lat_lon_to_xyz(ll);
      cpos[i] = {p.x, p.y, p.z};
    }
  }

  const double total_myr = std::max(1.0, static_cast<double>(params.tectonics.simulation_steps) *
                                              params.tectonics.dt_myr);
  const int sample_count =
      std::clamp(static_cast<int>(std::round(total_myr / 12.5)), 6, 24);  // 250 My -> 20 samples.
  const double angle_per_myr = 0.006;  // tuned to avoid unrealistic full-turn drift.

  std::vector<float> interaction(cn, 0.0f);
  std::vector<int32_t> plate_coarse(cn, 0);
  std::vector<V3> moved_seed(plate_count);

  for (int s = 0; s < sample_count; ++s) {
    const double t_myr =
        (sample_count == 1) ? 0.0 : (static_cast<double>(s) / static_cast<double>(sample_count - 1)) * total_myr;

    for (int p = 0; p < plate_count; ++p) {
      const V3 axis = normalize(ang_vel[p]);
      const double omega = norm(ang_vel[p]);
      const double angle = omega * angle_per_myr * t_myr;
      moved_seed[p] = rotate_axis_angle(seeds0[p], axis, angle);
    }

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < cn; ++i) {
      const V3 p = cpos[i];
      int p1 = 0;
      int p2 = 1;
      double best = -1e30;
      double second = -1e30;

      for (int k = 0; k < plate_count; ++k) {
        const double base = dot(p, moved_seed[k]);
        const double warp = 0.05 * std::sin(9.0 * dot(p, warp_dir1[k]) + phase1[k]) +
                            0.03 * std::sin(17.0 * dot(p, warp_dir2[k]) + phase2[k]);
        const double score = base + warp;
        if (score > best) {
          second = best;
          p2 = p1;
          best = score;
          p1 = k;
        } else if (score > second) {
          second = score;
          p2 = k;
        }
      }

      if (s == sample_count - 1) {
        plate_coarse[i] = p1;
      }

      const double gap = best - second;
      // Sharper boundary detection: higher factor = narrower mountain belts
      const double boundary = std::exp(-std::max(0.0, gap) * 35.0);

      const V3 v1 = cross(ang_vel[p1], p);
      const V3 v2 = cross(ang_vel[p2], p);
      const V3 d = sub(moved_seed[p2], moved_seed[p1]);
      const V3 tangent = normalize(sub(d, mul(p, dot(d, p))));
      const double rel = dot(sub(v2, v1), tangent);
      // Convergent boundaries get strong uplift; divergent get mild rifting
      const double c = (rel < 0.0) ? (-rel) : (-0.35 * rel);
      interaction[i] += static_cast<float>(boundary * c);
    }
  }

  for (float& v : interaction) {
    v /= static_cast<float>(sample_count);
  }

  // Fewer blur passes for sharper/narrower mountain belts
  blur_scalar_field(cdom, interaction,
                    static_cast<int>(std::max(4.0, params.tectonics.boundary_width_px * 2.0)));

  std::vector<float> interaction_full;
  std::vector<int32_t> plate_full;
  if (cw == domain.width() && ch == domain.height()) {
    interaction_full = interaction;
    plate_full = plate_coarse;
  } else {
    interaction_full = upsample_bilinear(interaction, cw, ch, domain.width(), domain.height());
    plate_full = upsample_nearest_i32(plate_coarse, cw, ch, domain.width(), domain.height());
  }
  plate_id = std::move(plate_full);

  float max_abs = 1e-6f;
  for (float v : interaction_full) {
    max_abs = std::max(max_abs, std::abs(v));
  }

  for (int i = 0; i < n; ++i) {
    // Use a sharper response: signed square root preserves narrow peaks better than tanh
    const float normed = interaction_full[i] / max_abs;
    const float sign = (normed >= 0.0f) ? 1.0f : -1.0f;
    const float shaped = sign * std::sqrt(std::abs(normed));
    tectonic[i] = shaped * static_cast<float>(params.tectonics.uplift_scale_m);
  }

  // Reduced blur at full resolution for sharper mountain belts
  blur_scalar_field(domain, tectonic,
                    static_cast<int>(std::max(4.0, params.tectonics.boundary_width_px * 3.0)));

  const double total_years = std::max(1.0, total_myr * 1'000'000.0);
  for (int i = 0; i < n; ++i) {
    uplift[i] = static_cast<float>(std::max(0.0f, tectonic[i]) / total_years);
  }
}

void run_combine_stage(const PipelineParams& params, TerrainDataset& dataset) {
  if (!dataset.has_layer("elevation_base_m")) {
    dataset.create_float_layer("elevation_base_m", "m", "Primeval + tectonic base elevation");
  }

  const auto& prime = dataset.float_layer("elevation_primeval_m");
  const auto& tectonic = dataset.float_layer("tectonic_elevation_m");
  auto& base = dataset.float_layer("elevation_base_m");
  const float mix = static_cast<float>(std::clamp(params.tectonics.tectonic_mix, 0.0, 1.0));
  const float sea = params.hydrology.sea_level_m;

  for (int i = 0; i < dataset.size(); ++i) {
    float z = prime[i] + tectonic[i] * mix;

    // Create continental shelf effect: land near sea level gets a gentle slope
    // instead of an abrupt transition to deep ocean
    if (z > sea && z < sea + 200.0f) {
      // Coastal lowlands: flatten slightly to create plains
      const float t = (z - sea) / 200.0f;
      z = sea + t * t * 200.0f;  // quadratic ramp creates coastal plains
    } else if (z < sea && z > sea - 300.0f) {
      // Continental shelf: gentle slope near coastline
      const float t = (sea - z) / 300.0f;
      z = sea - t * t * 300.0f;  // shallow shelf break
    }

    base[i] = z;
  }
}

}  // namespace world_engine::terrain::procedural::stages
