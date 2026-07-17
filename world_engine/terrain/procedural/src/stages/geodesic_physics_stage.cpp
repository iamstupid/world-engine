#include "world_engine/terrain/procedural/stages/geodesic_physics_stage.h"

// Dense physics on the icosahedral geodesic grid (plan milestone M6).
//
// The analytical erosion solver (Tzathas et al. 2024) is receiver-graph
// based and the hydrology products are flow-accumulation based, so both
// generalize from the raster to the geodesic cell graph directly:
//   - neighbors come from GeodesicGrid (5/6-connectivity, CW order)
//   - edge lengths are great-circle distances, cell areas are exact
//   - the multigrid hierarchy uses the frequency nesting F/2 -> F (coarse
//     cell (r, c) coincides with fine cell (2r, 2c))
//   - prolongation locates jittered fine-cell directions in the coarse grid
//     (the spherical version of the paper's +-0.25 cell jitter)
// Inputs (base elevation, uplift) are sampled from the raster layers by
// bilinear interpolation; results are rasterized back through locate().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <vector>

#include "FastNoiseLite.h"
#include "world_engine/terrain/geodesic_grid.h"

namespace world_engine::terrain::procedural::stages {
namespace {

constexpr double kPi = 3.14159265358979323846;

uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x ^= (x >> 31);
  return x;
}

float rand01(uint64_t key) {
  return static_cast<float>((splitmix64(key) >> 11) * (1.0 / 9007199254740992.0));
}

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d cross(const Vec3d& a, const Vec3d& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3d normalized(const Vec3d& v) {
  const double n = std::sqrt(dot(v, v));
  return {v.x / n, v.y / n, v.z / n};
}

// Bilinear sample of a lat-lon raster layer at a unit direction.
float sample_raster(const TerrainDomain& domain, const std::vector<float>& layer,
                    const Vec3d& dir) {
  const LatLon ll = domain.xyz_to_lat_lon(dir);
  const int w = domain.width();
  const int h = domain.height();
  const double u = (ll.lon_deg + 180.0) / 360.0 * w - 0.5;
  const double v = (90.0 - ll.lat_deg) / 180.0 * h - 0.5;
  int x0 = static_cast<int>(std::floor(u));
  const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, h - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float tx = static_cast<float>(u - std::floor(u));
  const float ty = std::clamp(static_cast<float>(v - y0), 0.0f, 1.0f);
  const int x1 = ((x0 + 1) % w + w) % w;
  x0 = (x0 % w + w) % w;
  const float c00 = layer[y0 * w + x0];
  const float c10 = layer[y0 * w + x1];
  const float c01 = layer[y1 * w + x0];
  const float c11 = layer[y1 * w + x1];
  const float c0 = c00 + (c10 - c00) * tx;
  const float c1 = c01 + (c11 - c01) * tx;
  return c0 + (c1 - c0) * ty;
}

// Bilinear sample of a painted override raster at a unit direction (y-up
// lat/lon convention shared with TerrainDomain).
float sample_paint(const PaintLayer& paint, const Vec3d& dir) {
  if (paint.width <= 0 || paint.height <= 0 || paint.data.empty()) {
    return 0.0f;
  }
  const double lat = std::asin(std::clamp(dir.y, -1.0, 1.0));
  const double lon = std::atan2(dir.z, dir.x);
  const int w = paint.width;
  const int h = paint.height;
  const double u = (lon / kPi * 180.0 + 180.0) / 360.0 * w - 0.5;
  const double v = (90.0 - lat / kPi * 180.0) / 180.0 * h - 0.5;
  int x0 = static_cast<int>(std::floor(u));
  const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, h - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float tx = static_cast<float>(u - std::floor(u));
  const float ty = std::clamp(static_cast<float>(v - y0), 0.0f, 1.0f);
  const int x1 = ((x0 + 1) % w + w) % w;
  x0 = (x0 % w + w) % w;
  const float c0 = paint.data[y0 * w + x0] +
                   (paint.data[y0 * w + x1] - paint.data[y0 * w + x0]) * tx;
  const float c1 = paint.data[y1 * w + x0] +
                   (paint.data[y1 * w + x1] - paint.data[y1 * w + x0]) * tx;
  return c0 + (c1 - c0) * ty;
}

// Per-cell neighbor cache (indices + edge lengths in meters).
struct CellGraph {
  const GeodesicGrid* grid = nullptr;
  double radius_m = 0.0;
  std::vector<int32_t> nb;       // n_cells * 6 (unused slots = -1)
  std::vector<float> edge_m;     // n_cells * 6
  std::vector<uint8_t> degree;   // 5 or 6
  std::vector<float> area_m2;

  void build(const GeodesicGrid& g, double radius) {
    grid = &g;
    radius_m = radius;
    const int n = g.cell_count();
    nb.assign(static_cast<size_t>(n) * 6, -1);
    edge_m.assign(static_cast<size_t>(n) * 6, 0.0f);
    degree.assign(n, 0);
    area_m2.assign(n, 0.0f);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      std::array<int, 6> local{};
      const int m = g.neighbors(i, local);
      degree[i] = static_cast<uint8_t>(m);
      const Vec3d& ci = g.cell_center(i);
      for (int k = 0; k < m; ++k) {
        nb[static_cast<size_t>(i) * 6 + k] = local[k];
        const Vec3d& cj = g.cell_center(local[k]);
        const double ang = std::acos(std::clamp(dot(ci, cj), -1.0, 1.0));
        edge_m[static_cast<size_t>(i) * 6 + k] =
            static_cast<float>(std::max(1.0, ang * radius));
      }
      area_m2[i] = static_cast<float>(g.cell_area_sr(i) * radius * radius);
    }
  }
};

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

FilledResult fill_depressions(const CellGraph& graph, const std::vector<float>& z,
                              const std::vector<uint8_t>& outlet) {
  struct Node {
    float h;
    int idx;
  };
  auto cmp = [](const Node& a, const Node& b) { return a.h > b.h; };
  std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

  const int n = static_cast<int>(z.size());
  std::vector<float> filled = z;
  std::vector<int32_t> parent(n, -1);
  std::vector<uint8_t> visited(n, 0);
  for (int i = 0; i < n; ++i) {
    if (outlet[i]) {
      visited[i] = 1;
      pq.push({filled[i], i});
    }
  }
  while (!pq.empty()) {
    const Node cur = pq.top();
    pq.pop();
    const int deg = graph.degree[cur.idx];
    for (int k = 0; k < deg; ++k) {
      const int j = graph.nb[static_cast<size_t>(cur.idx) * 6 + k];
      if (visited[j]) {
        continue;
      }
      visited[j] = 1;
      parent[j] = cur.idx;
      filled[j] = std::max(filled[j], cur.h);
      pq.push({filled[j], j});
    }
  }
  return {std::move(filled), std::move(parent)};
}

// Receivers: random lower neighbor proportional to slope (Tzathas receiver
// randomization); flood parent as fallback for carved cells.
void build_receivers(const CellGraph& graph, const std::vector<float>& z,
                     const std::vector<int32_t>& flood_parent,
                     const std::vector<uint8_t>& outlet, const std::vector<float>& rnd,
                     std::vector<int32_t>& receiver, std::vector<float>& receiver_dist) {
  const int n = static_cast<int>(z.size());
  receiver.assign(n, 0);
  receiver_dist.assign(n, 1.0f);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    if (outlet[i]) {
      receiver[i] = i;
      continue;
    }
    const int deg = graph.degree[i];
    float wsum = 0.0f;
    float weights[6];
    int ids[6];
    float dists[6];
    int count = 0;
    for (int k = 0; k < deg; ++k) {
      const int j = graph.nb[static_cast<size_t>(i) * 6 + k];
      const float dz = z[i] - z[j];
      if (dz > 0.0f) {
        const float dist = graph.edge_m[static_cast<size_t>(i) * 6 + k];
        ids[count] = j;
        dists[count] = dist;
        weights[count] = dz / dist;
        wsum += weights[count];
        ++count;
      }
    }
    if (count > 0) {
      float u = rnd[i] * wsum;
      int pick = count - 1;
      for (int k = 0; k < count; ++k) {
        u -= weights[k];
        if (u <= 0.0f) {
          pick = k;
          break;
        }
      }
      receiver[i] = ids[pick];
      receiver_dist[i] = dists[pick];
    } else if (flood_parent[i] >= 0) {
      receiver[i] = flood_parent[i];
      const Vec3d& a = graph.grid->cell_center(i);
      const Vec3d& b = graph.grid->cell_center(flood_parent[i]);
      receiver_dist[i] = static_cast<float>(
          std::max(1.0, std::acos(std::clamp(dot(a, b), -1.0, 1.0)) * graph.radius_m));
    } else {
      receiver[i] = i;
    }
  }
}

