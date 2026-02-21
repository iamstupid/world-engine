#include "noise_generator.hpp"
#include <algorithm>
#include <cmath>

namespace legacy {

template<typename Mesh>
Field<Mesh> generate_noise(const Mesh& mesh, const NoiseParams& params) {
    Field<Mesh> field(&mesh, "elevation");

    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.SetSeed(params.seed);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(params.octaves);
    noise.SetFractalLacunarity(params.lacunarity);
    noise.SetFractalGain(params.gain);
    noise.SetFrequency(params.frequency);

    FastNoiseLite warp;
    if (params.warp_amplitude > 0) {
        warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        warp.SetDomainWarpAmp(params.warp_amplitude);
        warp.SetSeed(params.seed + 1);
        warp.SetFrequency(params.frequency * 0.5f);
    }

    for (int i = 0; i < mesh.num_cells(); i++) {
        Vec3f p = mesh.cell_position(i);
        float x = p.x, y = p.y, z = p.z;
        if (params.warp_amplitude > 0) {
            warp.DomainWarp(x, y, z);
        }
        field[i] = noise.GetNoise(x, y, z);
    }

    // Shift so ocean_fraction quantile lands at zero
    float cutoff = field.quantile(params.ocean_fraction);
    field.shift(-cutoff);

    // Rescale so range is [-1, 1]
    float peak = field.max_val();
    float abyss = field.min_val();
    float scale = std::max(std::abs(peak), std::abs(abyss));
    if (scale > 1e-6f) {
        for (int i = 0; i < field.size(); i++)
            field[i] /= scale;
    }

    return field;
}

// Explicit template instantiation
template Field<IcoMesh> generate_noise(const IcoMesh& mesh, const NoiseParams& params);

} // namespace legacy
