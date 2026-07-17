#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "world_engine/terrain/terrain_domain.h"
#include "world_engine/terrain/terrain_types.h"

namespace world_engine::terrain {

struct LayerData {
  LayerMetadata metadata;
  std::vector<float> f32;
  std::vector<uint8_t> u8;
  std::vector<int32_t> i32;
};

class TerrainDataset {
 public:
  explicit TerrainDataset(TerrainDomain domain = TerrainDomain{});

  [[nodiscard]] const TerrainDomain& domain() const { return domain_; }
  [[nodiscard]] int size() const { return domain_.width() * domain_.height(); }

  void set_seed(int seed) { seed_ = seed; }
  [[nodiscard]] int seed() const { return seed_; }

  void create_float_layer(const std::string& name, const std::string& units,
                          const std::string& description);
  void create_u8_layer(const std::string& name, const std::string& units,
                       const std::string& description);
  void create_i32_layer(const std::string& name, const std::string& units,
                        const std::string& description);

  [[nodiscard]] bool has_layer(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> list_layers() const;
  [[nodiscard]] const LayerMetadata& layer_metadata(const std::string& name) const;
  [[nodiscard]] LayerDataType layer_type(const std::string& name) const;

  std::vector<float>& float_layer(const std::string& name);
  std::vector<uint8_t>& u8_layer(const std::string& name);
  std::vector<int32_t>& i32_layer(const std::string& name);

  [[nodiscard]] const std::vector<float>& float_layer(const std::string& name) const;
  [[nodiscard]] const std::vector<uint8_t>& u8_layer(const std::string& name) const;
  [[nodiscard]] const std::vector<int32_t>& i32_layer(const std::string& name) const;

  [[nodiscard]] std::string deterministic_hash() const;

  // Geodesic cell buffers (rhombus-atlas persistence, plan addendum b):
  // float data over the cells of a GeodesicGrid of the given frequency.
  // Not part of deterministic_hash (raster layers remain the hashed truth).
  struct CellLayer {
    int frequency = 0;
    std::vector<float> data;
  };
  void set_cell_layer(const std::string& name, int frequency, std::vector<float> data);
  [[nodiscard]] bool has_cell_layer(const std::string& name) const;
  [[nodiscard]] const CellLayer& cell_layer(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> list_cell_layers() const;

 private:
  std::unordered_map<std::string, CellLayer> cell_layers_;
  LayerData& mutable_layer_or_throw(const std::string& name);
  [[nodiscard]] const LayerData& layer_or_throw(const std::string& name) const;

  TerrainDomain domain_;
  int seed_ = 0;
  std::unordered_map<std::string, LayerData> layers_;
};

}  // namespace world_engine::terrain