void solve_level(const CellGraph& graph, const ErosionParams& eparams, int seed,
                 const std::vector<float>& z0, const std::vector<float>& uplift,
                 float sea_level_m, std::vector<float>& z_work, int iterations,
                 int level_id) {
  const int n = static_cast<int>(z_work.size());
  std::vector<float> rnd(n);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    rnd[i] = rand01(static_cast<uint64_t>(seed) * 1315423911ULL +
                    static_cast<uint64_t>(level_id) * 2654435761ULL +
                    static_cast<uint64_t>(i));
  }

  const std::vector<uint8_t> outlet = build_outlet_mask(z0, sea_level_m);
  std::vector<int32_t> receiver;
  std::vector<float> receiver_dist;
  std::vector<float> area(n, 0.0f);
  std::vector<float> z_new(n, 0.0f);
  std::vector<int> order(n);

  const float dt = static_cast<float>(eparams.time_years);
  const float ema = std::clamp(static_cast<float>(eparams.fixed_point_ema), 0.05f, 1.0f);

  for (int it = 0; it < iterations; ++it) {
    std::cout << "    [geodesic-erosion] level-id " << level_id << " iteration "
              << (it + 1) << "/" << iterations << "\n";
    const FilledResult filled = fill_depressions(graph, z_work, outlet);
    build_receivers(graph, filled.filled_z, filled.flood_parent, outlet, rnd, receiver,
                    receiver_dist);

    // Drainage accumulation in descending order of filled elevation.
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      return filled.filled_z[a] > filled.filled_z[b];
    });
    for (int i = 0; i < n; ++i) {
      area[i] = graph.area_m2[i];
    }
    for (int idx : order) {
      const int r = receiver[idx];
      if (r != idx) {
        area[r] += area[idx];
      }
    }

    // Analytical update root-to-leaf (ascending filled elevation).
    for (int oi = n - 1; oi >= 0; --oi) {
      const int idx = order[oi];
      if (outlet[idx]) {
        z_new[idx] = sea_level_m;
        continue;
      }
      const int r = receiver[idx];
      if (r == idx) {
        z_new[idx] = z_work[idx];
        continue;
      }
      const float dist = receiver_dist[idx];

      float a = static_cast<float>(
          eparams.k * std::pow(std::max(1.0f, area[idx]), eparams.m));
      // Single-receiver slope correction (Tzathas 4.3): steepest downhill
      // slope over all neighbors versus the receiver drop.
      {
        const float zc = filled.filled_z[idx];
        float steepest = 0.0f;
        const int deg = graph.degree[idx];
        for (int k = 0; k < deg; ++k) {
          const int j = graph.nb[static_cast<size_t>(idx) * 6 + k];
          const float s =
              (zc - filled.filled_z[j]) / graph.edge_m[static_cast<size_t>(idx) * 6 + k];
          steepest = std::max(steepest, s);
        }
        const float drop = zc - filled.filled_z[r];
        if (drop > 1e-3f && steepest > 0.0f) {
          a *= std::clamp(dist * steepest / drop, 0.5f, 2.5f);
        }
      }
      if (eparams.enable_hillslope) {
        a += static_cast<float>((eparams.hillslope_k / eparams.hack_c) *
                                std::pow(std::max(1.0f, area[idx]), -eparams.hack_h));
      }
      const float denom = std::max(1e-8f, a);
      const float lambda = std::exp(-denom * dt / dist);
      const float target = z_new[r] + uplift[idx] * dist / denom;
      // The analytical solution advects the INITIAL terrain z0 (Eq 8); the
      // fixed point iterates only through the drainage network.
      z_new[idx] = z0[idx] * lambda + target * (1.0f - lambda);
      if (z0[idx] > sea_level_m) {
        z_new[idx] = std::max(z_new[idx], sea_level_m);
      }
    }

    // Thermal critical-slope clamp along receiver edges.
    if (eparams.enable_thermal) {
      const float sc = static_cast<float>(eparams.thermal_critical_slope);
      for (int oi = n - 1; oi >= 0; --oi) {
        const int idx = order[oi];
        const int r = receiver[idx];
        if (r == idx) {
          continue;
        }
        const float max_drop = sc * receiver_dist[idx];
        if (z_new[idx] - z_new[r] > max_drop) {
          z_new[idx] = z_new[r] + max_drop;
        }
      }
    }

    // Fixed-point EMA damping (Tzathas 5.1).
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

