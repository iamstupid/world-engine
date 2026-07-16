#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "lodepng.h"
#include "world_engine/terrain/procedural/procedural_generator.h"

namespace {
using world_engine::terrain::procedural::PipelineParams;
using world_engine::terrain::procedural::ProceduralGenerator;

struct Rgb {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
};

Rgb lerp_rgb(const Rgb& a, const Rgb& b, float t) {
  const float u = std::clamp(t, 0.0f, 1.0f);
  const auto blend = [u](unsigned char x, unsigned char y) -> unsigned char {
    return static_cast<unsigned char>(static_cast<float>(x) +
                                      (static_cast<float>(y) - static_cast<float>(x)) * u + 0.5f);
  };
  return {blend(a.r, b.r), blend(a.g, b.g), blend(a.b, b.b)};
}

bool write_png8_elevation_color(const std::string& path, const std::vector<float>& data, int w, int h,
                                float sea_level_m) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }

  // Use absolute elevation references for consistent, informative visualization.
  // Ocean: sea_level down to -6000m (deep blue)
  // Land:  sea_level to 200m (dark green), 200-800m (green-yellow),
  //        800-2000m (yellow-red), 2000-4000m (red-purple), 4000m+ (purple-white)
  const float ocean_floor = -6000.0f;
  const float below_span = std::max(1e-6f, sea_level_m - ocean_floor);

  const Rgb shallow_blue{70, 150, 245};
  const Rgb mid_blue{30, 80, 180};
  const Rgb deep_blue{5, 15, 80};
  const Rgb dark_green{20, 100, 30};
  const Rgb green{50, 170, 60};
  const Rgb yellow{220, 200, 40};
  const Rgb orange{210, 120, 30};
  const Rgb red{190, 50, 30};
  const Rgb purple{140, 60, 170};
  const Rgb white{255, 255, 255};

  std::vector<unsigned char> raw(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u, 0);
  for (int i = 0; i < w * h; ++i) {
    const float z = data[i];
    Rgb c{};
    if (z < sea_level_m) {
      const float depth = sea_level_m - z;
      if (depth < 200.0f) {
        c = lerp_rgb(shallow_blue, mid_blue, depth / 200.0f);
      } else {
        const float t = std::clamp((depth - 200.0f) / (below_span - 200.0f), 0.0f, 1.0f);
        c = lerp_rgb(mid_blue, deep_blue, t);
      }
    } else {
      const float elev = z - sea_level_m;
      if (elev < 200.0f) {
        c = lerp_rgb(dark_green, green, elev / 200.0f);
      } else if (elev < 800.0f) {
        c = lerp_rgb(green, yellow, (elev - 200.0f) / 600.0f);
      } else if (elev < 2000.0f) {
        c = lerp_rgb(yellow, orange, (elev - 800.0f) / 1200.0f);
      } else if (elev < 4000.0f) {
        c = lerp_rgb(orange, red, (elev - 2000.0f) / 2000.0f);
      } else if (elev < 6000.0f) {
        c = lerp_rgb(red, purple, (elev - 4000.0f) / 2000.0f);
      } else {
        c = lerp_rgb(purple, white, std::clamp((elev - 6000.0f) / 3000.0f, 0.0f, 1.0f));
      }
    }
    raw[3 * i + 0] = c.r;
    raw[3 * i + 1] = c.g;
    raw[3 * i + 2] = c.b;
  }

  const unsigned err =
      lodepng::encode(path, raw, static_cast<unsigned>(w), static_cast<unsigned>(h), LCT_RGB, 8);
  if (err != 0) {
    std::cerr << "PNG encode failed for " << path << ": " << lodepng_error_text(err) << "\n";
    return false;
  }
  return true;
}

