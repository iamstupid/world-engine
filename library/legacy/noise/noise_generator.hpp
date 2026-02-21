#pragma once
#include "legacy/mesh/field.hpp"
#include "legacy/mesh/icosahedral_geodesic.hpp"
#include "noise/noise_params.hpp"
#include "FastNoiseLite.h"

namespace legacy {

template<typename Mesh>
Field<Mesh> generate_noise(const Mesh& mesh, const NoiseParams& params);

} // namespace legacy
