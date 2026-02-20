#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include "mesh/icosahedral_geodesic.hpp"
#include "mesh/field.hpp"
#include "noise/noise_generator.hpp"
#include "io/serialize.hpp"

namespace py = pybind11;

PYBIND11_MODULE(worldengine_py, m) {
    m.doc() = "WorldEngine: Physically-based world generation";

    py::class_<IcoMesh>(m, "IcoMesh")
        .def(py::init<int>(), py::arg("N"))
        .def("num_cells", &IcoMesh::num_cells)
        .def("N", &IcoMesh::N)
        .def("num_rows", &IcoMesh::num_rows)
        .def("positions_numpy", [](const IcoMesh& m) {
            const auto& pos = m.positions();
            return py::array_t<float>(
                {(int)pos.size(), 3},
                {3 * (int)sizeof(float), (int)sizeof(float)},
                reinterpret_cast<const float*>(pos.data())
            );
        })
        .def("faces_numpy", [](const IcoMesh& m) {
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

    py::class_<Field<IcoMesh>>(m, "IcoField")
        .def("to_numpy", [](Field<IcoMesh>& f) {
            return py::array_t<float>(
                {f.size()}, {(int)sizeof(float)}, f.ptr(), py::cast(f)
            );
        })
        .def("size", &Field<IcoMesh>::size)
        .def("min_val", &Field<IcoMesh>::min_val)
        .def("max_val", &Field<IcoMesh>::max_val)
        .def("mean_val", &Field<IcoMesh>::mean_val)
        .def_readonly("name", &Field<IcoMesh>::name);

    m.def("generate_noise_ico", [](int N, py::dict params_dict) {
        IcoMesh mesh(N);
        NoiseParams np;
        if (params_dict.contains("seed")) np.seed = params_dict["seed"].cast<int>();
        if (params_dict.contains("octaves")) np.octaves = params_dict["octaves"].cast<int>();
        if (params_dict.contains("frequency")) np.frequency = params_dict["frequency"].cast<float>();
        if (params_dict.contains("lacunarity")) np.lacunarity = params_dict["lacunarity"].cast<float>();
        if (params_dict.contains("gain")) np.gain = params_dict["gain"].cast<float>();
        if (params_dict.contains("warp_amplitude")) np.warp_amplitude = params_dict["warp_amplitude"].cast<float>();
        if (params_dict.contains("ocean_fraction")) np.ocean_fraction = params_dict["ocean_fraction"].cast<float>();

        py::gil_scoped_release release;
        auto field = generate_noise(mesh, np);
        py::gil_scoped_acquire acquire;
        return field;
    }, py::arg("N"), py::arg("params"));

    m.def("serialize_field", [](const Field<IcoMesh>& f, int N, int compression) {
        auto bytes = serialize_field_packet(f, N, compression);
        return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }, py::arg("field"), py::arg("N"), py::arg("compression") = 1);
}
