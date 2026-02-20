#pragma once
#include <cmath>
#include <algorithm>

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3f operator+(Vec3f b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3f operator-(Vec3f b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(Vec3f b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3f cross(Vec3f b) const {
        return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3f normalized() const {
        float l = length();
        return {x / l, y / l, z / l};
    }
    float latitude() const { return std::asin(std::clamp(z / length(), -1.0f, 1.0f)); }
    float longitude() const { return std::atan2(y, x); }
};

struct Adjacency {
    int idx;
    float distance;
};
