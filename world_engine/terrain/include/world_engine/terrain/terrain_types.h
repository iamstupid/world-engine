#pragma once

#include <cstdint>
#include <string>

namespace world_engine::terrain {

struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct LatLon {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
};

enum class LayerDataType : uint8_t {
  kFloat32 = 0,
  kUInt8 = 1,
  kInt32 = 2,
};

struct LayerMetadata {
  std::string name;
  LayerDataType type = LayerDataType::kFloat32;
  std::string units;
  std::string description;
  int width = 0;
  int height = 0;
};

}  // namespace world_engine::terrain
