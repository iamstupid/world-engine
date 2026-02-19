#include "world_engine/terrain/procedural/procedural_generator.h"

#include <cmath>
#include <stdexcept>

namespace world_engine::terrain::procedural {

void ProceduralGenerator::generate(const PipelineParams& params) {
  params_ = params;
  dataset_ = pipeline_.run(params_);
}

float ProceduralGenerator::sample_layer(const std::string& layer_name, double lat_deg,
                                        double lon_deg) const {
  const auto& domain = dataset_.domain();
  const double u = (lon_deg + 180.0) / 360.0;
  const double v = (90.0 - lat_deg) / 180.0;
  const int x = static_cast<int>(std::floor(u * static_cast<double>(domain.width())));
  const int y = static_cast<int>(std::floor(v * static_cast<double>(domain.height())));
  const int idx = domain.index(x, y);

  const auto type = dataset_.layer_type(layer_name);
  if (type == LayerDataType::kFloat32) {
    return dataset_.float_layer(layer_name)[idx];
  }
  if (type == LayerDataType::kUInt8) {
    return static_cast<float>(dataset_.u8_layer(layer_name)[idx]);
  }
  return static_cast<float>(dataset_.i32_layer(layer_name)[idx]);
}

float ProceduralGenerator::sample_elevation(double lat_deg, double lon_deg) const {
  if (dataset_.has_layer("elevation_eroded_m")) {
    return sample_layer("elevation_eroded_m", lat_deg, lon_deg);
  }
  if (dataset_.has_layer("elevation_base_m")) {
    return sample_layer("elevation_base_m", lat_deg, lon_deg);
  }
  throw std::runtime_error("No elevation layer available");
}

std::vector<std::string> ProceduralGenerator::list_layers() const {
  return dataset_.list_layers();
}

}  // namespace world_engine::terrain::procedural