bool write_png16_gray(const std::string& path, const std::vector<float>& data, int w, int h) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }

  float mn = std::numeric_limits<float>::max();
  float mx = std::numeric_limits<float>::lowest();
  for (float v : data) {
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  const float span = std::max(1e-6f, mx - mn);

  // lodepng expects 16-bit samples in big-endian byte order.
  std::vector<unsigned char> raw(static_cast<size_t>(w) * static_cast<size_t>(h) * 2u, 0);
  for (int i = 0; i < w * h; ++i) {
    const float t = std::clamp((data[i] - mn) / span, 0.0f, 1.0f);
    const uint16_t u = static_cast<uint16_t>(t * 65535.0f + 0.5f);
    raw[2 * i + 0] = static_cast<unsigned char>((u >> 8) & 0xFF);
    raw[2 * i + 1] = static_cast<unsigned char>(u & 0xFF);
  }

  lodepng::State state;
  state.info_raw.colortype = LCT_GREY;
  state.info_raw.bitdepth = 16;
  state.info_png.color.colortype = LCT_GREY;
  state.info_png.color.bitdepth = 16;
  std::vector<unsigned char> png;
  unsigned err = lodepng::encode(png, raw, static_cast<unsigned>(w), static_cast<unsigned>(h), state);
  if (err == 0) {
    err = lodepng::save_file(png, path);
  }
  if (err != 0) {
    std::cerr << "PNG encode failed for " << path << ": " << lodepng_error_text(err) << "\n";
    return false;
  }
  return true;
}

bool write_png8_mask(const std::string& path, const std::vector<uint8_t>& data, int w, int h) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }
  std::vector<unsigned char> raw(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  for (size_t i = 0; i < data.size(); ++i) {
    raw[i] = data[i] ? static_cast<unsigned char>(255) : static_cast<unsigned char>(0);
  }

  const unsigned err =
      lodepng::encode(path, raw, static_cast<unsigned>(w), static_cast<unsigned>(h), LCT_GREY, 8);
  if (err != 0) {
    std::cerr << "PNG encode failed for " << path << ": " << lodepng_error_text(err) << "\n";
    return false;
  }
  return true;
}

