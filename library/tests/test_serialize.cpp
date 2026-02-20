#include <cstdio>
#include <cmath>
#include <cassert>
#include <cstring>
#include "mesh/icosahedral_geodesic.hpp"
#include "noise/noise_generator.hpp"
#include "io/serialize.hpp"

#define TEST(name) printf("  TEST: %s ... ", #name);
#define PASS() printf("PASSED\n");

void test_roundtrip_uncompressed() {
    TEST(roundtrip_uncompressed);
    IcoMesh mesh(10);
    NoiseParams params;
    params.seed = 42;
    Field<IcoMesh> field = generate_noise(mesh, params);

    auto packet = serialize_field_packet(field, 10, 0);

    Field<IcoMesh> restored = deserialize_field_packet(packet.data(), packet.size(), &mesh);

    assert(restored.size() == field.size());
    for (int i = 0; i < field.size(); i++) {
        assert(restored[i] == field[i] && "Roundtrip mismatch");
    }
    printf("(packet=%zu bytes) ", packet.size());
    PASS();
}

void test_roundtrip_gzip() {
    TEST(roundtrip_gzip);
    IcoMesh mesh(10);
    NoiseParams params;
    params.seed = 42;
    Field<IcoMesh> field = generate_noise(mesh, params);

    auto uncompressed = serialize_field_packet(field, 10, 0);
    auto compressed = serialize_field_packet(field, 10, 1);

    printf("(raw=%zu gzip=%zu ratio=%.2f) ",
           uncompressed.size(), compressed.size(),
           (float)compressed.size() / uncompressed.size());

    assert(compressed.size() < uncompressed.size() && "Gzip should be smaller");

    Field<IcoMesh> restored = deserialize_field_packet(compressed.data(), compressed.size(), &mesh);

    assert(restored.size() == field.size());
    for (int i = 0; i < field.size(); i++) {
        assert(restored[i] == field[i] && "Gzip roundtrip mismatch");
    }
    PASS();
}

void test_header_fields() {
    TEST(header_fields);
    IcoMesh mesh(10);
    Field<IcoMesh> field(&mesh, "elevation");
    for (int i = 0; i < field.size(); i++) field[i] = (float)i / field.size();

    auto packet = serialize_field_packet(field, 10, 0);

    FILDHeader header;
    memcpy(&header, packet.data(), sizeof(FILDHeader));

    assert(header.magic == 0x46494C44 && "Bad magic");
    assert(header.version == 1 && "Bad version");
    assert(header.N == 10 && "Bad N");
    assert(header.num_cells == (uint32_t)mesh.num_cells() && "Bad num_cells");
    assert(header.dtype == 0 && "Bad dtype");
    assert(header.compression == 0 && "Bad compression");
    assert(header.payload_size == (uint32_t)(mesh.num_cells() * sizeof(float)));
    assert(std::string(header.name) == "elevation" && "Bad name");
    PASS();
}

int main() {
    printf("=== Serialization Tests ===\n");

    test_roundtrip_uncompressed();
    test_roundtrip_gzip();
    test_header_fields();

    printf("\nAll serialization tests PASSED!\n");
    return 0;
}
