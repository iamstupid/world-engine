#include "world_engine/terrain/procedural/stages/analytical_erosion_stage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <queue>
#include <vector>

namespace world_engine::terrain::procedural::stages {
namespace {
// Source: Analytical_Terrains_EG.pdf, Section 4, Eq (1), Eq (3), Eq (8), Eq (15), Eq (18).
// Source: Analytical_Terrains_EG.pdf, Section 5.1-5.2 (fixed-point + multigrid acceleration).
// Source: Analytical_Terrains_EG.pdf, Section 4.3 / Section 7 (receiver selection among lower neighbors).
// Source: Analytical_Terrains_EG.pdf, Section 6 (hillslope and thermal extensions).

uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x ^= (x >> 31);
  return x;
}

float deterministic_rand01(uint64_t key) {
  const uint64_t h = splitmix64(key);
  return static_cast<float>((h >> 11) & ((1ULL << 53) - 1)) *
         static_cast<float>(1.0 / static_cast<double>(1ULL << 53));
}

std::vector<float> downsample_average(const std::vector<float>& src, int sw, int sh, int dw, int dh) {
  std::vector<float> out(dw * dh, 0.0f);
  const int sx_scale = std::max(1, sw / dw);
  const int sy_scale = std::max(1, sh / dh);
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      double sum = 0.0;
      int count = 0;
      const int x0 = x * sx_scale;
      const int y0 = y * sy_scale;
      for (int ky = 0; ky < sy_scale; ++ky) {
        for (int kx = 0; kx < sx_scale; ++kx) {
          const int sx = std::min(sw - 1, x0 + kx);
          const int sy = std::min(sh - 1, y0 + ky);
          sum += src[sy * sw + sx];
          ++count;
        }
      }
      out[y * dw + x] = static_cast<float>(sum / std::max(1, count));
    }
  }
  return out;
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

// 8-neighbor indices: E, NE, N, NW, W, SW, S, SE
void get_8_neighbors(const TerrainDomain& domain, int x, int y, int nb[8]) {
  nb[0] = domain.index(x + 1, y);           // E
  nb[1] = north_neighbor(domain, x + 1, y); // NE
  nb[2] = north_neighbor(domain, x, y);     // N
  nb[3] = north_neighbor(domain, x - 1, y); // NW
  nb[4] = domain.index(x - 1, y);           // W
  nb[5] = south_neighbor(domain, x - 1, y); // SW
  nb[6] = south_neighbor(domain, x, y);     // S
  nb[7] = south_neighbor(domain, x + 1, y); // SE
}

// Distance to neighbor by direction index: 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE
float neighbor_distance_m(const TerrainDomain& domain, int y, int dir) {
  const float ew = static_cast<float>(domain.east_west_spacing_m(y));
  const float ns = static_cast<float>(domain.north_south_spacing_m());
  switch (dir) {
    case 0: case 4: return ew;
    case 2: case 6: return ns;
    default: return std::sqrt(ew * ew + ns * ns);
  }
}

// Compute distance from cell to a specific neighbor index
float distance_to_neighbor(const TerrainDomain& domain, int x, int y, int nb_idx) {
  const auto [nx, ny] = domain.unindex(nb_idx);
  const int dx = nx - x;
  const int dy = ny - y;
  // Detect diagonal vs cardinal
  if (dx == 0 || (dx == domain.width() / 2))  // pure N/S (or polar wrap)
    return static_cast<float>(domain.north_south_spacing_m());
  if (dy == 0)  // pure E/W
    return static_cast<float>(domain.east_west_spacing_m(y));
  // diagonal
  const float ew = static_cast<float>(domain.east_west_spacing_m(y));
  const float ns = static_cast<float>(domain.north_south_spacing_m());
  return std::sqrt(ew * ew + ns * ns);
}

std::vector<uint8_t> build_outlet_mask(const std::vector<float>& z, float sea_level_m) {
  const int n = static_cast<int>(z.size());
  std::vector<uint8_t> outlet(n, 0);
  int count = 0;
  for (int i = 0; i < n; ++i) {
    if (z[i] <= sea_level_m) {
      outlet[i] = 1;
      ++count;
    }
  }

  const int min_outlets = std::max(1, n / 200);
  if (count >= min_outlets) {
    return outlet;
  }

  std::vector<float> tmp = z;
  std::nth_element(tmp.begin(), tmp.begin() + (min_outlets - 1), tmp.end());
  const float threshold = tmp[min_outlets - 1];
  for (int i = 0; i < n; ++i) {
    if (z[i] <= threshold) {
      outlet[i] = 1;
    }
  }
  return outlet;
}

