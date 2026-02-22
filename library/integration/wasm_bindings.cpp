#include <emscripten/bind.h>
#include <emscripten/val.h>
#include "mesh/ico_topology.hpp"
#include "mesh/ico_mesh.hpp"
#include "io/serialize.hpp"
#include "noise/noise_generator.hpp"
#include <cstring>

using namespace emscripten;

// ---------------------------------------------------------------------------
// Mesh geometry — positions and faces are cached inside IcoTopology,
// so typed_memory_view is safe (pointers remain valid for program lifetime).
// ---------------------------------------------------------------------------

val buildPositions(int k) {
    const IcoTopology& topo = ico_topology(k);
    const Vec3f* pos = topo.positions();
    return val(typed_memory_view(
        topo.num_cells() * 3,
        reinterpret_cast<const float*>(pos)
    ));
}

val buildFaces(int k) {
    const IcoTopology& topo = ico_topology(k);
    const int* f = topo.faces();
    return val(typed_memory_view(
        topo.num_faces_tri() * 3,
        reinterpret_cast<const unsigned int*>(f)
    ));
}

int getNumCells(int k) { return ico_topology(k).num_cells(); }
int getN(int k) { return ico_topology(k).N(); }

// ---------------------------------------------------------------------------
// FILD parsing — result kept alive in a static to avoid dangling views.
// ---------------------------------------------------------------------------

static IcoMesh<float> fild_result(0);

val parseFILD(uintptr_t buf_ptr, int buf_len, int k) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(buf_ptr);
    const IcoTopology& topo = ico_topology(k);
    fild_result = deserialize_field<float>(data, static_cast<size_t>(buf_len), &topo);
    return val(typed_memory_view(
        fild_result.num_cells(),
        fild_result.data()
    ));
}

// ---------------------------------------------------------------------------
// Noise generation — result kept alive in a static.
// ---------------------------------------------------------------------------

static IcoMesh<float> noise_result(0);

val generateNoise(int k, int seed, int octaves, float frequency,
                  float lacunarity, float gain, float warp_amplitude,
                  float ocean_fraction) {
    const IcoTopology& topo = ico_topology(k);
    NoiseParams params;
    params.seed = seed;
    params.octaves = octaves;
    params.frequency = frequency;
    params.lacunarity = lacunarity;
    params.gain = gain;
    params.warp_amplitude = warp_amplitude;
    params.ocean_fraction = ocean_fraction;
    noise_result = generate_noise<IcoMesh<float>>(&topo, params);
    return val(typed_memory_view(
        noise_result.num_cells(),
        noise_result.data()
    ));
}

// ---------------------------------------------------------------------------
// Embind module
// ---------------------------------------------------------------------------

EMSCRIPTEN_BINDINGS(worldengine) {
    function("buildPositions", &buildPositions);
    function("buildFaces", &buildFaces);
    function("getNumCells", &getNumCells);
    function("getN", &getN);
    function("parseFILD", &parseFILD);
    function("generateNoise", &generateNoise);
}
