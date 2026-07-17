#include "world_engine/terrain/procedural/graph_ops.h"

#include <cmath>
#include <memory>

#include "FastNoiseLite.h"
#include "world_engine/terrain/geodesic_grid.h"

namespace world_engine::terrain::procedural {

std::vector<float> noise_cells(int frequency, int seed, double base_frequency,
                               int octaves, double lacunarity, double gain,
                               double amplitude, bool ridged) {
  const GeodesicGrid grid(frequency);
  const int n = grid.cell_count();
  FastNoiseLite noise(seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(ridged ? FastNoiseLite::FractalType_Ridged
                              : FastNoiseLite::FractalType_FBm);
  noise.SetFractalOctaves(octaves);
  noise.SetFractalLacunarity(static_cast<float>(lacunarity));
  noise.SetFractalGain(static_cast<float>(gain));
  noise.SetFrequency(static_cast<float>(base_frequency));
  std::vector<float> out(n);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    const Vec3d& c = grid.cell_center(i);
    out[i] = static_cast<float>(amplitude) *
             noise.GetNoise(static_cast<float>(c.x), static_cast<float>(c.y),
                            static_cast<float>(c.z));
  }
  return out;
}

std::vector<float> resample_cells(int src_freq, const std::vector<float>& src,
                                  int dst_freq) {
  const GeodesicGrid sg(src_freq);
  const GeodesicGrid dg(dst_freq);
  const int n = dg.cell_count();
  std::vector<float> out(n);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < n; ++i) {
    const auto lk = sg.locate(dg.cell_center(i));
    float v = 0.0f;
    for (int k = 0; k < 3; ++k) {
      v += static_cast<float>(lk.weight[k]) * src[lk.cell[k]];
    }
    out[i] = v;
  }
  return out;
}

std::vector<float> rasterize_cells(int freq, const std::vector<float>& cells,
                                   int width, int height) {
  const GeodesicGrid grid(freq);
  std::vector<float> out(static_cast<size_t>(width) * height);
  constexpr double kPi = 3.14159265358979323846;
  #pragma omp parallel for schedule(dynamic, 8)
  for (int y = 0; y < height; ++y) {
    const double lat = (90.0 - (y + 0.5) / height * 180.0) * kPi / 180.0;
    for (int x = 0; x < width; ++x) {
      const double lon = ((x + 0.5) / width * 360.0 - 180.0) * kPi / 180.0;
      const Vec3d p{std::cos(lat) * std::cos(lon), std::sin(lat),
                    std::cos(lat) * std::sin(lon)};
      const auto lk = grid.locate(p);
      double v = 0.0;
      for (int k = 0; k < 3; ++k) {
        v += lk.weight[k] * cells[lk.cell[k]];
      }
      out[static_cast<size_t>(y) * width + x] = static_cast<float>(v);
    }
  }
  return out;
}

}  // namespace world_engine::terrain::procedural