struct FilledResult {
  std::vector<float> filled_z;
  std::vector<int32_t> flood_parent;
};

// Priority flood with 8-connectivity for better depression handling
FilledResult fill_depressions_with_outlets(const TerrainDomain& domain, const std::vector<float>& z,
                                           std::vector<uint8_t> outlet) {
  struct Node {
    float h = 0.0f;
    int idx = 0;
  };
  auto cmp = [](const Node& a, const Node& b) { return a.h > b.h; };
  std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

  const int n = static_cast<int>(z.size());
  std::vector<float> filled = z;
  std::vector<int32_t> parent(n, -1);
  std::vector<uint8_t> visited(n, 0);

  int seeded = 0;
  for (int i = 0; i < n; ++i) {
    if (outlet[i]) {
      visited[i] = 1;
      pq.push({filled[i], i});
      ++seeded;
    }
  }
  if (seeded == 0) {
    int min_idx = 0;
    for (int i = 1; i < n; ++i) {
      if (filled[i] < filled[min_idx]) {
        min_idx = i;
      }
    }
    visited[min_idx] = 1;
    outlet[min_idx] = 1;
    pq.push({filled[min_idx], min_idx});
  }

  while (!pq.empty()) {
    const Node cur = pq.top();
    pq.pop();
    const auto [x, y] = domain.unindex(cur.idx);

    // Use 8-connectivity for flood fill
    int nb[8];
    get_8_neighbors(domain, x, y, nb);

    for (int nidx : nb) {
      if (visited[nidx]) {
        continue;
      }
      visited[nidx] = 1;
      parent[nidx] = cur.idx;
      filled[nidx] = std::max(filled[nidx], cur.h);
      pq.push({filled[nidx], nidx});
    }
  }
  return {std::move(filled), std::move(parent)};
}

// Build receivers using 8-connectivity with slope-weighted random selection
void build_receivers(const TerrainDomain& domain, const std::vector<float>& z,
                     const std::vector<int32_t>& flood_parent, const std::vector<uint8_t>& outlet,
                     const std::vector<float>& rnd, std::vector<int32_t>& receiver,
                     std::vector<int>& receiver_dir) {
  const int w = domain.width();
  const int h = domain.height();
  const int n = w * h;
  receiver.assign(n, 0);
  receiver_dir.assign(n, 0);

  #pragma omp parallel for schedule(static)
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int idx = domain.index(x, y);
      if (outlet[idx]) {
        receiver[idx] = idx;
        receiver_dir[idx] = -1;
        continue;
      }

      int nb[8];
      get_8_neighbors(domain, x, y, nb);

      // Compute slope to each of 8 neighbors, weight by slope (steeper = more likely)
      int lower_ids[8];
      int lower_dirs[8];
      float lower_w[8];
      int lower_count = 0;
      float wsum = 0.0f;

      for (int k = 0; k < 8; ++k) {
        const float dist = std::max(1.0f, neighbor_distance_m(domain, y, k));
        const float dz = z[idx] - z[nb[k]];
        if (dz > 0.0f) {
          const float slope = dz / dist;  // weight by slope, not just dz
          lower_ids[lower_count] = nb[k];
          lower_dirs[lower_count] = k;
          lower_w[lower_count] = slope;
          wsum += slope;
          ++lower_count;
        }
      }

      if (lower_count > 0) {
        float u = rnd[idx] * wsum;
        int pick = lower_count - 1;
        for (int i = 0; i < lower_count; ++i) {
          u -= lower_w[i];
          if (u <= 0.0f) {
            pick = i;
            break;
          }
        }
        receiver[idx] = lower_ids[pick];
        receiver_dir[idx] = lower_dirs[pick];
      } else if (flood_parent[idx] >= 0) {
        receiver[idx] = flood_parent[idx];
        receiver_dir[idx] = -1;
      } else {
        receiver[idx] = idx;
        receiver_dir[idx] = -1;
      }
    }
  }
}

