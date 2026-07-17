#include "world_engine/terrain/procedural/param_schema.h"

#include <cmath>
#include <sstream>

namespace world_engine::terrain::procedural {
namespace {

// X(group, field, lvalue, type, min, max, description)
// type: I = integer, D = double, B = boolean (0/1), F = float
#define WE_PARAM_TABLE(X, p)                                                                     \
  X("world", "seed", (p).seed, I, 0, 2147483647, "Global random seed")                           \
  X("world", "width", (p).width, I, 64, 16384, "Export raster width")                            \
  X("world", "height", (p).height, I, 32, 8192, "Export raster height")                          \
  X("world", "radius_m", (p).radius_m, D, 100000, 1e9, "Planet radius (m)")                      \
  X("world", "physics_grid_frequency", (p).physics_grid_frequency, I, 0, 4096,                   \
    "Geodesic physics grid frequency (0 = raster path)")                                          \
  X("noise", "base_frequency", (p).noise.base_frequency, D, 0.05, 16, "Detail noise frequency")  \
  X("noise", "octaves", (p).noise.octaves, I, 1, 12, "Detail noise octaves")                     \
  X("noise", "lacunarity", (p).noise.lacunarity, D, 1.2, 4, "Fractal lacunarity")                \
  X("noise", "gain", (p).noise.gain, D, 0.1, 0.9, "Fractal gain")                                \
  X("noise", "amplitude_m", (p).noise.amplitude_m, D, 0, 4000, "Detail amplitude (m)")           \
  X("tectonics", "grid_frequency", (p).tectonics.grid_frequency, I, 4, 1024,                     \
    "Tectonic geodesic grid frequency")                                                           \
  X("tectonics", "plate_count", (p).tectonics.plate_count, I, 2, 60, "Initial plate count")      \
  X("tectonics", "simulation_steps", (p).tectonics.simulation_steps, I, 0, 1000,                 \
    "Simulation steps")                                                                           \
  X("tectonics", "dt_myr", (p).tectonics.dt_myr, D, 0.1, 10, "Step length (My)")                 \
  X("tectonics", "resample_interval_steps", (p).tectonics.resample_interval_steps, I, 1, 100,    \
    "Global resampling cadence (steps)")                                                          \
  X("tectonics", "continent_ratio", (p).tectonics.continent_ratio, D, 0.05, 0.95,                \
    "Initial continental cell fraction")                                                          \
  X("tectonics", "continent_noise_freq", (p).tectonics.continent_noise_freq, D, 0.1, 4,          \
    "Continent seed noise frequency")                                                             \
  X("tectonics", "continental_base_m", (p).tectonics.continental_base_m, D, 0, 2000,             \
    "Initial continental elevation (m)")                                                          \
  X("tectonics", "oceanic_base_m", (p).tectonics.oceanic_base_m, D, -8000, -1000,                \
    "Initial oceanic elevation (m)")                                                              \
  X("tectonics", "max_plate_speed_mm_yr", (p).tectonics.max_plate_speed_mm_yr, D, 10, 300,       \
    "Max plate speed v0 (mm/yr)")                                                                 \
  X("tectonics", "min_plate_speed_frac", (p).tectonics.min_plate_speed_frac, D, 0, 1,            \
    "Min plate speed fraction of v0")                                                             \
  X("tectonics", "subduction_uplift_mm_yr", (p).tectonics.subduction_uplift_mm_yr, D, 0, 5,      \
    "Subduction uplift u0 (mm/yr)")                                                               \
  X("tectonics", "subduction_distance_km", (p).tectonics.subduction_distance_km, D, 200, 4000,   \
    "Subduction reach r_s (km)")                                                                  \
  X("tectonics", "collision_max_uplift_m", (p).tectonics.collision_max_uplift_m, D, 0, 8000,     \
    "Collision surge cap (m)")                                                                    \
  X("tectonics", "rift_events_per_100myr", (p).tectonics.rift_events_per_100myr, D, 0, 5,        \
    "Rifting rate lambda0")                                                                       \
  X("tectonics", "slab_pull_strength", (p).tectonics.slab_pull_strength, D, 0, 1,                \
    "Slab pull axis blend per 100 My")                                                            \
  X("tectonics", "uplift_window_myr", (p).tectonics.uplift_window_myr, D, 1, 100,                \
    "Recent-uplift EMA window (My)")                                                              \
  X("tectonics", "noise_detail_mix", (p).tectonics.noise_detail_mix, D, 0, 1,                    \
    "Detail noise blend")                                                                         \
  X("erosion", "fixed_point_iterations", (p).erosion.fixed_point_iterations, I, 1, 64,           \
    "Fixed-point iterations")                                                                     \
  X("erosion", "multigrid_levels", (p).erosion.multigrid_levels, I, 1, 8, "Multigrid levels")    \
  X("erosion", "k", (p).erosion.k, D, 1e-8, 1e-4, "Stream power k")                              \
  X("erosion", "m", (p).erosion.m, D, 0.2, 0.8, "Stream power m")                                \
  X("erosion", "time_years", (p).erosion.time_years, D, 1e4, 2e7, "Erosion horizon (yr)")        \
  X("erosion", "enable_hillslope", (p).erosion.enable_hillslope, B, 0, 1, "Hillslope term")      \
  X("erosion", "hillslope_k", (p).erosion.hillslope_k, D, 0, 1, "Hillslope k_h (m^2/yr)")        \
  X("erosion", "enable_thermal", (p).erosion.enable_thermal, B, 0, 1, "Thermal clamp")           \
  X("erosion", "thermal_critical_slope", (p).erosion.thermal_critical_slope, D, 0.1, 2,          \
    "Critical slope (tan)")                                                                       \
  X("erosion", "fixed_point_ema", (p).erosion.fixed_point_ema, D, 0.05, 1,                       \
    "Fixed-point EMA weight")                                                                     \
  X("hydrology", "river_area_threshold_m2", (p).hydrology.river_area_threshold_m2, F, 1e7,       \
    1e12, "River threshold (m^2 drainage)")                                                       \
  X("hydrology", "sea_level_m", (p).hydrology.sea_level_m, F, -2000, 2000, "Sea level (m)")   \
  X("hydrology", "flood_lowstand_m", (p).hydrology.flood_lowstand_m, F, 0, 500,               \
    "Drowned-coast lowstand depth (m)")

const char* type_name_I() { return "int"; }
const char* type_name_D() { return "double"; }
const char* type_name_B() { return "bool"; }
const char* type_name_F() { return "double"; }

}  // namespace

std::string params_schema_json() {
  PipelineParams defaults;
  std::ostringstream os;
  os << "{\"groups\":{";
  std::string current_group;
  bool first_group = true;
  bool first_field = true;
  const auto flush_group = [&](const std::string& g) {
    if (g == current_group) {
      return;
    }
    if (!current_group.empty()) {
      os << "]";
    }
    if (!first_group) {
      os << ",";
    }
    os << "\"" << g << "\":[";
    current_group = g;
    first_group = false;
    first_field = true;
  };
#define WE_EMIT(group, name, lval, type, mn, mx, desc)                                    \
  flush_group(group);                                                                     \
  if (!first_field) {                                                                     \
    os << ",";                                                                            \
  }                                                                                       \
  first_field = false;                                                                    \
  os << "{\"name\":\"" << name << "\",\"type\":\"" << type_name_##type()                  \
     << "\",\"default\":" << static_cast<double>(lval) << ",\"min\":" << (double)(mn)     \
     << ",\"max\":" << (double)(mx) << ",\"desc\":\"" << desc << "\"}";
  WE_PARAM_TABLE(WE_EMIT, defaults)
#undef WE_EMIT
  if (!current_group.empty()) {
    os << "]";
  }
  os << "}}";
  return os.str();
}

bool set_param(PipelineParams& params, const std::string& key, double value) {
#define WE_SET(group, name, lval, type, mn, mx, desc)                        \
  if (key == std::string(group) + "." + name) {                              \
    const double v = std::min((double)(mx), std::max((double)(mn), value));  \
    lval = static_cast<std::remove_reference_t<decltype(lval)>>(             \
        std::string(#type) == "I" ? std::round(v) : v);                      \
    return true;                                                             \
  }
  WE_PARAM_TABLE(WE_SET, params)
#undef WE_SET
  return false;
}

bool get_param(const PipelineParams& params, const std::string& key, double& out) {
#define WE_GET(group, name, lval, type, mn, mx, desc)   \
  if (key == std::string(group) + "." + name) {         \
    out = static_cast<double>(lval);                    \
    return true;                                        \
  }
  WE_PARAM_TABLE(WE_GET, params)
#undef WE_GET
  return false;
}

std::vector<std::string> param_keys() {
  std::vector<std::string> keys;
#define WE_KEY(group, name, lval, type, mn, mx, desc) \
  keys.push_back(std::string(group) + "." + name);
  PipelineParams dummy;
  WE_PARAM_TABLE(WE_KEY, dummy)
#undef WE_KEY
  return keys;
}

}  // namespace world_engine::terrain::procedural
