#pragma once

#include "io/fild_header.hpp"
#include "mesh/ico_mesh.hpp"
#include "mesh/dtype_traits.hpp"
#include <vector>
#include <cstdint>

template<typename T>
std::vector<uint8_t> serialize_field(const IcoMesh<T>& mesh, int compression = 1);

template<typename T>
IcoMesh<T> deserialize_field(const uint8_t* data, size_t len, const IcoTopology* topo);