void flow_accumulation(const TerrainDomain& domain, const std::vector<float>& z,
                       const std::vector<int32_t>& receiver, std::vector<float>& area) {
  const int n = static_cast<int>(z.size());
  area.assign(n, 0.0f);
  for (int i = 0; i < n; ++i) {
    const auto [x, y] = domain.unindex(i);
    (void)x;
    area[i] = static_cast<float>(domain.cell_area_m2(y));
  }

  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return z[a] > z[b]; });

  for (int idx : order) {
    const int r = receiver[idx];
    if (r != idx) {
      area[r] += area[idx];
    }
  }
}

// Jittered bilinear upsample for the multigrid prolongation (Tzathas 5.2):
// plain upsampling makes rivers follow coarse-block boundaries with sharp
// turns; a deterministic sub-cell offset in [-0.25, 0.25]^2 breaks this.
std::vector<float> upsample_bilinear_jitter(const std::vector<float>& src, int sw, int sh,
                                            int dw, int dh, int seed, int level) {
  std::vector<float> out(dw * dh, 0.0f);
  #pragma omp parallel for schedule(static)
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      const uint64_t key = static_cast<uint64_t>(seed) * 0x9E3779B1ULL +
                           static_cast<uint64_t>(level) * 0x85EBCA77ULL +
                           static_cast<uint64_t>(y) * 0xC2B2AE3DULL +
                           static_cast<uint64_t>(x);
      const float ju = (deterministic_rand01(key) - 0.5f) * 0.5f;
      const float jv = (deterministic_rand01(key + 0x27D4EB2FULL) - 0.5f) * 0.5f;
      const float v = (static_cast<float>(y) + 0.5f + jv) * static_cast<float>(sh) /
                          static_cast<float>(dh) -
                      0.5f;
      const float u = (static_cast<float>(x) + 0.5f + ju) * static_cast<float>(sw) /
                          static_cast<float>(dw) -
                      0.5f;
      const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, sh - 1);
      const int y1 = std::clamp(y0 + 1, 0, sh - 1);
      const float ty = std::clamp(v - static_cast<float>(y0), 0.0f, 1.0f);
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

void apply_thermal_constraint(const TerrainDomain& domain, float critical_slope,
                              const std::vector<int32_t>& receiver,
                              const std::vector<int>& receiver_dir,
                              std::vector<float>& z) {
  for (int y = 0; y < domain.height(); ++y) {
    for (int x = 0; x < domain.width(); ++x) {
      const int idx = domain.index(x, y);
      const int r = receiver[idx];
      if (r == idx) {
        continue;
      }
      const int dir = receiver_dir[idx];
      float dist;
      if (dir >= 0) {
        dist = std::max(1.0f, neighbor_distance_m(domain, y, dir));
      } else {
        dist = std::max(1.0f, distance_to_neighbor(domain, x, y, r));
      }
      const float max_drop = critical_slope * dist;
      const float dz = z[idx] - z[r];
      if (dz > max_drop) {
        z[idx] = z[r] + max_drop;
      }
    }
  }
}

