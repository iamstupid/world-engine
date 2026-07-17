#pragma once

// Single source of truth for the tunable parameter surface: an X-macro table
// generates the JSON Schema export and the string-keyed get/set used by the
// bindings/server, so UI forms can never drift from params.h.

#include <string>
#include <vector>

#include "world_engine/terrain/procedural/params.h"

namespace world_engine::terrain::procedural {

// JSON document: {"groups": {group: [{name, type, default, min, max, desc}]}}
std::string params_schema_json();

// Keys are "group.field" (e.g. "tectonics.plate_count"); root fields use
// "world.field" (e.g. "world.seed"). Booleans are 0/1.
bool set_param(PipelineParams& params, const std::string& key, double value);
bool get_param(const PipelineParams& params, const std::string& key, double& out);
std::vector<std::string> param_keys();

}  // namespace world_engine::terrain::procedural
