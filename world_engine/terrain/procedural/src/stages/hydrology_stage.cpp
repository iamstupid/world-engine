#include "world_engine/terrain/procedural/stages/hydrology_stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <queue>
#include <vector>

namespace world_engine::terrain::procedural::stages {
namespace {
// Source: Water Resources Research - 1997 - Tarboton - A new method for the
// determination of flow directions and upslope areas in grid digital elevation models.
// D-Infinity is implemented as:
// - direction angle on triangular facets around each cell
// - split flow partition to the two adjacent neighbors that bound the angle.
//
// Source: Analytical_Terrains_EG.pdf, Section 4.3 / Section 7.
// Analytical erosion stage still uses a single-receiver routing for its solver.

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kPiOver4 = kPi * 0.25f;

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
  const float th = tmp[min_outlets - 1];
  for (int i = 0; i < n; ++i) {
    if (z[i] <= th) {
      outlet[i] = 1;
    }
  }
  return outlet;
}

struct FilledResult {
  std::vector<float> filled_z;
  std::vector<int32_t> flood_parent;
};

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
    const int nb[4] = {
        domain.index(x - 1, y), domain.index(x + 1, y), north_neighbor(domain, x, y),
        south_neighbor(domain, x, y)};

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

struct FacetResult {
  float slope = -1.0f;
  float angle = 0.0f;
};

FacetResult evaluate_facet(float zc, float z1, float z2, float d1, float d2, float theta1, float sign) {
  const float s1 = (zc - z1) / std::max(1e-6f, d1);
  const float s2 = (z1 - z2) / std::max(1e-6f, d2);
  const float rmax = std::atan2(d2, d1);
  float r = std::atan2(s2, s1);

  float s = 0.0f;
  if (r < 0.0f) {
    r = 0.0f;
    s = s1;
  } else if (r > rmax) {
    r = rmax;
    s = (zc - z2) / std::sqrt(d1 * d1 + d2 * d2);
  } else {
    s = std::sqrt(std::max(0.0f, s1 * s1 + s2 * s2));
  }

  float a = theta1 + sign * r;
  while (a < 0.0f) {
    a += kTwoPi;
  }
  while (a >= kTwoPi) {
    a -= kTwoPi;
  }
  return {s, a};
}

float angle_to_neighbor(const TerrainDomain& domain, int from_idx, int to_idx) {
  const auto [x0, y0] = domain.unindex(from_idx);
  const auto [x1, y1] = domain.unindex(to_idx);
  int dx = x1 - x0;
  if (dx > domain.width() / 2) {
    dx -= domain.width();
  } else if (dx < -domain.width() / 2) {
    dx += domain.width();
  }
  const int dy = y0 - y1;  // screen y-down => north is positive dy.
  float a = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
  if (a < 0.0f) {
    a += kTwoPi;
  }
  return a;
}

struct DInfFlow {
  int32_t r0 = -1;
  int32_t r1 = -1;
  float w0 = 1.0f;
  float w1 = 0.0f;
  float angle = 0.0f;
};

DInfFlow compute_dinf_flow(const TerrainDomain& domain, const std::vector<float>& filled_z,
                           const std::vector<int32_t>& flood_parent, const std::vector<uint8_t>& outlet, int idx) {
  const auto [x, y] = domain.unindex(idx);
  const int e = domain.index(x + 1, y);
  const int ne = north_neighbor(domain, x + 1, y);
  const int n = north_neighbor(domain, x, y);
  const int nw = north_neighbor(domain, x - 1, y);
  const int w = domain.index(x - 1, y);
  const int sw = south_neighbor(domain, x - 1, y);
  const int s = south_neighbor(domain, x, y);
  const int se = south_neighbor(domain, x + 1, y);
  const std::array<int, 8> dirs = {e, ne, n, nw, w, sw, s, se};

  if (outlet[idx]) {
    return {idx, idx, 1.0f, 0.0f, 0.0f};
  }

  const float zc = filled_z[idx];
  const float de = static_cast<float>(domain.east_west_spacing_m(y));
  const float dn = static_cast<float>(domain.north_south_spacing_m());

  std::array<FacetResult, 8> facets = {
      evaluate_facet(zc, filled_z[e], filled_z[ne], de, dn, 0.0f, +1.0f),
      evaluate_facet(zc, filled_z[n], filled_z[ne], dn, de, 0.5f * kPi, -1.0f),
      evaluate_facet(zc, filled_z[n], filled_z[nw], dn, de, 0.5f * kPi, +1.0f),
      evaluate_facet(zc, filled_z[w], filled_z[nw], de, dn, kPi, -1.0f),
      evaluate_facet(zc, filled_z[w], filled_z[sw], de, dn, kPi, +1.0f),
      evaluate_facet(zc, filled_z[s], filled_z[sw], dn, de, 1.5f * kPi, -1.0f),
      evaluate_facet(zc, filled_z[s], filled_z[se], dn, de, 1.5f * kPi, +1.0f),
      evaluate_facet(zc, filled_z[e], filled_z[se], de, dn, 0.0f, -1.0f),
  };

  FacetResult best{};
  for (const auto& f : facets) {
    if (f.slope > best.slope) {
      best = f;
    }
  }

  if (best.slope <= 0.0f) {
    const int parent = flood_parent[idx];
    if (parent >= 0 && parent != idx) {
      const float a = angle_to_neighbor(domain, idx, parent);
      return {parent, parent, 1.0f, 0.0f, a};
    }
    return {idx, idx, 1.0f, 0.0f, 0.0f};
  }

  const float a = best.angle;
  int sector = static_cast<int>(std::floor(a / kPiOver4));
  sector = std::clamp(sector, 0, 7);
  float local = (a - static_cast<float>(sector) * kPiOver4) / kPiOver4;
  local = std::clamp(local, 0.0f, 1.0f);

  int r0 = dirs[sector];
  int r1 = dirs[(sector + 1) & 7];
  float w1 = local;
  float w0 = 1.0f - local;

  if (r0 == r1 || w0 < 1e-4f || w1 < 1e-4f) {
    if (w1 > w0) {
      r0 = r1;
    }
    return {r0, r0, 1.0f, 0.0f, a};
  }
  return {r0, r1, w0, w1, a};
}
}  // namespace

