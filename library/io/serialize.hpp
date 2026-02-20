#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "mesh/field.hpp"
#include "mesh/icosahedral_geodesic.hpp"

// FILD packet header (48 bytes)
struct FILDHeader {
    uint32_t magic;          // 0x46494C44 ("FILD")
    uint32_t version;        // 1
    uint32_t N;
    uint32_t num_cells;
    uint32_t dtype;          // 0=float32, 1=uint8, 2=float16, 3=vec2_float32
    uint32_t compression;    // 0=none, 1=gzip, 2=delta+gzip
    uint32_t payload_size;
    char name[16];
    uint32_t reserved;
};
static_assert(sizeof(FILDHeader) == 48, "FILD header must be exactly 48 bytes");

std::vector<uint8_t> serialize_field_packet(const Field<IcoMesh>& field, int N, int compression);
Field<IcoMesh> deserialize_field_packet(const uint8_t* data, size_t len, const IcoMesh* mesh);
