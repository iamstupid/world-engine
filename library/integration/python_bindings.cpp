#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include "legacy/mesh/icosahedral_geodesic.hpp"
#include "legacy/mesh/field.hpp"
#include "legacy/noise/noise_generator.hpp"
#include "legacy/io/serialize.hpp"
#include "mesh/ico_topology.hpp"
#include "mesh/ico_mesh.hpp"
#include "noise/noise_generator.hpp"
#include "io/serialize.hpp"

namespace py = pybind11;

PYBIND11_MODULE(worldengine_py, m) {
    m.doc() = "WorldEngine: Physically-based world generation";

    py::class_<legacy::IcoMesh>(m, "IcoMesh")
        .def(py::init<int>(), py::arg("N"))
        .def("num_cells", &legacy::IcoMesh::num_cells)
        .def("N", &legacy::IcoMesh::N)
        .def("num_rows", &legacy::IcoMesh::num_rows)
        .def("positions_numpy", [](const legacy::IcoMesh& m) {
            const auto& pos = m.positions();
            return py::array_t<float>(
                {(int)pos.size(), 3},
                {3 * (int)sizeof(float), (int)sizeof(float)},
                reinterpret_cast<const float*>(pos.data())
            );
        })
        .def("faces_numpy", [](const legacy::IcoMesh& m) {
            const auto& faces = m.faces();
            return py::array_t<int>(
                {(int)faces.size(), 3},
                {3 * (int)sizeof(int), (int)sizeof(int)},
                reinterpret_cast<const int*>(faces.data())
            );
        });

    py::class_<NoiseParams>(m, "NoiseParams")
        .def(py::init<>())
        .def_readwrite("seed", &NoiseParams::seed)
        .def_readwrite("octaves", &NoiseParams::octaves)
        .def_readwrite("frequency", &NoiseParams::frequency)
        .def_readwrite("lacunarity", &NoiseParams::lacunarity)
        .def_readwrite("gain", &NoiseParams::gain)
        .def_readwrite("warp_amplitude", &NoiseParams::warp_amplitude)
        .def_readwrite("ocean_fraction", &NoiseParams::ocean_fraction);

    py::class_<legacy::Field<legacy::IcoMesh>>(m, "IcoField")
        .def("to_numpy", [](legacy::Field<legacy::IcoMesh>& f) {
            return py::array_t<float>(
                {f.size()}, {(int)sizeof(float)}, f.ptr(), py::cast(f)
            );
        })
        .def("size", &legacy::Field<legacy::IcoMesh>::size)
        .def("min_val", &legacy::Field<legacy::IcoMesh>::min_val)
        .def("max_val", &legacy::Field<legacy::IcoMesh>::max_val)
        .def("mean_val", &legacy::Field<legacy::IcoMesh>::mean_val)
        .def_readonly("name", &legacy::Field<legacy::IcoMesh>::name);

    m.def("generate_noise_ico", [](int N, py::dict params_dict) {
        legacy::IcoMesh mesh(N);
        NoiseParams np;
        if (params_dict.contains("seed")) np.seed = params_dict["seed"].cast<int>();
        if (params_dict.contains("octaves")) np.octaves = params_dict["octaves"].cast<int>();
        if (params_dict.contains("frequency")) np.frequency = params_dict["frequency"].cast<float>();
        if (params_dict.contains("lacunarity")) np.lacunarity = params_dict["lacunarity"].cast<float>();
        if (params_dict.contains("gain")) np.gain = params_dict["gain"].cast<float>();
        if (params_dict.contains("warp_amplitude")) np.warp_amplitude = params_dict["warp_amplitude"].cast<float>();
        if (params_dict.contains("ocean_fraction")) np.ocean_fraction = params_dict["ocean_fraction"].cast<float>();

        py::gil_scoped_release release;
        auto field = legacy::generate_noise(mesh, np);
        py::gil_scoped_acquire acquire;
        return field;
    }, py::arg("N"), py::arg("params"));

    m.def("serialize_field", [](const legacy::Field<legacy::IcoMesh>& f, int N, int compression) {
        auto bytes = legacy::serialize_field_packet(f, N, compression);
        return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }, py::arg("field"), py::arg("N"), py::arg("compression") = 1);

    // --- v2 API ---

    py::class_<IcoTopology>(m, "IcoTopology")
        .def_static("get", [](int k) -> const IcoTopology& {
            return ico_topology(k);
        }, py::return_value_policy::reference, py::arg("k"))
        .def("k", &IcoTopology::k)
        .def("N", &IcoTopology::N)
        .def("num_cells", &IcoTopology::num_cells)
        .def("num_rows", &IcoTopology::num_rows)
        .def("positions_numpy", [](const IcoTopology& t) {
            const Vec3f* pos = t.positions();
            return py::array_t<float>(
                {t.num_cells(), 3},
                {3 * (int)sizeof(float), (int)sizeof(float)},
                reinterpret_cast<const float*>(pos)
            );
        })
        .def("faces_numpy", [](const IcoTopology& t) {
            const int* f = t.faces();
            int nf = t.num_faces_tri();
            return py::array_t<int>(
                {nf, 3},
                {3 * (int)sizeof(int), (int)sizeof(int)},
                f
            );
        });

    py::class_<IcoMesh<float>>(m, "IcoMeshF")
        .def(py::init<int, std::string>(), py::arg("k"), py::arg("name") = "")
        .def("num_cells", &IcoMesh<float>::num_cells)
        .def("N", &IcoMesh<float>::N)
        .def("k", &IcoMesh<float>::k)
        .def("name", &IcoMesh<float>::name)
        .def("min_val", &IcoMesh<float>::min_val)
        .def("max_val", &IcoMesh<float>::max_val)
        .def("mean_val", &IcoMesh<float>::mean_val)
        .def("to_numpy", [](IcoMesh<float>& m) {
            return py::array_t<float>(
                {m.num_cells()}, {(int)sizeof(float)}, m.data(), py::cast(m)
            );
        })
        .def("shift", &IcoMesh<float>::shift)
        .def("rescale", &IcoMesh<float>::rescale);

    m.def("generate_noise", [](int k, py::dict params_dict) {
        const IcoTopology& topo = ico_topology(k);
        NoiseParams np;
        if (params_dict.contains("seed")) np.seed = params_dict["seed"].cast<int>();
        if (params_dict.contains("octaves")) np.octaves = params_dict["octaves"].cast<int>();
        if (params_dict.contains("frequency")) np.frequency = params_dict["frequency"].cast<float>();
        if (params_dict.contains("lacunarity")) np.lacunarity = params_dict["lacunarity"].cast<float>();
        if (params_dict.contains("gain")) np.gain = params_dict["gain"].cast<float>();
        if (params_dict.contains("warp_amplitude")) np.warp_amplitude = params_dict["warp_amplitude"].cast<float>();
        if (params_dict.contains("ocean_fraction")) np.ocean_fraction = params_dict["ocean_fraction"].cast<float>();

        py::gil_scoped_release release;
        auto mesh = generate_noise<IcoMesh<float>>(&topo, np);
        py::gil_scoped_acquire acquire;
        return mesh;
    }, py::arg("k"), py::arg("params"));

    m.def("serialize_v2", [](const IcoMesh<float>& mesh, int compression) {
        auto bytes = serialize_field(mesh, compression);
        return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }, py::arg("mesh"), py::arg("compression") = 1);
}
