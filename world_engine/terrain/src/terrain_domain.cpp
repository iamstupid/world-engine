#include "world_engine/terrain/terrain_domain.h"

#include <algorithm>
#include <cmath>

namespace world_engine::terrain {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
}  // namespace

TerrainDomain::TerrainDomain(int width, int height, double radius_m)
    : width_(width), height_(height), radius_m_(radius_m) {}

int TerrainDomain::wrap_x(int x) const {
  if (width_ <= 0) {
    return 0;
  }
  int out = x % width_;
  if (out < 0) {
    out += width_;
  }
  return out;
}

int TerrainDomain::clamp_y(int y) const {
  if (height_ <= 0) {
    return 0;
  }
  return std::clamp(y, 0, height_ - 1);
}

int TerrainDomain::index(int x, int y) const {
  return clamp_y(y) * width_ + wrap_x(x);
}

std::pair<int, int> TerrainDomain::unindex(int idx) const {
  if (width_ <= 0) {
    return {0, 0};
  }
  const int y = std::clamp(idx / width_, 0, std::max(0, height_ - 1));
  const int x = std::clamp(idx - y * width_, 0, std::max(0, width_ - 1));
  return {x, y};
}

LatLon TerrainDomain::center_lat_lon_deg(int x, int y) const {
  const double u = (static_cast<double>(wrap_x(x)) + 0.5) / static_cast<double>(width_);
  const double v = (static_cast<double>(clamp_y(y)) + 0.5) / static_cast<double>(height_);

  const double lon_deg = u * 360.0 - 180.0;
  const double lat_deg = 90.0 - v * 180.0;
  return {lat_deg, lon_deg};
}

Vec3d TerrainDomain::lat_lon_to_xyz(const LatLon& ll) const {
  const double lat = ll.lat_deg * kDegToRad;
  const double lon = ll.lon_deg * kDegToRad;
  const double c = std::cos(lat);
  return {c * std::cos(lon), std::sin(lat), c * std::sin(lon)};
}

LatLon TerrainDomain::xyz_to_lat_lon(const Vec3d& p) const {
  const double n = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
  if (n == 0.0) {
    return {0.0, 0.0};
  }
  const double x = p.x / n;
  const double y = p.y / n;
  const double z = p.z / n;
  const double lat = std::asin(std::clamp(y, -1.0, 1.0));
  const double lon = std::atan2(z, x);
  return {lat * kRadToDeg, lon * kRadToDeg};
}

double TerrainDomain::cell_area_m2(int y) const {
  if (width_ <= 0 || height_ <= 0) {
    return 0.0;
  }
  const int cy = clamp_y(y);
  const double lat0 = (90.0 - (static_cast<double>(cy) * 180.0 / static_cast<double>(height_))) * kDegToRad;
  const double lat1 =
      (90.0 - (static_cast<double>(cy + 1) * 180.0 / static_cast<double>(height_))) * kDegToRad;
  const double dlon = 2.0 * kPi / static_cast<double>(width_);
  return radius_m_ * radius_m_ * dlon * std::abs(std::sin(lat0) - std::sin(lat1));
}

double TerrainDomain::east_west_spacing_m(int y) const {
  if (width_ <= 0) {
    return 0.0;
  }
  const auto ll = center_lat_lon_deg(0, y);
  const double lat = ll.lat_deg * kDegToRad;
  return (2.0 * kPi * radius_m_ * std::max(0.000001, std::cos(lat))) / static_cast<double>(width_);
}

double TerrainDomain::north_south_spacing_m() const {
  if (height_ <= 0) {
    return 0.0;
  }
  return kPi * radius_m_ / static_cast<double>(height_);
}

}  // namespace world_engine::terrain
