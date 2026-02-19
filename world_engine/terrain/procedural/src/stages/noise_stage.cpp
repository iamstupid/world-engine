#include "world_engine/terrain/procedural/stages/noise_stage.h"

#include <algorithm>
#include <cmath>

#include "FastNoiseLite.h"

namespace world_engine::terrain::procedural::stages {
namespace {

// Asymmetric hypsometric curve inspired by real Earth elevation distribution:
// - Ocean floor: mostly flat around -3 to -5 km (abyssal plains)
// - Continental shelf: 0 to -200m
// - Continental lowlands: 0 to 500m (largest land area)
// - Mountains: rare peaks up to 3-5km
// Input v in [-1, 1], output remapped asymmetrically
float hypsometric_curve(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  if (v < 0.0f) {
    // Ocean: power curve with exponent < 1 flattens the deep ocean
    // while keeping the continental shelf steep
    const float t = -v;  // 0..1
    return -std::pow(t, 0.55f);
  } else {
    // Land: power curve with exponent > 1 concentrates most area
    // near sea level while allowing tall mountain peaks
    const float t = v;  // 0..1
    return std::pow(t, 1.5f);  // power 1.5: lowland-dominated but allows tall peaks
  }
}

}  // namespace

void run_noise_stage(const PipelineParams& params, TerrainDataset& dataset) {
  if (!dataset.has_layer("elevation_primeval_m")) {
    dataset.create_float_layer("elevation_primeval_m", "m",
                               "Primeval spherical FastNoiseLite OpenSimplex2 fractal elevation");
  }

  auto& out = dataset.float_layer("elevation_primeval_m");
  const auto& domain = dataset.domain();

  // Main terrain noise
  FastNoiseLite noise(params.seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  noise.SetFractalOctaves(params.noise.octaves);
  noise.SetFractalLacunarity(static_cast<float>(params.noise.lacunarity));
  noise.SetFractalGain(static_cast<float>(params.noise.gain));
  noise.SetFrequency(static_cast<float>(params.noise.base_frequency));

  // Domain warp noise for breaking symmetry and creating organic shapes
  FastNoiseLite warp(params.seed + 7919);
  warp.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  warp.SetFractalType(FastNoiseLite::FractalType_FBm);
  warp.SetFractalOctaves(3);
  warp.SetFractalLacunarity(2.0f);
  warp.SetFractalGain(0.5f);
  warp.SetFrequency(static_cast<float>(params.noise.base_frequency * 0.5));

  // Continent-scale noise (very low frequency) for large landmass shapes
  FastNoiseLite continent_noise(params.seed + 4217);
  continent_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  continent_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  continent_noise.SetFractalOctaves(3);
  continent_noise.SetFractalLacunarity(2.0f);
  continent_noise.SetFractalGain(0.5f);
  continent_noise.SetFrequency(static_cast<float>(params.noise.base_frequency * 0.25));

  // Ridged noise for mountain detail
  FastNoiseLite ridge_noise(params.seed + 1619);
  ridge_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  ridge_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
  ridge_noise.SetFractalOctaves(4);
  ridge_noise.SetFractalLacunarity(static_cast<float>(params.noise.lacunarity));
  ridge_noise.SetFractalGain(0.55f);
  ridge_noise.SetFrequency(static_cast<float>(params.noise.base_frequency * 2.0));

  const float warp_strength = 0.35f;
  const float amp = static_cast<float>(params.noise.amplitude_m);

  #pragma omp parallel for schedule(dynamic, 4)
  for (int y = 0; y < domain.height(); ++y) {
    for (int x = 0; x < domain.width(); ++x) {
      const int idx = domain.index(x, y);
      const auto ll = domain.center_lat_lon_deg(x, y);
      const auto p = domain.lat_lon_to_xyz(ll);
      const float px = static_cast<float>(p.x);
      const float py = static_cast<float>(p.y);
      const float pz = static_cast<float>(p.z);

      // Domain warping: offset the sample position for organic coastlines
      const float wx = warp.GetNoise(px * 1.5f, py * 1.5f, pz * 1.5f) * warp_strength;
      const float wy = warp.GetNoise(px * 1.5f + 100.0f, py * 1.5f + 100.0f, pz * 1.5f + 100.0f) * warp_strength;
      const float wz = warp.GetNoise(px * 1.5f + 200.0f, py * 1.5f + 200.0f, pz * 1.5f + 200.0f) * warp_strength;

      const float wpx = px + wx;
      const float wpy = py + wy;
      const float wpz = pz + wz;

      // Main terrain value (warped)
      const float n_main = noise.GetNoise(wpx, wpy, wpz);

      // Continent-scale value for large structures
      const float n_continent = continent_noise.GetNoise(px, py, pz);

      // Ridged noise for mountain detail (only on elevated areas)
      const float n_ridge = ridge_noise.GetNoise(wpx, wpy, wpz);

      // Blend: continent shape + terrain detail + ridge detail on peaks
      float combined = n_continent * 0.45f + n_main * 0.40f;
      const float ridge_mask = std::clamp(combined * 2.0f, 0.0f, 1.0f);
      combined += n_ridge * 0.15f * ridge_mask;

      // Apply hypsometric curve for realistic elevation distribution
      const float shaped = hypsometric_curve(combined);

      // Ocean bias: shift distribution so ~60% is below sea level.
      // The quadratic land curve already compresses land, so mild bias suffices.
      const float ocean_bias = -0.08f;
      out[idx] = (shaped + ocean_bias) * amp;
    }
  }
}

}  // namespace world_engine::terrain::procedural::stages
