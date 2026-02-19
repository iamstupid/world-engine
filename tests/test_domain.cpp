#include <cmath>
#include <iostream>

#include "world_engine/terrain/terrain_domain.h"

int main() {
  world_engine::terrain::TerrainDomain d(512, 256, 6'371'000.0);

  const world_engine::terrain::LatLon ll{37.2, -122.1};
  const auto p = d.lat_lon_to_xyz(ll);
  const auto ll2 = d.xyz_to_lat_lon(p);
  if (std::abs(ll.lat_deg - ll2.lat_deg) > 1e-6 || std::abs(ll.lon_deg - ll2.lon_deg) > 1e-6) {
    std::cerr << "lat/lon conversion mismatch\n";
    return 1;
  }

  if (d.index(-1, 0) != d.index(511, 0)) {
    std::cerr << "wrap mismatch\n";
    return 1;
  }

  if (!(d.cell_area_m2(0) > 0.0) || !(d.cell_area_m2(128) > 0.0)) {
    std::cerr << "invalid cell area\n";
    return 1;
  }

  return 0;
}
