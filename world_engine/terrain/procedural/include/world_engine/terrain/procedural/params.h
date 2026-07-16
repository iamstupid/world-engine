#pragma once

namespace world_engine::terrain::procedural {

struct NoiseParams {
  double base_frequency = 0.8;
  int octaves = 6;
  double lacunarity = 2.0;
  double gain = 0.5;
  double amplitude_m = 800.0;  // detail layer only; tectonics drives large-scale elevation
};

// Lagrangian plate tectonics (Cortial et al. 2019, "Procedural Tectonic
// Planets"). Crust lives on an icosahedral geodesic grid; plates are point
// sets carried by rigid rotations; the canonical grid is re-populated by
// global resampling. Constants follow the paper's Appendix A unless noted.
struct TectonicsParams {
  int grid_frequency = 100;  // 10*F^2+2 crust cells (100 -> 100,002, ~67 km)
  int plate_count = 14;
  int simulation_steps = 125;  // ~250 My at dt = 2 My
  double dt_myr = 2.0;         // paper: delta_t = 2 My
  int resample_interval_steps = 20;  // global resampling cadence (paper: 10-60)

  // Initial crust distribution (per-cell, independent of plate partition).
  double continent_ratio = 0.40;      // fraction of cells continental at t=0
  double continent_noise_freq = 0.7;  // unit-sphere frequency of seed patches

  // Reference elevations (paper Appendix A).
  double continental_base_m = 400.0;
  double oceanic_base_m = -4200.0;
  double ridge_crest_m = -1000.0;    // z_r: oceanic ridge crest
  double abyssal_m = -6000.0;        // z_a: abyssal plains
  double trench_depth_m = -10000.0;  // z_t: oceanic trench
  double max_continental_m = 10000.0;  // z_c: highest continental altitude

  // Plate motion.
  double max_plate_speed_mm_yr = 100.0;  // v0
  double min_plate_speed_frac = 0.2;     // omega drawn in [frac, 1] * v0/R

  // Subduction (paper Section 4.1).
  double subduction_uplift_mm_yr = 0.6;   // u0
  double subduction_distance_km = 1800.0; // r_s: max uplift range from front

  // Continental collision (paper Section 4.2).
  double collision_coeff_per_km = 1.3e-5;   // delta_c
  double collision_distance_km = 4200.0;    // r_c
  double collision_interpen_km = 300.0;     // event trigger threshold
  // Cap on the instantaneous collision surge (delta_c * A alone reaches tens
  // of km for subcontinent-sized terranes; the paper leaves this unregulated
  // aside from the z_c clamp).
  double collision_max_uplift_m = 3500.0;

  // Rifting (paper Section 4.4).
  double rift_events_per_100myr = 0.7;  // lambda_0 (paper leaves unspecified)

  // Slab pull (paper Section 4.1): axis drift toward subduction fronts.
  double slab_pull_strength = 0.3;  // axis blend fraction per 100 My

  // Per-step elevation modifications (paper Section 4.5), mm/yr.
  double continental_erosion_mm_yr = 0.03;  // eps_c
  double oceanic_dampening_mm_yr = 0.04;    // eps_o
  double sediment_accretion_mm_yr = 0.3;    // eps_t

  // Uplift map for the downstream analytical erosion stage: average tectonic
  // uplift over the trailing window (recent orogeny), not the full run.
  double uplift_window_myr = 20.0;

  // Detail-noise blend in the combine stage.
  double noise_detail_mix = 0.55;
};

struct ErosionParams {
  int fixed_point_iterations = 6;
  int multigrid_levels = 5;
  double k = 1.2e-6;               // moderate erosion preserves mountain height
  double m = 0.45;
  double time_years = 2.0e6;        // ~landscape response time; big channels
                                    // reach steady state, ridges stay young
  // Hillslope enters the advection coefficient (Tzathas Eq 26):
  //   a(s) = k*A^m + (k_h / C) * A^(-h)
  bool enable_hillslope = true;
  double hillslope_k = 0.05;        // k_h [m^2/yr]
  double hack_c = 1.5;              // Hack's law constant C
  double hack_h = 0.6;              // Hack's law exponent h
  bool enable_thermal = true;
  double thermal_critical_slope = 0.577350269;  // tan(30deg)
  // Fixed-point damping (Tzathas 5.1): exponential moving average between
  // iterations prevents oscillation at small t.
  double fixed_point_ema = 0.65;    // weight of the fresh solution
};

struct HydrologyParams {
  float river_area_threshold_m2 = 8.0e9f;   // lower threshold = more visible rivers
  float sea_level_m = 0.0f;
};

struct PipelineParams {
  int seed = 1337;
  int width = 4096;
  int height = 2048;
  double radius_m = 6'371'000.0;
  // > 0: run erosion/hydrology/masks on a dense geodesic grid of this
  // frequency (rounded to F0 * 2^(multigrid_levels-1)); 0: lat-lon raster
  // path. The lat-lon raster remains the export/preview format either way.
  int physics_grid_frequency = 0;

  NoiseParams noise{};
  TectonicsParams tectonics{};
  ErosionParams erosion{};
  HydrologyParams hydrology{};
};

}  // namespace world_engine::terrain::procedural