void solve_fixed_point_level(const TerrainDomain& domain, const ErosionParams& eparams, int seed,
                             const std::vector<float>& z0, const std::vector<float>& uplift, float sea_level_m,
                             std::vector<float>& z_work, int iterations, int level_id) {
  const int n = static_cast<int>(z_work.size());
  std::vector<float> rnd(n, 0.0f);
  for (int i = 0; i < n; ++i) {
    rnd[i] = deterministic_rand01(static_cast<uint64_t>(seed) * 1315423911ULL +
                                  static_cast<uint64_t>(level_id) * 2654435761ULL +
                                  static_cast<uint64_t>(i));
  }

  const std::vector<uint8_t> outlet = build_outlet_mask(z0, sea_level_m);
  std::vector<int32_t> receiver(n, 0);
  std::vector<int> receiver_dir(n, 0);
  std::vector<float> area(n, 0.0f);
  std::vector<float> z_new(n, 0.0f);
  std::vector<int> asc(n);
  std::iota(asc.begin(), asc.end(), 0);

  const float dt = static_cast<float>(eparams.time_years);
  for (int it = 0; it < iterations; ++it) {
    std::cout << "    [erosion] level-id " << level_id << " iteration " << (it + 1) << "/" << iterations
              << "\n";
    const auto filled = fill_depressions_with_outlets(domain, z_work, outlet);
    build_receivers(domain, filled.filled_z, filled.flood_parent, outlet, rnd, receiver, receiver_dir);
    flow_accumulation(domain, filled.filled_z, receiver, area);

    std::stable_sort(asc.begin(), asc.end(),
                     [&](int a, int b) { return filled.filled_z[a] < filled.filled_z[b]; });

    for (int idx : asc) {
      if (outlet[idx]) {
        // FIX: Use sea_level_m as the effective outlet elevation, not the
        // ocean floor depth. This prevents rivers from eroding below sea level.
        z_new[idx] = sea_level_m;
        continue;
      }

      const int r = receiver[idx];
      if (r == idx) {
        z_new[idx] = z_work[idx];
        continue;
      }

      // Compute distance using direction index for proper diagonal distances
      const int dir = receiver_dir[idx];
      float dist;
      if (dir >= 0) {
        const auto [x, y] = domain.unindex(idx);
        dist = std::max(1.0f, neighbor_distance_m(domain, y, dir));
      } else {
        const auto [x, y] = domain.unindex(idx);
        dist = std::max(1.0f, distance_to_neighbor(domain, x, y, r));
      }

      // Fluvial advection velocity a(s) = k * A^m ...
      float a = static_cast<float>(eparams.k * std::pow(std::max(1.0f, area[idx]), eparams.m));
      // ... with the single-receiver slope correction (Tzathas Section 4.3):
      // a *= dist * ||grad z|| / (z - z_r), gradient from per-axis downstream
      // differences. Removes axis-aligned isoline artifacts.
      {
        const auto [x, y] = domain.unindex(idx);
        const float zc = filled.filled_z[idx];
        const float ze = filled.filled_z[domain.index(x + 1, y)];
        const float zw = filled.filled_z[domain.index(x - 1, y)];
        const float zn = filled.filled_z[north_neighbor(domain, x, y)];
        const float zs = filled.filled_z[south_neighbor(domain, x, y)];
        const float ew = std::max(1.0f, static_cast<float>(domain.east_west_spacing_m(y)));
        const float ns = std::max(1.0f, static_cast<float>(domain.north_south_spacing_m()));
        const float gx = std::max(0.0f, zc - std::min(ze, zw)) / ew;
        const float gy = std::max(0.0f, zc - std::min(zn, zs)) / ns;
        const float grad = std::sqrt(gx * gx + gy * gy);
        const float drop = zc - filled.filled_z[r];
        if (drop > 1e-3f && grad > 0.0f) {
          const float corr = std::clamp(dist * grad / drop, 0.5f, 2.5f);
          a *= corr;
        }
      }
      // Hillslope contribution enters the advection coefficient
      // (Tzathas Eq 26): a += (k_h / C) * A^(-h).
      if (eparams.enable_hillslope) {
        a += static_cast<float>(
            (eparams.hillslope_k / eparams.hack_c) *
            std::pow(std::max(1.0f, area[idx]), -eparams.hack_h));
      }
      const float denom = std::max(1e-8f, a);
      const float lambda = std::exp(-denom * dt / dist);
      // Use the effective receiver elevation (already sea_level for outlets)
      const float z_r = z_new[r];
      const float target = z_r + uplift[idx] * dist / denom;
      z_new[idx] = z0[idx] * lambda + target * (1.0f - lambda);

      // FIX: Clamp eroded elevation to not go below sea level for land cells
      if (z0[idx] > sea_level_m) {
        z_new[idx] = std::max(z_new[idx], sea_level_m);
      }
    }

    if (eparams.enable_thermal) {
      apply_thermal_constraint(domain, static_cast<float>(eparams.thermal_critical_slope),
                               receiver, receiver_dir, z_new);
    }
    // Fixed-point damping (Tzathas 5.1): blend with the previous iterate to
    // prevent oscillation at small t / large initial discontinuities.
    const float ema = std::clamp(static_cast<float>(eparams.fixed_point_ema), 0.05f, 1.0f);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      z_new[i] = ema * z_new[i] + (1.0f - ema) * z_work[i];
      if (outlet[i]) {
        z_new[i] = sea_level_m;
      }
    }

    z_work.swap(z_new);
  }
}

