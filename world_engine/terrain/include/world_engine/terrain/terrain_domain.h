#pragma once

#include <cstdint>
#include <utility>

#include "world_engine/terrain/terrain_types.h"

namespace world_engine::terrain {

class TerrainDomain {
 public:
  TerrainDomain() = default;
  TerrainDomain(int width, int height, double radius_m);

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] double radius_m() const { return radius_m_; }

  [[nodiscard]] int wrap_x(int x) const;
  [[nodiscard]] int clamp_y(int y) const;
  [[nodiscard]] int index(int x, int y) const;
  [[nodiscard]] std::pair<int, int> unindex(int idx) const;

  [[nodiscard]] LatLon center_lat_lon_deg(int x, int y) const;
  [[nodiscard]] Vec3d lat_lon_to_xyz(const LatLon& ll) const;
  [[nodiscard]] LatLon xyz_to_lat_lon(const Vec3d& p) const;

  [[nodiscard]] double cell_area_m2(int y) const;
  [[nodiscard]] double east_west_spacing_m(int y) const;
  [[nodiscard]] double north_south_spacing_m() const;

 private:
  int width_ = 0;
  int height_ = 0;
  double radius_m_ = 6'371'000.0;
};

}  // namespace world_engine::terrain
