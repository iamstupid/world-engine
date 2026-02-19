#pragma once

namespace world_engine::terrain::procedural {

struct NoiseParams {
  double base_frequency = 0.8;
  int octaves = 6;
  double lacunarity = 2.0;
  double gain = 0.5;
  double amplitude_m = 5000.0;      // large range: hypsometric curve creates bimodal distribution
};

struct TectonicsParams {
  int plate_count = 14;
  int simulation_steps = 250;
  double dt_myr = 1.0;
  double boundary_width_px = 5.0;
  double uplift_scale_m = 5000.0;   // tall mountain chains at convergent boundaries
  double ridge_scale_m = 2000.0;
  double tectonic_mix = 0.50;       // balanced noise + tectonic contribution
};

struct ErosionParams {
  int fixed_point_iterations = 6;
  int multigrid_levels = 5;
  double k = 1.2e-6;               // moderate erosion preserves mountain height
  double m = 0.45;
  double time_years = 200000.0;     // less time to preserve peaks
  bool enable_hillslope = true;
  double hillslope_k = 0.02;
  bool enable_thermal = true;
  double thermal_critical_slope = 0.577350269;  // tan(30deg)
};

struct HydrologyParams {
  float river_area_threshold_m2 = 1.5e10f;  // lower threshold = more visible rivers
  float sea_level_m = 0.0f;
};

struct PipelineParams {
  int seed = 1337;
  int width = 4096;
  int height = 2048;
  double radius_m = 6'371'000.0;

  NoiseParams noise{};
  TectonicsParams tectonics{};
  ErosionParams erosion{};
  HydrologyParams hydrology{};
};

}  // namespace world_engine::terrain::procedural
