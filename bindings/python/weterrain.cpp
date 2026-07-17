// pybind11 bindings: the terrain kernel exposed to the FastAPI server.
// Generation runs with the GIL released; the progress callback re-acquires
// it. All layer data crosses the boundary as raw bytes (numpy wraps them
// zero-copy-ish on the Python side).

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "world_engine/terrain/geodesic_grid.h"
#include "world_engine/terrain/procedural/graph_ops.h"
#include "world_engine/terrain/procedural/param_schema.h"
#include "world_engine/terrain/procedural/pipeline.h"
#include "world_engine/terrain/procedural/params.h"
#include "world_engine/terrain/procedural/stages/geodesic_physics_stage.h"
#include "world_engine/terrain/procedural/stages/tectonics_stage.h"

namespace py = pybind11;
namespace wt = world_engine::terrain;
namespace wtp = world_engine::terrain::procedural;

namespace {

struct CancelFlag {
  std::atomic<bool> flag{false};
};

py::bytes vec_bytes(const void* data, size_t bytes) {
  return py::bytes(static_cast<const char*>(data), bytes);
}

void apply_paint(wtp::PipelineParams& params, const py::object& paint) {
  if (paint.is_none()) {
    return;
  }
  for (const auto item : paint.cast<py::dict>()) {
    const auto name = item.first.cast<std::string>();
    const auto tup = item.second.cast<py::tuple>();
    wtp::PaintLayer layer;
    layer.width = tup[0].cast<int>();
    layer.height = tup[1].cast<int>();
    const auto raw = tup[2].cast<std::string>();
    layer.data.resize(raw.size() / sizeof(float));
    std::memcpy(layer.data.data(), raw.data(), layer.data.size() * sizeof(float));
    params.paint_layers[name] = std::move(layer);
  }
}

py::dict pack_dataset(const wt::TerrainDataset& ds);

py::dict generate(const wtp::PipelineParams& params_in, py::object progress,
                  std::shared_ptr<CancelFlag> cancel, py::object paint) {
  wtp::PipelineParams params = params_in;
  apply_paint(params, paint);
  wtp::ProgressFn progress_fn;
  if (!progress.is_none()) {
    auto cb = std::make_shared<py::function>(progress.cast<py::function>());
    progress_fn = [cb](const std::string& stage, int idx, int total, double phase) {
      py::gil_scoped_acquire gil;
      try {
        (*cb)(stage, idx, total, phase);
      } catch (py::error_already_set& e) {
        e.discard_as_unraisable("weterrain progress callback");
      }
    };
  }

  wtp::Pipeline pipeline;
  wt::TerrainDataset ds;
  {
    py::gil_scoped_release release;
    ds = pipeline.run(params, progress_fn,
                      cancel ? &cancel->flag : nullptr);
  }

  return pack_dataset(ds);
}

py::dict pack_dataset(const wt::TerrainDataset& ds) {
  py::dict out;
  out["width"] = ds.domain().width();
  out["height"] = ds.domain().height();
  out["seed"] = ds.seed();
  out["hash"] = ds.deterministic_hash();

  py::dict layers;
  for (const auto& name : ds.list_layers()) {
    py::dict entry;
    switch (ds.layer_type(name)) {
      case wt::LayerDataType::kFloat32: {
        const auto& v = ds.float_layer(name);
        entry["dtype"] = "f32";
        entry["data"] = vec_bytes(v.data(), v.size() * sizeof(float));
        break;
      }
      case wt::LayerDataType::kUInt8: {
        const auto& v = ds.u8_layer(name);
        entry["dtype"] = "u8";
        entry["data"] = vec_bytes(v.data(), v.size());
        break;
      }
      case wt::LayerDataType::kInt32: {
        const auto& v = ds.i32_layer(name);
        entry["dtype"] = "i32";
        entry["data"] = vec_bytes(v.data(), v.size() * sizeof(int32_t));
        break;
      }
    }
    layers[py::str(name)] = entry;
  }
  out["layers"] = layers;

  py::dict cell_layers;
  for (const auto& name : ds.list_cell_layers()) {
    const auto& cl = ds.cell_layer(name);
    py::dict entry;
    entry["frequency"] = cl.frequency;
    entry["dtype"] = "f32";
    entry["data"] = vec_bytes(cl.data.data(), cl.data.size() * sizeof(float));
    cell_layers[py::str(name)] = entry;
  }
  out["cell_layers"] = cell_layers;
  return out;
}

py::dict geodesic_graph(int frequency) {
  const wt::GeodesicGrid grid(frequency);
  const int n = grid.cell_count();
  std::vector<int32_t> neighbors(static_cast<size_t>(n) * 6, -1);
  std::vector<int8_t> degree(n, 0);
  std::vector<double> centers(static_cast<size_t>(n) * 3);
  std::vector<double> areas(n);
  std::array<int, 6> nb{};
  for (int i = 0; i < n; ++i) {
    const int m = grid.neighbors(i, nb);
    degree[i] = static_cast<int8_t>(m);
    for (int k = 0; k < m; ++k) {
      neighbors[static_cast<size_t>(i) * 6 + k] = nb[k];
    }
    const auto& c = grid.cell_center(i);
    centers[static_cast<size_t>(i) * 3 + 0] = c.x;
    centers[static_cast<size_t>(i) * 3 + 1] = c.y;
    centers[static_cast<size_t>(i) * 3 + 2] = c.z;
    areas[i] = grid.cell_area_sr(i);
  }
  py::dict out;
  out["cell_count"] = n;
  out["frequency"] = frequency;
  out["neighbors_i32"] = vec_bytes(neighbors.data(), neighbors.size() * 4);
  out["degree_i8"] = vec_bytes(degree.data(), degree.size());
  out["centers_f64"] = vec_bytes(centers.data(), centers.size() * 8);
  out["areas_f64"] = vec_bytes(areas.data(), areas.size() * 8);
  return out;
}

// ---- graph operators (DAG pipeline, docs/PIPELINE_DAG_DESIGN.md) ----

py::bytes vecf_bytes(const std::vector<float>& v) {
  return py::bytes(reinterpret_cast<const char*>(v.data()),
                   v.size() * sizeof(float));
}

std::vector<float> bytes_vecf(const py::bytes& b) {
  const auto raw = static_cast<std::string>(b);
  std::vector<float> v(raw.size() / sizeof(float));
  std::memcpy(v.data(), raw.data(), raw.size());
  return v;
}

py::bytes op_noise_cells(int frequency, int seed, double base_frequency,
                         int octaves, double lacunarity, double gain,
                         double amplitude, bool ridged) {
  std::vector<float> out;
  {
    py::gil_scoped_release release;
    out = wtp::noise_cells(frequency, seed, base_frequency, octaves,
                           lacunarity, gain, amplitude, ridged);
  }
  return vecf_bytes(out);
}

py::bytes op_resample_cells(int src_freq, py::bytes src, int dst_freq) {
  const auto v = bytes_vecf(src);
  std::vector<float> out;
  {
    py::gil_scoped_release release;
    out = wtp::resample_cells(src_freq, v, dst_freq);
  }
  return vecf_bytes(out);
}

py::bytes op_rasterize_cells(int freq, py::bytes cells, int width, int height) {
  const auto v = bytes_vecf(cells);
  std::vector<float> out;
  {
    py::gil_scoped_release release;
    out = wtp::rasterize_cells(freq, v, width, height);
  }
  return vecf_bytes(out);
}

py::dict op_tectonics_cells(const wtp::PipelineParams& params_in,
                            py::object paint) {
  wtp::PipelineParams params = params_in;
  apply_paint(params, paint);
  wt::TerrainDataset ds(
      wt::TerrainDomain(64, 32, params.radius_m));
  ds.set_seed(params.seed);
  {
    py::gil_scoped_release release;
    wtp::stages::run_tectonics_stage(params, ds);
  }
  return pack_dataset(ds);
}

py::dict op_physics_cells(const wtp::PipelineParams& params_in,
                          int z0_freq, py::bytes z0, int up_freq,
                          py::object uplift, int width, int height,
                          py::object paint) {
  wtp::PipelineParams params = params_in;
  apply_paint(params, paint);
  const auto z0v = bytes_vecf(z0);
  std::vector<float> upv;
  const bool has_up = !uplift.is_none();
  if (has_up) {
    upv = bytes_vecf(uplift.cast<py::bytes>());
  }
  wt::TerrainDataset ds(wt::TerrainDomain(width, height, params.radius_m));
  ds.set_seed(params.seed);
  {
    py::gil_scoped_release release;
    wtp::stages::run_geodesic_physics_stage_fields(
        params, ds, z0_freq, &z0v, up_freq, has_up ? &upv : nullptr);
  }
  return pack_dataset(ds);
}

// Atlas pixel -> cell id map (row-major, height 2F, width 5F); the two pole
// cells are not part of the atlas.
py::bytes atlas_map(int frequency) {
  const wt::GeodesicGrid grid(frequency);
  const int f = frequency;
  std::vector<int32_t> map(static_cast<size_t>(10) * f * f, -1);
  const int w = 5 * f;
  for (int r = 0; r < 10; ++r) {
    const int bx = (r % 5) * f;
    const int by = (r / 5) * f;
    for (int i = 0; i < f; ++i) {
      for (int j = 0; j < f; ++j) {
        map[static_cast<size_t>(by + i) * w + bx + j] = grid.rhombus_to_cell(r, i, j);
      }
    }
  }
  return vec_bytes(map.data(), map.size() * 4);
}

}  // namespace

