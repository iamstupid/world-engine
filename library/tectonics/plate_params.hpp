#pragma once

struct PlateAssignParams {
    int num_plates = 12;
    int seed = 42;
    float ocean_bias = 1.5f;       // >1 = plate boundaries prefer ocean cells
    int weight_octaves = 4;
    float weight_frequency = 1.0f;
    float weight_lacunarity = 2.0f;
    float weight_gain = 0.5f;
};
