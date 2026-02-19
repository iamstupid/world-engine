#include "world_engine/terrain/procedural/stages/masks_stage.h"

#include <deque>
#include <limits>
#include <vector>

namespace world_engine::terrain::procedural::stages {

namespace {
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
}  // namespace

void run_masks_stage(const PipelineParams& params, TerrainDataset& dataset) {
  const auto& elev = dataset.float_layer("elevation_eroded_m");
  const auto& domain = dataset.domain();
  const int n = dataset.size();

  if (!dataset.has_layer("ocean_mask")) {
    dataset.create_u8_layer("ocean_mask", "bool", "Ocean connectivity mask");
  }
  if (!dataset.has_layer("lake_mask")) {
    dataset.create_u8_layer("lake_mask", "bool", "Inland lake mask");
  }
  if (!dataset.has_layer("river_mask")) {
    dataset.create_u8_layer("river_mask", "bool", "River mask");
  }

  auto& ocean = dataset.u8_layer("ocean_mask");
  auto& lake = dataset.u8_layer("lake_mask");
  auto& river = dataset.u8_layer("river_mask");
  ocean.assign(n, 0);
  lake.assign(n, 0);

  std::vector<uint8_t> below(n, 0);
  for (int i = 0; i < n; ++i) {
    below[i] = (elev[i] <= params.hydrology.sea_level_m) ? 1 : 0;
  }

  std::vector<int32_t> comp(n, -1);
  std::vector<int32_t> comp_size;
  std::vector<float> comp_min_elev;
  std::deque<int> q;
  int comp_count = 0;

  for (int i = 0; i < n; ++i) {
    if (!below[i] || comp[i] >= 0) {
      continue;
    }
    comp_size.push_back(0);
    comp_min_elev.push_back(std::numeric_limits<float>::max());
    comp[i] = comp_count;
    q.push_back(i);

    while (!q.empty()) {
      const int idx = q.front();
      q.pop_front();
      const int cid = comp[idx];
      ++comp_size[cid];
      if (elev[idx] < comp_min_elev[cid]) {
        comp_min_elev[cid] = elev[idx];
      }

      const auto [x, y] = domain.unindex(idx);
      const int nb[4] = {
          domain.index(x - 1, y), domain.index(x + 1, y), north_neighbor(domain, x, y),
          south_neighbor(domain, x, y)};

      for (int nidx : nb) {
        if (!below[nidx] || comp[nidx] >= 0) {
          continue;
        }
        comp[nidx] = cid;
        q.push_back(nidx);
      }
    }
    ++comp_count;
  }

  int ocean_comp = -1;
  for (int cid = 0; cid < comp_count; ++cid) {
    if (ocean_comp < 0) {
      ocean_comp = cid;
      continue;
    }
    if (comp_size[cid] > comp_size[ocean_comp] ||
        (comp_size[cid] == comp_size[ocean_comp] && comp_min_elev[cid] < comp_min_elev[ocean_comp])) {
      ocean_comp = cid;
    }
  }

  for (int i = 0; i < n; ++i) {
    if (comp[i] >= 0) {
      ocean[i] = (comp[i] == ocean_comp) ? 1 : 0;
    } else {
      ocean[i] = 0;
    }
    lake[i] = (below[i] && !ocean[i]) ? 1 : 0;
    if (ocean[i] || lake[i]) {
      river[i] = 0;
    }
  }
}

}  // namespace world_engine::terrain::procedural::stages
