#pragma once

#include "noise/noise_params.hpp"
#include "mesh/mesh_concept.hpp"
#include "mesh/ico_mesh.hpp"

template<Mesh M>
M generate_noise(const typename M::topology_type* topo, const NoiseParams& params)
    requires std::same_as<typename M::value_type, float>;
