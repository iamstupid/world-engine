#include "world_engine/terrain/terrain_dataset.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace world_engine::terrain {
namespace {
uint64_t fnv1a64_update(uint64_t h, const void* data, size_t size) {
  constexpr uint64_t kPrime = 1099511628211ULL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= bytes[i];
    h *= kPrime;
  }
  return h;
}

uint64_t fnv1a64_update_string(uint64_t h, const std::string& s) {
  return fnv1a64_update(h, s.data(), s.size());
}
}  // namespace

TerrainDataset::TerrainDataset(TerrainDomain domain) : domain_(std::move(domain)) {}

void TerrainDataset::create_float_layer(const std::string& name, const std::string& units,
                                        const std::string& description) {
  LayerData layer;
  layer.metadata = {name, LayerDataType::kFloat32, units, description, domain_.width(), domain_.height()};
  layer.f32.assign(size(), 0.0f);
  layers_[name] = std::move(layer);
}

void TerrainDataset::create_u8_layer(const std::string& name, const std::string& units,
                                     const std::string& description) {
  LayerData layer;
  layer.metadata = {name, LayerDataType::kUInt8, units, description, domain_.width(), domain_.height()};
  layer.u8.assign(size(), 0);
  layers_[name] = std::move(layer);
}

void TerrainDataset::create_i32_layer(const std::string& name, const std::string& units,
                                      const std::string& description) {
  LayerData layer;
  layer.metadata = {name, LayerDataType::kInt32, units, description, domain_.width(), domain_.height()};
  layer.i32.assign(size(), 0);
  layers_[name] = std::move(layer);
}

bool TerrainDataset::has_layer(const std::string& name) const { return layers_.contains(name); }

std::vector<std::string> TerrainDataset::list_layers() const {
  std::vector<std::string> names;
  names.reserve(layers_.size());
  for (const auto& [name, _] : layers_) {
    (void)_;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

const LayerMetadata& TerrainDataset::layer_metadata(const std::string& name) const {
  return layer_or_throw(name).metadata;
}

LayerDataType TerrainDataset::layer_type(const std::string& name) const {
  return layer_or_throw(name).metadata.type;
}

std::vector<float>& TerrainDataset::float_layer(const std::string& name) {
  auto& layer = mutable_layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kFloat32) {
    throw std::runtime_error("Layer type mismatch for float layer: " + name);
  }
  return layer.f32;
}

std::vector<uint8_t>& TerrainDataset::u8_layer(const std::string& name) {
  auto& layer = mutable_layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kUInt8) {
    throw std::runtime_error("Layer type mismatch for u8 layer: " + name);
  }
  return layer.u8;
}

std::vector<int32_t>& TerrainDataset::i32_layer(const std::string& name) {
  auto& layer = mutable_layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kInt32) {
    throw std::runtime_error("Layer type mismatch for i32 layer: " + name);
  }
  return layer.i32;
}

const std::vector<float>& TerrainDataset::float_layer(const std::string& name) const {
  const auto& layer = layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kFloat32) {
    throw std::runtime_error("Layer type mismatch for float layer: " + name);
  }
  return layer.f32;
}

const std::vector<uint8_t>& TerrainDataset::u8_layer(const std::string& name) const {
  const auto& layer = layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kUInt8) {
    throw std::runtime_error("Layer type mismatch for u8 layer: " + name);
  }
  return layer.u8;
}

const std::vector<int32_t>& TerrainDataset::i32_layer(const std::string& name) const {
  const auto& layer = layer_or_throw(name);
  if (layer.metadata.type != LayerDataType::kInt32) {
    throw std::runtime_error("Layer type mismatch for i32 layer: " + name);
  }
  return layer.i32;
}

std::string TerrainDataset::deterministic_hash() const {
  uint64_t h = 1469598103934665603ULL;
  h = fnv1a64_update(h, &seed_, sizeof(seed_));

  const int w = domain_.width();
  const int hh = domain_.height();
  h = fnv1a64_update(h, &w, sizeof(w));
  h = fnv1a64_update(h, &hh, sizeof(hh));

  auto names = list_layers();
  for (const auto& name : names) {
    const auto& layer = layer_or_throw(name);
    h = fnv1a64_update_string(h, name);
    h = fnv1a64_update(h, &layer.metadata.type, sizeof(layer.metadata.type));
    if (layer.metadata.type == LayerDataType::kFloat32) {
      if (!layer.f32.empty()) {
        h = fnv1a64_update(h, layer.f32.data(), layer.f32.size() * sizeof(float));
      }
    } else if (layer.metadata.type == LayerDataType::kUInt8) {
      if (!layer.u8.empty()) {
        h = fnv1a64_update(h, layer.u8.data(), layer.u8.size() * sizeof(uint8_t));
      }
    } else {
      if (!layer.i32.empty()) {
        h = fnv1a64_update(h, layer.i32.data(), layer.i32.size() * sizeof(int32_t));
      }
    }
  }

  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << h;
  return oss.str();
}

LayerData& TerrainDataset::mutable_layer_or_throw(const std::string& name) {
  auto it = layers_.find(name);
  if (it == layers_.end()) {
    throw std::runtime_error("Missing layer: " + name);
  }
  return it->second;
}

const LayerData& TerrainDataset::layer_or_throw(const std::string& name) const {
  auto it = layers_.find(name);
  if (it == layers_.end()) {
    throw std::runtime_error("Missing layer: " + name);
  }
  return it->second;
}

}  // namespace world_engine::terrain