bool write_png8_preview(const std::string& path, const std::vector<float>& data, int w, int h) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }
  float mn = std::numeric_limits<float>::max();
  float mx = std::numeric_limits<float>::lowest();
  for (float v : data) {
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  const float span = std::max(1e-6f, mx - mn);

  std::vector<unsigned char> raw(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  for (int i = 0; i < w * h; ++i) {
    const float t = std::clamp((data[i] - mn) / span, 0.0f, 1.0f);
    raw[i] = static_cast<unsigned char>(t * 255.0f + 0.5f);
  }

  const unsigned err =
      lodepng::encode(path, raw, static_cast<unsigned>(w), static_cast<unsigned>(h), LCT_GREY, 8);
  if (err != 0) {
    std::cerr << "PNG encode failed for " << path << ": " << lodepng_error_text(err) << "\n";
    return false;
  }
  return true;
}

// FNV-1a over raw bytes; stable across platforms for identical float bit patterns.
uint64_t fnv1a_hash(const void* data, size_t bytes) {
  const auto* p = static_cast<const unsigned char*>(data);
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < bytes; ++i) {
    hash ^= p[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Area-weighted metrics: equirectangular rows must be weighted by cell area,
// otherwise polar rows dominate cell-count statistics.
void write_metrics(std::ostream& os, const world_engine::terrain::TerrainDataset& ds,
                   float sea_level_m) {
  const auto& domain = ds.domain();
  const int w = domain.width();
  const int h = domain.height();
  const auto& elev = ds.float_layer("elevation_eroded_m");

  double total_area = 0.0;
  double ocean_area = 0.0;
  std::vector<std::pair<float, double>> weighted;  // (elevation, area)
  weighted.reserve(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    const double a = domain.cell_area_m2(y);
    for (int x = 0; x < w; ++x) {
      const float z = elev[domain.index(x, y)];
      total_area += a;
      if (z < sea_level_m) {
        ocean_area += a;
      }
      weighted.emplace_back(z, a);
    }
  }
  std::sort(weighted.begin(), weighted.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  const auto percentile = [&](double q) -> float {
    double acc = 0.0;
    for (const auto& [z, a] : weighted) {
      acc += a;
      if (acc >= q * total_area) {
        return z;
      }
    }
    return weighted.back().first;
  };

  os << "metrics:\n";
  os << "  elevation_min_m: " << weighted.front().first << "\n";
  os << "  elevation_max_m: " << weighted.back().first << "\n";
  os << "  ocean_area_fraction: " << (ocean_area / total_area) << "\n";
  os << "  hypsometric_percentiles_m:";
  for (const double q : {0.01, 0.05, 0.25, 0.50, 0.75, 0.95, 0.99}) {
    os << " p" << static_cast<int>(q * 100) << "=" << percentile(q);
  }
  os << "\n";

  // Area-weighted elevation histogram (500 m bins from -8000 to 8000) for
  // eyeballing bimodality (ocean mode near -4 km, land mode < +500 m).
  constexpr int kBins = 32;
  constexpr float kLo = -8000.0f;
  constexpr float kHi = 8000.0f;
  std::vector<double> hist(kBins, 0.0);
  for (const auto& [z, a] : weighted) {
    const int b = std::clamp(
        static_cast<int>((z - kLo) / (kHi - kLo) * kBins), 0, kBins - 1);
    hist[b] += a;
  }
  os << "  hypsometric_histogram_500m_bins:";
  for (double v : hist) {
    os << " " << static_cast<int>(1000.0 * v / total_area + 0.5);
  }
  os << "  (permille per bin, -8km..+8km)\n";

  os << "  layer_hashes:\n";
  for (const auto& name : ds.list_layers()) {
    uint64_t hv = 0;
    switch (ds.layer_type(name)) {
      case world_engine::terrain::LayerDataType::kFloat32: {
        const auto& v = ds.float_layer(name);
        hv = fnv1a_hash(v.data(), v.size() * sizeof(float));
        break;
      }
      case world_engine::terrain::LayerDataType::kUInt8: {
        const auto& v = ds.u8_layer(name);
        hv = fnv1a_hash(v.data(), v.size());
        break;
      }
      case world_engine::terrain::LayerDataType::kInt32: {
        const auto& v = ds.i32_layer(name);
        hv = fnv1a_hash(v.data(), v.size() * sizeof(int32_t));
        break;
      }
    }
    os << "    " << name << ": " << std::hex << hv << std::dec << "\n";
  }
}

bool write_pgm16(const std::string& path, const std::vector<float>& data, int w, int h) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }
  float mn = std::numeric_limits<float>::max();
  float mx = std::numeric_limits<float>::lowest();
  for (float v : data) {
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  const float span = std::max(1e-6f, mx - mn);

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "P5\n" << w << " " << h << "\n65535\n";
  for (float v : data) {
    const float t = std::clamp((v - mn) / span, 0.0f, 1.0f);
    const uint16_t u = static_cast<uint16_t>(t * 65535.0f + 0.5f);
    const uint8_t hi = static_cast<uint8_t>((u >> 8) & 0xFF);
    const uint8_t lo = static_cast<uint8_t>(u & 0xFF);
    out.put(static_cast<char>(hi));
    out.put(static_cast<char>(lo));
  }
  return true;
}

bool write_pgm8(const std::string& path, const std::vector<uint8_t>& data, int w, int h) {
  if (data.empty() || w <= 0 || h <= 0) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "P5\n" << w << " " << h << "\n255\n";
  std::vector<uint8_t> vis(data.size(), 0);
  for (size_t i = 0; i < data.size(); ++i) {
    vis[i] = data[i] ? static_cast<uint8_t>(255) : static_cast<uint8_t>(0);
  }
  out.write(reinterpret_cast<const char*>(vis.data()), static_cast<std::streamsize>(vis.size()));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  PipelineParams params;
  std::string out_dir = "output";
  bool also_write_pgm = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) {
      params.seed = std::stoi(argv[++i]);
    } else if (a == "--width" && i + 1 < argc) {
      params.width = std::stoi(argv[++i]);
    } else if (a == "--height" && i + 1 < argc) {
      params.height = std::stoi(argv[++i]);
    } else if (a == "--preview") {
      params.width = 1024;
      params.height = 512;
    } else if (a == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--sea-level" && i + 1 < argc) {
      params.hydrology.sea_level_m = std::stof(argv[++i]);
    } else if (a == "--erosion-iters" && i + 1 < argc) {
      params.erosion.fixed_point_iterations = std::max(1, std::stoi(argv[++i]));
    } else if (a == "--erosion-levels" && i + 1 < argc) {
      params.erosion.multigrid_levels = std::max(1, std::stoi(argv[++i]));
    } else if (a == "--erosion-k" && i + 1 < argc) {
      params.erosion.k = std::max(1.0e-12, std::stod(argv[++i]));
    } else if (a == "--erosion-time-years" && i + 1 < argc) {
      params.erosion.time_years = std::max(1.0, std::stod(argv[++i]));
    } else if (a == "--hillslope-k" && i + 1 < argc) {
      params.erosion.hillslope_k = std::max(0.0, std::stod(argv[++i]));
    } else if (a == "--disable-thermal") {
      params.erosion.enable_thermal = false;
    } else if (a == "--disable-hillslope") {
      params.erosion.enable_hillslope = false;
    } else if (a == "--river-threshold" && i + 1 < argc) {
      params.hydrology.river_area_threshold_m2 = std::stof(argv[++i]);
    } else if (a == "--tect-steps" && i + 1 < argc) {
      params.tectonics.simulation_steps = std::max(0, std::stoi(argv[++i]));
    } else if (a == "--tect-dt-myr" && i + 1 < argc) {
      params.tectonics.dt_myr = std::max(0.01, std::stod(argv[++i]));
    } else if (a == "--tect-freq" && i + 1 < argc) {
      params.tectonics.grid_frequency = std::max(4, std::stoi(argv[++i]));
    } else if (a == "--plates" && i + 1 < argc) {
      params.tectonics.plate_count = std::max(2, std::stoi(argv[++i]));
    } else if (a == "--continent-ratio" && i + 1 < argc) {
      params.tectonics.continent_ratio = std::clamp(std::stod(argv[++i]), 0.05, 0.95);
    } else if (a == "--noise-mix" && i + 1 < argc) {
      params.tectonics.noise_detail_mix = std::clamp(std::stod(argv[++i]), 0.0, 1.0);
    } else if (a == "--slab-pull" && i + 1 < argc) {
      params.tectonics.slab_pull_strength = std::max(0.0, std::stod(argv[++i]));
    } else if (a == "--rift-rate" && i + 1 < argc) {
      params.tectonics.rift_events_per_100myr = std::max(0.0, std::stod(argv[++i]));
    } else if (a == "--collision-cap" && i + 1 < argc) {
      params.tectonics.collision_max_uplift_m = std::max(0.0, std::stod(argv[++i]));
    } else if (a == "--write-pgm") {
      also_write_pgm = true;
    }
  }

  std::filesystem::create_directories(out_dir);

  ProceduralGenerator generator;
  generator.generate(params);
  const auto& ds = generator.dataset();

  const auto& elev = ds.float_layer("elevation_eroded_m");
  const auto& ocean = ds.u8_layer("ocean_mask");
  const auto& river = ds.u8_layer("river_mask");
  const auto& lake = ds.u8_layer("lake_mask");
  const int w = ds.domain().width();
  const int h = ds.domain().height();

  float mn = std::numeric_limits<float>::max();
  float mx = std::numeric_limits<float>::lowest();
  int ocean_count = 0;
  for (int i = 0; i < ds.size(); ++i) {
    mn = std::min(mn, elev[i]);
    mx = std::max(mx, elev[i]);
    ocean_count += ocean[i] ? 1 : 0;
  }
  const double ocean_pct = 100.0 * static_cast<double>(ocean_count) / static_cast<double>(ds.size());

  const auto out_path = std::filesystem::path(out_dir);

  // Write intermediate stage maps for diagnostic inspection
  if (ds.has_layer("elevation_primeval_m")) {
    const auto& primeval = ds.float_layer("elevation_primeval_m");
    write_png8_elevation_color((out_path / "stage_0_primeval_color.png").string(), primeval, w, h,
                               params.hydrology.sea_level_m);
    write_png8_preview((out_path / "stage_0_primeval_gray.png").string(), primeval, w, h);
  }
  if (ds.has_layer("tectonic_elevation_m")) {
    const auto& tectonic = ds.float_layer("tectonic_elevation_m");
    write_png8_elevation_color((out_path / "stage_1_tectonic_color.png").string(), tectonic, w, h,
                               params.hydrology.sea_level_m);
    write_png8_preview((out_path / "stage_1_tectonic_gray.png").string(), tectonic, w, h);
  }
  if (ds.has_layer("crust_type")) {
    write_png8_mask((out_path / "crust_type.png").string(), ds.u8_layer("crust_type"), w, h);
  }
  if (ds.has_layer("oceanic_age_myr")) {
    write_png8_preview((out_path / "oceanic_age.png").string(),
                       ds.float_layer("oceanic_age_myr"), w, h);
  }
  if (ds.has_layer("uplift_rate_m_per_yr")) {
    write_png8_preview((out_path / "uplift_rate.png").string(),
                       ds.float_layer("uplift_rate_m_per_yr"), w, h);
  }
  if (ds.has_layer("elevation_base_m")) {
    const auto& base = ds.float_layer("elevation_base_m");
    write_png8_elevation_color((out_path / "stage_2_base_color.png").string(), base, w, h,
                               params.hydrology.sea_level_m);
    write_png8_preview((out_path / "stage_2_base_gray.png").string(), base, w, h);
  }

  // Write final outputs
  if (!write_png16_gray((out_path / "elevation_eroded.png").string(), elev, w, h)) {
    return 2;
  }
  if (!write_png8_elevation_color((out_path / "elevation_color.png").string(), elev, w, h,
                                  params.hydrology.sea_level_m)) {
    return 2;
  }
  if (!write_png8_preview((out_path / "elevation_preview.png").string(), elev, w, h)) {
    return 2;
  }
  if (!write_png8_mask((out_path / "ocean_mask.png").string(), ocean, w, h)) {
    return 2;
  }
  if (!write_png8_mask((out_path / "river_mask.png").string(), river, w, h)) {
    return 2;
  }
  if (!write_png8_mask((out_path / "lake_mask.png").string(), lake, w, h)) {
    return 2;
  }
  if (also_write_pgm) {
    write_pgm16((out_path / "elevation_eroded.pgm").string(), elev, w, h);
    write_pgm8((out_path / "ocean_mask.pgm").string(), ocean, w, h);
    write_pgm8((out_path / "river_mask.pgm").string(), river, w, h);
    write_pgm8((out_path / "lake_mask.pgm").string(), lake, w, h);
  }

  std::cout << "seed: " << params.seed << "\n";
  std::cout << "size: " << w << "x" << h << "\n";
  std::cout << "elevation min/max (m): " << mn << " / " << mx << "\n";
  std::cout << "ocean coverage (%): " << ocean_pct << "\n";
  std::cout << "erosion params: levels=" << params.erosion.multigrid_levels
            << " iterations=" << params.erosion.fixed_point_iterations << " k=" << params.erosion.k
            << " time_years=" << params.erosion.time_years << " hillslope_k=" << params.erosion.hillslope_k
            << " thermal=" << (params.erosion.enable_thermal ? "on" : "off")
            << " hillslope=" << (params.erosion.enable_hillslope ? "on" : "off") << "\n";
  std::cout << "output: " << out_dir << "\n";

  write_metrics(std::cout, ds, params.hydrology.sea_level_m);
  std::ofstream metrics_file(out_path / "metrics.txt");
  if (metrics_file) {
    metrics_file << "seed: " << params.seed << "\n";
    metrics_file << "size: " << w << "x" << h << "\n";
    write_metrics(metrics_file, ds, params.hydrology.sea_level_m);
  }
  return 0;
}
