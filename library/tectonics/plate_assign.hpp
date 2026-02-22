#pragma once

#include "tectonics/plate_params.hpp"
#include "mesh/ico_topology.hpp"
#include "mesh/ico_mesh.hpp"

/// Assign tectonic plates via multi-seed Dijkstra with FBM-weighted edges.
/// Returns IcoMesh<uint16_t> where each cell holds its plate ID [0, num_plates).
/// If elevation is non-null, ocean cells (elevation <= 0) get higher edge cost
/// controlled by ocean_bias, encouraging plate boundaries to form in oceans.
IcoMesh<uint16_t> assign_plates(const IcoTopology* topo,
                                 const PlateAssignParams& params,
                                 const float* elevation = nullptr);
