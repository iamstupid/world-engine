#include "legacy/mesh/icosahedral_geodesic.hpp"
#include "legacy/mesh/field.hpp"
#include "mesh/ico_mesh.hpp"
#include "legacy/noise/noise_generator.hpp"
#include "noise/noise_generator.hpp"
#include "legacy/io/serialize.hpp"
#include "io/serialize.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

static int g_pass = 0, g_fail = 0;

static bool test_noise_equivalence(int k) {
    int N = 1 << k;
    printf("  k=%d (N=%d): noise equivalence\n", k, N);

    NoiseParams params;
    params.seed = 123;
    params.octaves = 4;
    params.frequency = 2.0f;
    params.warp_amplitude = 0.3f;
    params.ocean_fraction = 0.5f;

    // v1
    legacy::IcoMesh v1_mesh(N);
    auto v1_field = legacy::generate_noise(v1_mesh, params);

    // v2
    const IcoTopology& topo = ico_topology(k);
    auto v2_field = generate_noise<IcoMesh<float>>(&topo, params);

    if (v1_field.size() != v2_field.num_cells()) {
        printf("    FAIL: cell count v1=%d v2=%d\n", v1_field.size(), v2_field.num_cells());
        return false;
    }

    float max_diff = 0;
    int max_diff_idx = -1;
    for (int i = 0; i < v1_field.size(); i++) {
        float diff = std::abs(v1_field[i] - v2_field[i]);
        if (diff > max_diff) {
            max_diff = diff;
            max_diff_idx = i;
        }
    }

    if (max_diff > 1e-6f) {
        printf("    FAIL: max noise diff = %e at cell %d (v1=%f, v2=%f)\n",
               max_diff, max_diff_idx, v1_field[max_diff_idx], v2_field[max_diff_idx]);
        return false;
    }

    return true;
}

static bool test_serialize_equivalence(int k) {
    int N = 1 << k;
    printf("  k=%d (N=%d): serialization equivalence\n", k, N);

    NoiseParams params;
    params.seed = 42;

    legacy::IcoMesh v1_mesh(N);
    auto v1_field = legacy::generate_noise(v1_mesh, params);

    const IcoTopology& topo = ico_topology(k);
    auto v2_field = generate_noise<IcoMesh<float>>(&topo, params);

    auto v1_packet = legacy::serialize_field_packet(v1_field, N, 1);
    auto v2_packet = serialize_field(v2_field, 1);

    if (v1_packet.size() != v2_packet.size()) {
        printf("    FAIL: packet size v1=%zu v2=%zu\n", v1_packet.size(), v2_packet.size());
        return false;
    }

    if (std::memcmp(v1_packet.data(), v2_packet.data(), v1_packet.size()) != 0) {
        printf("    FAIL: packet contents differ\n");
        return false;
    }

    return true;
}

int main() {
    printf("v1/v2 Full Equivalence Test\n");
    printf("===========================\n");

    for (int k = 0; k <= 7; k++) {
        if (test_noise_equivalence(k)) {
            printf("    PASS\n");
            g_pass++;
        } else {
            g_fail++;
        }
    }

    for (int k = 0; k <= 7; k++) {
        if (test_serialize_equivalence(k)) {
            printf("    PASS\n");
            g_pass++;
        } else {
            g_fail++;
        }
    }

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