void run_hydrology_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& elev = dataset.float_layer("elevation_eroded_m");
  const auto& domain = dataset.domain();
  const int n = dataset.size();

  if (!dataset.has_layer("receiver_index")) {
    dataset.create_i32_layer("receiver_index", "index", "Primary D-Infinity receiver index");
  }
  if (!dataset.has_layer("receiver_secondary_index")) {
    dataset.create_i32_layer("receiver_secondary_index", "index", "Secondary D-Infinity receiver index");
  }
  if (!dataset.has_layer("receiver_primary_weight")) {
    dataset.create_float_layer("receiver_primary_weight", "ratio", "Primary D-Infinity flow fraction");
  }
  if (!dataset.has_layer("receiver_secondary_weight")) {
    dataset.create_float_layer("receiver_secondary_weight", "ratio", "Secondary D-Infinity flow fraction");
  }
  if (!dataset.has_layer("flow_dir_angle_rad")) {
    dataset.create_float_layer("flow_dir_angle_rad", "rad", "D-Infinity flow direction angle");
  }
  if (!dataset.has_layer("flow_accumulation_m2")) {
    dataset.create_float_layer("flow_accumulation_m2", "m2", "Upslope contributing area");
  }
  if (!dataset.has_layer("river_mask")) {
    dataset.create_u8_layer("river_mask", "bool", "River network mask");
  }

  auto& receiver = dataset.i32_layer("receiver_index");
  auto& receiver2 = dataset.i32_layer("receiver_secondary_index");
  auto& w0 = dataset.float_layer("receiver_primary_weight");
  auto& w1 = dataset.float_layer("receiver_secondary_weight");
  auto& flow_angle = dataset.float_layer("flow_dir_angle_rad");
  auto& area = dataset.float_layer("flow_accumulation_m2");
  auto& river = dataset.u8_layer("river_mask");

  receiver.assign(n, 0);
  receiver2.assign(n, 0);
  w0.assign(n, 1.0f);
  w1.assign(n, 0.0f);
  flow_angle.assign(n, 0.0f);
  area.assign(n, 0.0f);
  river.assign(n, 0);

  const std::vector<uint8_t> outlet = build_outlet_mask(elev, params.hydrology.sea_level_m);
  const auto filled = fill_depressions_with_outlets(domain, elev, outlet);

  for (int i = 0; i < n; ++i) {
    const DInfFlow f = compute_dinf_flow(domain, filled.filled_z, filled.flood_parent, outlet, i);
    receiver[i] = f.r0;
    receiver2[i] = f.r1;
    w0[i] = f.w0;
    w1[i] = f.w1;
    flow_angle[i] = f.angle;
  }

  for (int i = 0; i < n; ++i) {
    const auto [x, y] = domain.unindex(i);
    (void)x;
    area[i] = static_cast<float>(domain.cell_area_m2(y));
  }

  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&](int a, int b) { return filled.filled_z[a] > filled.filled_z[b]; });
  for (int idx : order) {
    const int r0 = receiver[idx];
    const int r1 = receiver2[idx];
    if (r0 >= 0 && r0 < n && r0 != idx && w0[idx] > 0.0f) {
      area[r0] += area[idx] * w0[idx];
    }
    if (r1 >= 0 && r1 < n && r1 != idx && r1 != r0 && w1[idx] > 0.0f) {
      area[r1] += area[idx] * w1[idx];
    }
  }

  const float river_threshold = std::max(1.0f, params.hydrology.river_area_threshold_m2);
  for (int i = 0; i < n; ++i) {
    river[i] = (!outlet[i] && area[i] >= river_threshold) ? 1 : 0;
  }
}

}  // namespace world_engine::terrain::procedural::stages
