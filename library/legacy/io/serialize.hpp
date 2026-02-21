#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "legacy/mesh/field.hpp"
#include "legacy/mesh/icosahedral_geodesic.hpp"
#include "io/fild_header.hpp"

namespace legacy {

std::vector<uint8_t> serialize_field_packet(const Field<IcoMesh>& field, int N, int compression);
Field<IcoMesh> deserialize_field_packet(const uint8_t* data, size_t len, const IcoMesh* mesh);

} // namespace legacy
