#pragma once

#include <vector>

namespace world_engine::terrain::procedural {

// Graph operators (DAG pipeline, docs/PIPELINE_DAG_DESIGN.md): pure cell
// field functions exposed to the Python executor. Cell fields are f32
// arrays over the 10F^2+2 cells of a frequency-F geodesic grid.

// Fractal noise evaluated directly at cell centers on the unit sphere.
std::vector<float> noise_cells(int frequency, int seed, double base_frequency,
                               int octaves, double lacunarity, double gain,
                               double amplitude, bool ridged);

// Barycentric resample between grid frequencies (locate + 3-cell weights).
std::vector<float> resample_cells(int src_freq, const std::vector<float>& src,
                                  int dst_freq);

// Cell field -> equirect raster (export view; y-up lat/lon convention).
std::vector<float> rasterize_cells(int freq, const std::vector<float>& cells,
                                   int width, int height);

}  // namespace world_engine::terrain::procedural