std::vector<int> distribute_iterations(int total, int levels) {
  const int safe_levels = std::max(1, levels);
  int remaining = std::max(1, total);
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

void run_geodesic_physics_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& domain = dataset.domain();
  const auto& base = dataset.float_layer("elevation_base_m");
  const auto& uplift_raster = dataset.float_layer("uplift_rate_m_per_yr");
  const float sea = params.hydrology.sea_level_m;
  // Drowned-coast flooding (coastline fix 3): erode against lowstand
  // outlets, then flood back to sea level - valleys carved below today's sea
  // level become rias, fjords and shelf archipelagos.
  const float eff_sea = sea - params.hydrology.flood_lowstand_m;

  if (!dataset.has_layer("elevation_eroded_m")) {
    dataset.create_float_layer("elevation_eroded_m", "m",
                               "Analytical stream-power eroded elevation (geodesic)");
  }
  if (!dataset.has_layer("flow_accumulation_m2")) {
    dataset.create_float_layer("flow_accumulation_m2", "m2",
                               "Drainage accumulation (geodesic MFD)");
  }
  if (!dataset.has_layer("ocean_mask")) {
    dataset.create_u8_layer("ocean_mask", "bool", "Ocean connectivity mask");
  }
  if (!dataset.has_layer("lake_mask")) {
    dataset.create_u8_layer("lake_mask", "bool", "Inland lake mask");
  }
  if (!dataset.has_layer("river_mask")) {
    dataset.create_u8_layer("river_mask", "bool", "River mask");
  }
  if (!dataset.has_layer("temperature_c")) {
    dataset.create_float_layer("temperature_c", "C", "Mean annual temperature");
  }
  if (!dataset.has_layer("precipitation_mm_yr")) {
    dataset.create_float_layer("precipitation_mm_yr", "mm/yr", "Annual precipitation");
  }
  if (!dataset.has_layer("biome_id")) {
    dataset.create_u8_layer("biome_id", "id", "Biome classification");
  }
  if (!dataset.has_layer("vegetation")) {
    dataset.create_float_layer("vegetation", "", "Vegetation density (NPP proxy)");
  }

  // Frequency hierarchy: F = F0 * 2^(levels-1).
  const int levels = std::max(1, params.erosion.multigrid_levels);
  int f0 = std::max(8, params.physics_grid_frequency >> (levels - 1));
  const int f_top = f0 << (levels - 1);
  const std::vector<int> iter_budget =
      distribute_iterations(params.erosion.fixed_point_iterations, levels);

  std::vector<float> z_guess;      // on the previous (coarser) level
  std::unique_ptr<GeodesicGrid> prev_grid;

  std::unique_ptr<GeodesicGrid> grid;
  CellGraph graph;
  std::vector<float> z0;
  std::vector<float> uplift;
  std::vector<float> z_work;

  for (int level = 0; level < levels; ++level) {
    const int f = f0 << level;
    grid = std::make_unique<GeodesicGrid>(f);
    graph.build(*grid, domain.radius_m());
    const int n = grid->cell_count();

    z0.resize(n);
    uplift.resize(n);
    const auto paint_it = params.paint_layers.find("uplift_paint");
    const PaintLayer* uplift_paint =
        (paint_it != params.paint_layers.end()) ? &paint_it->second : nullptr;
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      const Vec3d& c = grid->cell_center(i);
      z0[i] = sample_raster(domain, base, c);
      uplift[i] = sample_raster(domain, uplift_raster, c);
      if (uplift_paint != nullptr) {
        uplift[i] += std::max(0.0f, sample_paint(*uplift_paint, c));
      }
    }

    // Initial guess: base elevation at the coarsest level, jittered
    // prolongation of the previous level's solution otherwise.
    z_work.resize(n);
    if (level == 0) {
      z_work = z0;
    } else {
      const double coarse_spacing =
          std::sqrt(4.0 * kPi / prev_grid->cell_count());  // radians
      #pragma omp parallel for schedule(static)
      for (int i = 0; i < n; ++i) {
        const Vec3d& c = grid->cell_center(i);
        const uint64_t key = static_cast<uint64_t>(params.seed) * 0x9E3779B1ULL +
                             static_cast<uint64_t>(level) * 0x85EBCA77ULL +
                             static_cast<uint64_t>(i);
        // Tangent jitter of +-0.25 coarse cells (Tzathas 5.2).
        const double a1 = (rand01(key) - 0.5) * 0.5 * coarse_spacing;
        const double a2 = (rand01(key + 0x2545F491ULL) - 0.5) * 0.5 * coarse_spacing;
        const Vec3d ref = (std::abs(c.z) < 0.9) ? Vec3d{0.0, 0.0, 1.0} : Vec3d{1.0, 0.0, 0.0};
        const Vec3d t1 = normalized(cross(c, ref));
        const Vec3d t2 = cross(c, t1);
        const Vec3d q = normalized({c.x + a1 * t1.x + a2 * t2.x,
                                    c.y + a1 * t1.y + a2 * t2.y,
                                    c.z + a1 * t1.z + a2 * t2.z});
        const auto lk = prev_grid->locate(q);
        float v = 0.0f;
        for (int k = 0; k < 3; ++k) {
          v += static_cast<float>(lk.weight[k]) * z_guess[lk.cell[k]];
        }
        z_work[i] = v;
      }
    }

    const int iters = iter_budget[levels - 1 - level];
    if (iters > 0) {
      std::cout << "  [geodesic-erosion] level " << (level + 1) << "/" << levels
                << " F=" << f << " cells=" << n << " iterations " << iters << "\n";
      solve_level(graph, params.erosion, params.seed, z0, uplift, eff_sea, z_work, iters,
                  level);
    }

    z_guess = z_work;
    prev_grid = std::move(grid);
  }

  const GeodesicGrid& top = *prev_grid;
  const int n = top.cell_count();
  std::vector<float>& z = z_guess;

  // Deep-ocean bathymetry is untouched by the solver; the lowstand band
  // [eff_sea, sea] keeps its carved valleys and floods below.
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    if (z0[i] <= eff_sea) {
      z[i] = z0[i];
    }
  }

  // ---- Climate (plan M9): temperature, zonal winds, moisture advection ----
  std::vector<float> temp_c(n);
  std::vector<float> precip_mm(n);
  std::vector<float> runoff_mm(n);
  std::vector<uint8_t> biome(n, 0);
  std::vector<float> veg(n, 0.0f);
  {
    FastNoiseLite band_noise(params.seed + 5501);
    band_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    band_noise.SetFrequency(1.3f);

    std::vector<float> lat_rad(n);
    std::vector<float> wind(static_cast<size_t>(n) * 3);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      const Vec3d& p = top.cell_center(i);
      const double lat = std::asin(std::clamp(p.y, -1.0, 1.0));
      lat_rad[i] = static_cast<float>(lat);
      // temperature: insolation + lapse rate + coherent noise
      const double t = -23.0 + 50.0 * std::pow(std::max(0.0, std::cos(lat)), 1.4) -
                       6.5 * std::max(0.0f, z[i]) / 1000.0 +
                       1.5 * band_noise.GetNoise(static_cast<float>(p.x * 3),
                                                 static_cast<float>(p.y * 3),
                                                 static_cast<float>(p.z * 3));
      temp_c[i] = static_cast<float>(t);
      // zonal wind bands with noise-jittered edges
      const double jitter = 6.0 * band_noise.GetNoise(static_cast<float>(p.x),
                                                      static_cast<float>(p.y),
                                                      static_cast<float>(p.z));
      const double abs_lat = std::abs(lat) * 180.0 / kPi + jitter;
      const double easterly = (abs_lat < 28.0 || abs_lat > 62.0) ? -1.0 : 1.0;
      // east tangent in the domain's y-up frame
      Vec3d east{p.z, 0.0, -p.x};
      const double en = std::sqrt(dot(east, east));
      east = (en > 1e-9) ? Vec3d{east.x / en, east.y / en, east.z / en}
                         : Vec3d{1.0, 0.0, 0.0};
      Vec3d north = cross(p, east);
      const double tilt = (abs_lat < 28.0) ? -0.4 * ((lat >= 0) ? 1.0 : -1.0)
                                           : 0.2 * ((lat >= 0) ? 1.0 : -1.0);
      Vec3d w{easterly * east.x + tilt * north.x, easterly * east.y + tilt * north.y,
              easterly * east.z + tilt * north.z};
      const double wn = std::sqrt(dot(w, w));
      wind[static_cast<size_t>(i) * 3 + 0] = static_cast<float>(w.x / wn);
      wind[static_cast<size_t>(i) * 3 + 1] = static_cast<float>(w.y / wn);
      wind[static_cast<size_t>(i) * 3 + 2] = static_cast<float>(w.z / wn);
    }

    // Upwind gather weights: flow from neighbor j into i where j's wind
    // points toward i.
    std::vector<float> upw(static_cast<size_t>(n) * 6, 0.0f);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      const Vec3d& pi = top.cell_center(i);
      float sum = 0.0f;
      const int deg = graph.degree[i];
      for (int k = 0; k < deg; ++k) {
        const int j = graph.nb[static_cast<size_t>(i) * 6 + k];
        const Vec3d& pj = top.cell_center(j);
        Vec3d d{pi.x - pj.x, pi.y - pj.y, pi.z - pj.z};
        d = {d.x - pj.x * dot(d, pj), d.y - pj.y * dot(d, pj), d.z - pj.z * dot(d, pj)};
        const double dn = std::sqrt(dot(d, d));
        if (dn < 1e-12) {
          continue;
        }
        const float a = static_cast<float>(
            (wind[static_cast<size_t>(j) * 3 + 0] * d.x +
             wind[static_cast<size_t>(j) * 3 + 1] * d.y +
             wind[static_cast<size_t>(j) * 3 + 2] * d.z) / dn);
        if (a > 0.0f) {
          upw[static_cast<size_t>(i) * 6 + k] = a;
          sum += a;
        }
      }
      if (sum > 1e-9f) {
        for (int k = 0; k < 6; ++k) {
          upw[static_cast<size_t>(i) * 6 + k] /= sum;
        }
      }
    }

    // Rain fraction: convective (ITCZ + frontal band) + orographic lift.
    std::vector<float> rainfrac(n);
    std::vector<float> evap(n);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      const double lat_deg = lat_rad[i] * 180.0 / kPi;
      double conv = 0.10 * std::exp(-std::pow(lat_deg / 12.0, 2)) +
                    0.05 * std::exp(-std::pow((std::abs(lat_deg) - 52.0) / 14.0, 2)) +
                    0.02;
      double orog = 0.0;
      const int deg = graph.degree[i];
      for (int k = 0; k < deg; ++k) {
        const float w = upw[static_cast<size_t>(i) * 6 + k];
        if (w <= 0.0f) {
          continue;
        }
        const int j = graph.nb[static_cast<size_t>(i) * 6 + k];
        const float dz = std::max(0.0f, std::max(z[i], sea) - std::max(z[j], sea));
        orog += w * dz / graph.edge_m[static_cast<size_t>(i) * 6 + k];
      }
      rainfrac[i] = static_cast<float>(std::clamp(conv + 15.0 * orog, 0.02, 0.9));
      const double warm = std::clamp((temp_c[i] + 5.0) / 30.0, 0.0, 1.0);
      evap[i] = static_cast<float>((z[i] <= sea) ? 0.6 + 0.4 * warm : 0.12 * warm);
    }

    // Fixed-point moisture advection.
    std::vector<float> hum(n, 0.0f);
    std::vector<float> hum_next(n, 0.0f);
    for (int it = 0; it < 30; ++it) {
      #pragma omp parallel for schedule(static)
      for (int i = 0; i < n; ++i) {
        float arrive = evap[i];
        const int deg = graph.degree[i];
        for (int k = 0; k < deg; ++k) {
          const float w = upw[static_cast<size_t>(i) * 6 + k];
          if (w > 0.0f) {
            arrive += w * hum[graph.nb[static_cast<size_t>(i) * 6 + k]];
          }
        }
        hum_next[i] = arrive * (1.0f - rainfrac[i]);
      }
      hum.swap(hum_next);
    }
    double rain_sum = 0.0;
    double area_sum = 0.0;
    #pragma omp parallel for schedule(static) reduction(+:rain_sum, area_sum)
    for (int i = 0; i < n; ++i) {
      float arrive = evap[i];
      const int deg = graph.degree[i];
      for (int k = 0; k < deg; ++k) {
        const float w = upw[static_cast<size_t>(i) * 6 + k];
        if (w > 0.0f) {
          arrive += w * hum[graph.nb[static_cast<size_t>(i) * 6 + k]];
        }
      }
      precip_mm[i] = rainfrac[i] * arrive;  // unnormalized
      rain_sum += static_cast<double>(precip_mm[i]) * graph.area_m2[i];
      area_sum += graph.area_m2[i];
    }
    const float scale = static_cast<float>(900.0 / std::max(1e-9, rain_sum / area_sum));
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
      precip_mm[i] = std::min(5000.0f, precip_mm[i] * scale);
      const float pet = std::clamp(45.0f * (temp_c[i] + 5.0f), 0.0f, 1800.0f);
      runoff_mm[i] = (z[i] > sea) ? std::max(0.0f, precip_mm[i] - 0.55f * pet) + 15.0f
                                  : 0.0f;
      // Biome classification (ids match the studio palette).
      const float t = temp_c[i];
      const float p = precip_mm[i];
      uint8_t b;
      if (z[i] <= sea) {
        b = 0;  // ocean
      } else if (t < -12.0f) {
        b = 1;  // ice
      } else if (t < -2.0f) {
        b = 2;  // tundra
      } else if (z[i] > 3300.0f) {
        b = 9;  // alpine
      } else if (p < 220.0f) {
        b = 8;  // desert
      } else if (t < 6.0f) {
        b = 3;  // boreal forest
      } else if (t > 21.0f && p > 1600.0f) {
        b = 6;  // tropical rainforest
      } else if (t > 19.0f && p < 1100.0f) {
        b = 7;  // savanna
      } else if (p < 550.0f) {
        b = 5;  // grassland
      } else {
        b = 4;  // temperate forest
      }
      biome[i] = b;
      veg[i] = (b == 0 || b == 1 || b == 8)
                   ? 0.0f
                   : std::clamp(std::min(p / 1800.0f, (t + 8.0f) / 32.0f), 0.0f, 1.0f);
    }
  }

  // ---- Hydrology: MFD accumulation weighted by runoff (discharge) ----
  const std::vector<uint8_t> outlet = build_outlet_mask(z0, eff_sea);
  const FilledResult filled = fill_depressions(graph, z, outlet);
  std::vector<float> accum(n);
  for (int i = 0; i < n; ++i) {
    // Area-equivalent at 500 mm/yr runoff keeps the river threshold param's
    // historical meaning while deserts lose rivers and wet belts gain them.
    accum[i] = graph.area_m2[i] * (runoff_mm[i] / 500.0f);
  }
  {
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      return filled.filled_z[a] > filled.filled_z[b];
    });
    for (int idx : order) {
      const int deg = graph.degree[idx];
      float wsum = 0.0f;
      float weights[6];
      int ids[6];
      int count = 0;
      for (int k = 0; k < deg; ++k) {
        const int j = graph.nb[static_cast<size_t>(idx) * 6 + k];
        const float dz = filled.filled_z[idx] - filled.filled_z[j];
        if (dz > 0.0f) {
          ids[count] = j;
          weights[count] = dz / graph.edge_m[static_cast<size_t>(idx) * 6 + k];
          wsum += weights[count];
          ++count;
        }
      }
      if (count > 0) {
        for (int k = 0; k < count; ++k) {
          accum[ids[k]] += accum[idx] * (weights[k] / wsum);
        }
      } else if (filled.flood_parent[idx] >= 0) {
        accum[filled.flood_parent[idx]] += accum[idx];
      }
    }
  }

  // ---- Masks: largest below-sea component = ocean, others = lakes ----
  std::vector<uint8_t> cell_ocean(n, 0);
  std::vector<uint8_t> cell_lake(n, 0);
  {
    std::vector<int32_t> comp(n, -1);
    std::vector<int64_t> comp_area;
    std::deque<int> queue;
    int comp_count = 0;
    for (int i = 0; i < n; ++i) {
      if (z[i] > sea || comp[i] >= 0) {
        continue;
      }
      comp_area.push_back(0);
      comp[i] = comp_count;
      queue.push_back(i);
      while (!queue.empty()) {
        const int idx = queue.front();
        queue.pop_front();
        comp_area[comp_count] += static_cast<int64_t>(graph.area_m2[idx]);
        const int deg = graph.degree[idx];
        for (int k = 0; k < deg; ++k) {
          const int j = graph.nb[static_cast<size_t>(idx) * 6 + k];
          if (z[j] <= sea && comp[j] < 0) {
            comp[j] = comp_count;
            queue.push_back(j);
          }
        }
      }
      ++comp_count;
    }
    // Any below-sea component covering >= 2% of the sphere is an ocean
    // (multi-basin worlds are normal); smaller ones are lakes.
    double total_area = 0.0;
    for (int i = 0; i < n; ++i) {
      total_area += graph.area_m2[i];
    }
    std::vector<uint8_t> comp_is_ocean(comp_count, 0);
    for (int cid = 0; cid < comp_count; ++cid) {
      comp_is_ocean[cid] =
          (static_cast<double>(comp_area[cid]) >= 0.02 * total_area) ? 1 : 0;
    }
    for (int i = 0; i < n; ++i) {
      if (comp[i] >= 0) {
        cell_ocean[i] = comp_is_ocean[comp[i]];
        cell_lake[i] = cell_ocean[i] ? 0 : 1;
      }
    }
  }
  std::vector<uint8_t> cell_river(n, 0);
  for (int i = 0; i < n; ++i) {
    cell_river[i] = (!cell_ocean[i] && !cell_lake[i] &&
                     accum[i] > params.hydrology.river_area_threshold_m2)
                        ? 1
                        : 0;
  }

  // ---- Rasterize to the lat-lon dataset ----
  auto& eroded = dataset.float_layer("elevation_eroded_m");
  auto& accum_layer = dataset.float_layer("flow_accumulation_m2");
  auto& ocean_layer = dataset.u8_layer("ocean_mask");
  auto& lake_layer = dataset.u8_layer("lake_mask");
  auto& river_layer = dataset.u8_layer("river_mask");
  auto& temp_layer = dataset.float_layer("temperature_c");
  auto& precip_layer = dataset.float_layer("precipitation_mm_yr");
  auto& biome_layer = dataset.u8_layer("biome_id");
  auto& veg_layer = dataset.float_layer("vegetation");

  const int w = domain.width();
  const int h = domain.height();
  #pragma omp parallel for schedule(dynamic, 8)
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int idx = domain.index(x, y);
      const Vec3d p = domain.lat_lon_to_xyz(domain.center_lat_lon_deg(x, y));
      const auto lk = top.locate(p);
      double ez = 0.0;
      double ac = 0.0;
      double tc = 0.0;
      double pr = 0.0;
      double vg = 0.0;
      int best = 0;
      for (int k = 0; k < 3; ++k) {
        ez += lk.weight[k] * z[lk.cell[k]];
        ac += lk.weight[k] * accum[lk.cell[k]];
        tc += lk.weight[k] * temp_c[lk.cell[k]];
        pr += lk.weight[k] * precip_mm[lk.cell[k]];
        vg += lk.weight[k] * veg[lk.cell[k]];
        if (lk.weight[k] > lk.weight[best]) {
          best = k;
        }
      }
      const int bc = lk.cell[best];
      eroded[idx] = static_cast<float>(ez);
      accum_layer[idx] = static_cast<float>(ac);
      ocean_layer[idx] = cell_ocean[bc];
      lake_layer[idx] = cell_lake[bc];
      temp_layer[idx] = static_cast<float>(tc);
      precip_layer[idx] = static_cast<float>(pr);
      veg_layer[idx] = static_cast<float>(vg);
      biome_layer[idx] = (ez <= sea && cell_ocean[bc]) ? 0 : biome[bc];
      // Pixel-level threshold on the interpolated accumulation keeps rivers
      // thin instead of painting whole (coarser) geodesic cells.
      river_layer[idx] = (!ocean_layer[idx] && !lake_layer[idx] && cell_river[bc] &&
                          ac > params.hydrology.river_area_threshold_m2)
                             ? 1
                             : 0;
    }
  }
  // Retain the final cell buffers (rhombus-atlas persistence, plan
  // addendum b): consumers fetch them via the bindings and pack rhombus
  // atlases with GeodesicGrid::cell_to_rhombus.
  {
    std::vector<float> ocean_f(n);
    for (int i = 0; i < n; ++i) {
      ocean_f[i] = static_cast<float>(cell_ocean[i]);
    }
    dataset.set_cell_layer("cell_elevation_m", f_top, z);
    dataset.set_cell_layer("cell_flow_accum_m2", f_top, accum);
    dataset.set_cell_layer("cell_ocean", f_top, std::move(ocean_f));
    dataset.set_cell_layer("cell_temperature_c", f_top, temp_c);
    dataset.set_cell_layer("cell_precip_mm", f_top, precip_mm);
    dataset.set_cell_layer("cell_runoff_mm", f_top, runoff_mm);
    dataset.set_cell_layer("cell_vegetation", f_top, veg);
    std::vector<float> biome_f(n);
    for (int i = 0; i < n; ++i) {
      biome_f[i] = static_cast<float>(biome[i]);
    }
    dataset.set_cell_layer("cell_biome", f_top, std::move(biome_f));
  }
  std::cout << "  [geodesic-physics] F=" << f_top << " cells=" << n << " done\n";
}

}  // namespace world_engine::terrain::procedural::stages
