#include "serialize.hpp"
#include <zlib.h>
#include <cstring>
#include <stdexcept>

std::vector<uint8_t> serialize_field_packet(const Field<IcoMesh>& field, int N, int compression) {
    // Raw float32 payload
    size_t raw_size = field.size() * sizeof(float);
    const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(field.ptr());

    std::vector<uint8_t> payload;

    if (compression == 0) {
        payload.assign(raw_data, raw_data + raw_size);
    } else if (compression == 1) {
        // gzip
        uLongf compressed_size = compressBound(raw_size);
        payload.resize(compressed_size);
        int ret = compress2(payload.data(), &compressed_size, raw_data, raw_size, Z_DEFAULT_COMPRESSION);
        if (ret != Z_OK) throw std::runtime_error("zlib compress failed");
        payload.resize(compressed_size);
    } else {
        throw std::runtime_error("Unsupported compression type");
    }

    // Build packet
    std::vector<uint8_t> packet(sizeof(FILDHeader) + payload.size());
    FILDHeader header{};
    header.magic = 0x46494C44;
    header.version = 1;
    header.N = N;
    header.num_cells = field.size();
    header.dtype = 0; // float32
    header.compression = compression;
    header.payload_size = payload.size();
    std::strncpy(header.name, field.name.c_str(), 15);
    header.name[15] = '\0';
    header.reserved = 0;

    std::memcpy(packet.data(), &header, sizeof(FILDHeader));
    std::memcpy(packet.data() + sizeof(FILDHeader), payload.data(), payload.size());

    return packet;
}

Field<IcoMesh> deserialize_field_packet(const uint8_t* data, size_t len, const IcoMesh* mesh) {
    if (len < sizeof(FILDHeader)) throw std::runtime_error("Packet too small");

    FILDHeader header;
    std::memcpy(&header, data, sizeof(FILDHeader));

    if (header.magic != 0x46494C44) throw std::runtime_error("Bad magic");
    if (header.version != 1) throw std::runtime_error("Bad version");

    Field<IcoMesh> field(mesh, std::string(header.name));

    const uint8_t* payload = data + sizeof(FILDHeader);
    size_t payload_size = header.payload_size;

    if (header.compression == 0) {
        if (payload_size != field.size() * sizeof(float))
            throw std::runtime_error("Payload size mismatch");
        std::memcpy(field.ptr(), payload, payload_size);
    } else if (header.compression == 1) {
        uLongf decompressed_size = field.size() * sizeof(float);
        int ret = uncompress(reinterpret_cast<Bytef*>(field.ptr()), &decompressed_size,
                             payload, payload_size);
        if (ret != Z_OK) throw std::runtime_error("zlib decompress failed");
    } else {
        throw std::runtime_error("Unsupported compression type");
    }

    return field;
}
