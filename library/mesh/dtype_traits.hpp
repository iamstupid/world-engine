#pragma once

#include <cstdint>

/// Compile-time type mapping for serialization.
/// Specialize for custom types to enable serialize_v2 / deserialize_v2.
template<typename T> struct DTypeTraits;

template<> struct DTypeTraits<float> {
    static constexpr uint32_t code = 0;
    static constexpr const char* name = "float32";
};

template<> struct DTypeTraits<uint8_t> {
    static constexpr uint32_t code = 1;
    static constexpr const char* name = "uint8";
};

template<> struct DTypeTraits<int16_t> {
    static constexpr uint32_t code = 4;
    static constexpr const char* name = "int16";
};

template<> struct DTypeTraits<double> {
    static constexpr uint32_t code = 5;
    static constexpr const char* name = "float64";
};

template<> struct DTypeTraits<int32_t> {
    static constexpr uint32_t code = 6;
    static constexpr const char* name = "int32";
};

template<> struct DTypeTraits<uint16_t> {
    static constexpr uint32_t code = 7;
    static constexpr const char* name = "uint16";
};