void smooth_polar_rows(const TerrainDomain& domain, std::vector<float>& z, int rows) {
  const int w = domain.width();
  const int h = domain.height();
  if (w <= 0 || h <= 0) {
    return;
  }
  const int rmax = std::clamp(rows, 1, std::max(1, h / 8));
  const std::vector<float> src = z;

  auto row_idx = [w](int x, int y) { return y * w + x; };
  for (int r = 0; r < rmax; ++r) {
    const float alpha = 0.65f * (static_cast<float>(rmax - r) / static_cast<float>(rmax));
    const int y_top = r;
    const int y_ref_top = std::min(h - 1, rmax);
    const int y_bot = h - 1 - r;
    const int y_ref_bot = std::max(0, h - 1 - rmax);

    for (int x = 0; x < w; ++x) {
      const int x_flip = (x + w / 2) % w;
      const float top_target = 0.5f * (src[row_idx(x, y_ref_top)] + src[row_idx(x_flip, y_ref_top)]);
      const float bot_target = 0.5f * (src[row_idx(x, y_ref_bot)] + src[row_idx(x_flip, y_ref_bot)]);
      z[row_idx(x, y_top)] = src[row_idx(x, y_top)] * (1.0f - alpha) + top_target * alpha;
      z[row_idx(x, y_bot)] = src[row_idx(x, y_bot)] * (1.0f - alpha) + bot_target * alpha;
    }
  }
}

std::vector<int> distribute_multigrid_iterations(int total_iterations, int levels) {
  const int safe_levels = std::max(1, levels);
  int remaining = std::max(1, total_iterations);
  std::vector<int> per_level(safe_levels, 0);

  for (int level = 0; level < safe_levels && remaining > 0; ++level) {
    per_level[level] = 1;
    --remaining;
  }

  int level = 0;
  while (remaining > 0) {
    per_level[level] += 1;
    --remaining;
    level = (level + 1) % safe_levels;
  }

  return per_level;
}
}  // namespace

void run_analytical_erosion_stage(const PipelineParams& params, TerrainDataset& dataset) {
  if (!dataset.has_layer("elevation_eroded_m")) {
    dataset.create_float_layer("elevation_eroded_m", "m", "Analytical stream-power eroded elevation");
  }

  const auto& base = dataset.float_layer("elevation_base_m");
  const auto& uplift = dataset.float_layer("uplift_rate_m_per_yr");
  auto& eroded = dataset.float_layer("elevation_eroded_m");
  eroded = base;

  const auto& domain = dataset.domain();
  const int w = domain.width();
  const int h = domain.height();
  const int levels = std::max(1, params.erosion.multigrid_levels);
  const std::vector<int> iter_budget =
      distribute_multigrid_iterations(params.erosion.fixed_point_iterations, levels);

  std::vector<float> z_guess = eroded;
  for (int level = levels - 1; level >= 0; --level) {
    const int factor = 1 << level;
    const int cw = std::max(2, w / factor);
    const int ch = std::max(2, h / factor);
    const TerrainDomain cdom(cw, ch, domain.radius_m());

    std::vector<float> z0c = (factor == 1) ? base : downsample_average(base, w, h, cw, ch);
    std::vector<float> uc = (factor == 1) ? uplift : downsample_average(uplift, w, h, cw, ch);
    std::vector<float> zg = (factor == 1) ? z_guess : downsample_average(z_guess, w, h, cw, ch);

    const int iter_count = iter_budget[level];
    if (iter_count <= 0) {
      continue;
    }
    std::cout << "  [erosion] multigrid level " << (levels - level) << "/" << levels << " grid " << cw << "x"
              << ch << " iterations " << iter_count << "\n";
    solve_fixed_point_level(cdom, params.erosion, params.seed, z0c, uc, params.hydrology.sea_level_m, zg,
                            iter_count, level);

    if (factor == 1) {
      z_guess = std::move(zg);
    } else {
      z_guess = upsample_bilinear_jitter(zg, cw, ch, w, h, params.seed, level);
    }
  }

  // Restore ocean bathymetry from base elevation
  for (int i = 0; i < w * h; ++i) {
    if (base[i] <= params.hydrology.sea_level_m) {
      z_guess[i] = base[i];
    }
  }

  smooth_polar_rows(domain, z_guess, 10);
  eroded = std::move(z_guess);
}

}  // namespace world_engine::terrain::procedural::stages
