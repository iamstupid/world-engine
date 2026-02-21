#pragma once

struct NoiseParams {
    int seed = 42;
    int octaves = 6;
    float frequency = 1.5f;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float warp_amplitude = 0.0f;
    float ocean_fraction = 0.55f;
};
