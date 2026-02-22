#include "serialize.hpp"
#include <zlib.h>
#include <cstring>
#include <stdexcept>

template<typename T>
std::vector<uint8_t> serialize_field(const IcoMesh<T>& mesh, int compression) {
    size_t raw_size = mesh.num_cells() * sizeof(T);
    const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(mesh.data());

    std::vector<uint8_t> payload;

    if (compression == 0) {
        payload.assign(raw_data, raw_data + raw_size);
    } else if (compression == 1) {
        uLongf compressed_size = compressBound(raw_size);
        payload.resize(compressed_size);
        int ret = compress2(payload.data(), &compressed_size, raw_data, raw_size, Z_DEFAULT_COMPRESSION);
        if (ret != Z_OK) throw std::runtime_error("zlib compress failed");
        payload.resize(compressed_size);
    } else {
        throw std::runtime_error("Unsupported compression type");
    }

    std::vector<uint8_t> packet(sizeof(FILDHeader) + payload.size());
    FILDHeader header{};
    header.magic = 0x46494C44;
    header.version = 1;
    header.N = mesh.N();
    header.num_cells = mesh.num_cells();
    header.dtype = DTypeTraits<T>::code;
    header.compression = compression;
    header.payload_size = payload.size();
    std::strncpy(header.name, mesh.name().c_str(), 15);
    header.name[15] = '\0';
    header.reserved = 0;

    std::memcpy(packet.data(), &header, sizeof(FILDHeader));
    std::memcpy(packet.data() + sizeof(FILDHeader), payload.data(), payload.size());

    return packet;
}

template<typename T>
IcoMesh<T> deserialize_field(const uint8_t* data, size_t len, const IcoTopology* topo) {
    if (len < sizeof(FILDHeader)) throw std::runtime_error("Packet too small");

    FILDHeader header;
    std::memcpy(&header, data, sizeof(FILDHeader));

    if (header.magic != 0x46494C44) throw std::runtime_error("Bad magic");
    if (header.version != 1) throw std::runtime_error("Bad version");
    if (header.dtype != DTypeTraits<T>::code) throw std::runtime_error("dtype mismatch");

    IcoMesh<T> mesh(topo, std::string(header.name));

    const uint8_t* payload = data + sizeof(FILDHeader);
    size_t payload_size = header.payload_size;

    if (header.compression == 0) {
        if (payload_size != static_cast<size_t>(mesh.num_cells()) * sizeof(T))
            throw std::runtime_error("Payload size mismatch");
        std::memcpy(mesh.data(), payload, payload_size);
    } else if (header.compression == 1) {
        uLongf decompressed_size = mesh.num_cells() * sizeof(T);
        int ret = uncompress(reinterpret_cast<Bytef*>(mesh.data()), &decompressed_size,
                             payload, payload_size);
        if (ret != Z_OK) throw std::runtime_error("zlib decompress failed");
    } else {
        throw std::runtime_error("Unsupported compression type");
    }

    return mesh;
}

// Explicit instantiations
template std::vector<uint8_t> serialize_field<float>(const IcoMesh<float>&, int);
template IcoMesh<float> deserialize_field<float>(const uint8_t*, size_t, const IcoTopology*);
template std::vector<uint8_t> serialize_field<uint8_t>(const IcoMesh<uint8_t>&, int);
template IcoMesh<uint8_t> deserialize_field<uint8_t>(const uint8_t*, size_t, const IcoTopology*);
template std::vector<uint8_t> serialize_field<uint16_t>(const IcoMesh<uint16_t>&, int);
template IcoMesh<uint16_t> deserialize_field<uint16_t>(const uint8_t*, size_t, const IcoTopology*);