PYBIND11_MODULE(weterrain, m) {
  m.doc() = "WorldEngine terrain kernel bindings";

  py::class_<wtp::PipelineParams>(m, "Params")
      .def(py::init<>())
      .def("set", [](wtp::PipelineParams& p, const std::string& key, double value) {
        if (!wtp::set_param(p, key, value)) {
          throw py::key_error("unknown param: " + key);
        }
      })
      .def("get", [](const wtp::PipelineParams& p, const std::string& key) {
        double out = 0.0;
        if (!wtp::get_param(p, key, out)) {
          throw py::key_error("unknown param: " + key);
        }
        return out;
      })
      .def_static("keys", []() { return wtp::param_keys(); });

  py::class_<CancelFlag, std::shared_ptr<CancelFlag>>(m, "CancelFlag")
      .def(py::init<>())
      .def("set", [](CancelFlag& c) { c.flag.store(true); })
      .def("is_set", [](const CancelFlag& c) { return c.flag.load(); });

  m.def("schema_json", &wtp::params_schema_json);
  m.def("generate", &generate, py::arg("params"), py::arg("progress") = py::none(),
        py::arg("cancel") = std::shared_ptr<CancelFlag>{},
        py::arg("paint") = py::none());
  m.def("geodesic_graph", &geodesic_graph, py::arg("frequency"));
  m.def("atlas_map", &atlas_map, py::arg("frequency"));
  m.def("noise_cells", &op_noise_cells, py::arg("frequency"), py::arg("seed"),
        py::arg("base_frequency"), py::arg("octaves"), py::arg("lacunarity"),
        py::arg("gain"), py::arg("amplitude"), py::arg("ridged") = false);
  m.def("resample_cells", &op_resample_cells, py::arg("src_freq"),
        py::arg("src"), py::arg("dst_freq"));
  m.def("rasterize_cells", &op_rasterize_cells, py::arg("freq"),
        py::arg("cells"), py::arg("width"), py::arg("height"));
  m.def("tectonics_cells", &op_tectonics_cells, py::arg("params"),
        py::arg("paint") = py::none());
  m.def("physics_cells", &op_physics_cells, py::arg("params"),
        py::arg("z0_freq"), py::arg("z0"), py::arg("up_freq") = 0,
        py::arg("uplift") = py::none(), py::arg("width") = 512,
        py::arg("height") = 256, py::arg("paint") = py::none());
}
