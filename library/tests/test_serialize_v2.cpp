#include "io/serialize.hpp"
#include "legacy/io/serialize.hpp"
#include "legacy/mesh/field.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

static int g_pass = 0, g_fail = 0;

static bool test_float32_roundtrip() {
    printf("  float32 roundtrip\n");
    const IcoTopology& t = ico_topology(3); // k=3, N=8, 642 cells
    IcoMesh<float> mesh(&t, "elevation");

    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i) * 0.001f - 0.3f;

    // Serialize (compressed)
    auto packet = serialize_field(mesh, 1);

    // Deserialize
    auto restored = deserialize_field<float>(packet.data(), packet.size(), &t);

    if (restored.num_cells() != mesh.num_cells()) {
        printf("    FAIL: num_cells %d != %d\n", restored.num_cells(), mesh.num_cells());
        return false;
    }
    if (restored.name() != mesh.name()) {
        printf("    FAIL: name '%s' != '%s'\n", restored.name().c_str(), mesh.name().c_str());
        return false;
    }
    for (int i = 0; i < mesh.num_cells(); i++) {
        if (restored[i] != mesh[i]) {
            printf("    FAIL: data mismatch at %d: %f != %f\n", i, restored[i], mesh[i]);
            return false;
        }
    }

    return true;
}

static bool test_float32_uncompressed() {
    printf("  float32 uncompressed\n");
    const IcoTopology& t = ico_topology(2);
    IcoMesh<float> mesh(&t, "test_unc");

    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i);

    auto packet = serialize_field(mesh, 0);
    auto restored = deserialize_field<float>(packet.data(), packet.size(), &t);

    for (int i = 0; i < mesh.num_cells(); i++) {
        if (restored[i] != mesh[i]) {
            printf("    FAIL: data mismatch at %d\n", i);
            return false;
        }
    }

    return true;
}

static bool test_uint8_roundtrip() {
    printf("  uint8 roundtrip\n");
    const IcoTopology& t = ico_topology(3);
    IcoMesh<uint8_t> mesh(&t, "biome");

    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<uint8_t>(i % 256);

    auto packet = serialize_field(mesh, 1);
    auto restored = deserialize_field<uint8_t>(packet.data(), packet.size(), &t);

    for (int i = 0; i < mesh.num_cells(); i++) {
        if (restored[i] != mesh[i]) {
            printf("    FAIL: data mismatch at %d: %d != %d\n", i, restored[i], mesh[i]);
            return false;
        }
    }

    return true;
}

static bool test_v1_v2_cross_compat() {
    printf("  v1/v2 float32 cross-compatibility\n");
    int N = 8; // k=3
    const IcoTopology& topo = ico_topology(3);

    // Create identical data via v1 and v2
    legacy::IcoMesh v1_mesh(N);
    legacy::Field<legacy::IcoMesh> v1_field(&v1_mesh, "elevation");
    IcoMesh<float> v2_mesh(&topo, "elevation");

    for (int i = 0; i < v1_mesh.num_cells(); i++) {
        float val = static_cast<float>(i) * 0.01f;
        v1_field[i] = val;
        v2_mesh[i] = val;
    }

    // Serialize both
    auto v1_packet = legacy::serialize_field_packet(v1_field, N, 1);
    auto v2_packet = serialize_field(v2_mesh, 1);

    // Packets should be byte-identical
    if (v1_packet.size() != v2_packet.size()) {
        printf("    FAIL: packet size v1=%zu v2=%zu\n", v1_packet.size(), v2_packet.size());
        return false;
    }
    if (std::memcmp(v1_packet.data(), v2_packet.data(), v1_packet.size()) != 0) {
        printf("    FAIL: packet contents differ\n");

        // Show header differences
        FILDHeader h1, h2;
        std::memcpy(&h1, v1_packet.data(), sizeof(FILDHeader));
        std::memcpy(&h2, v2_packet.data(), sizeof(FILDHeader));
        if (h1.magic != h2.magic) printf("      magic: %u vs %u\n", h1.magic, h2.magic);
        if (h1.N != h2.N) printf("      N: %u vs %u\n", h1.N, h2.N);
        if (h1.num_cells != h2.num_cells) printf("      num_cells: %u vs %u\n", h1.num_cells, h2.num_cells);
        if (h1.dtype != h2.dtype) printf("      dtype: %u vs %u\n", h1.dtype, h2.dtype);
        if (h1.payload_size != h2.payload_size) printf("      payload_size: %u vs %u\n", h1.payload_size, h2.payload_size);

        return false;
    }

    // Deserialize v2 packet with v1 and verify
    auto v1_restored = legacy::deserialize_field_packet(v2_packet.data(), v2_packet.size(), &v1_mesh);
    for (int i = 0; i < v1_mesh.num_cells(); i++) {
        if (v1_restored[i] != v1_field[i]) {
            printf("    FAIL: v1 restore mismatch at %d\n", i);
            return false;
        }
    }

    return true;
}

int main() {
    printf("Serialization v2 test\n");
    printf("=====================\n");

    auto run = [](bool (*fn)()) {
        if (fn()) { printf("    PASS\n"); g_pass++; }
        else { g_fail++; }
    };

    run(test_float32_roundtrip);
    run(test_float32_uncompressed);
    run(test_uint8_roundtrip);
    run(test_v1_v2_cross_compat);

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
