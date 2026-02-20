#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <numeric>

template<typename Mesh>
struct Field {
    const Mesh* mesh;
    std::vector<float> data;
    std::string name;

    Field() : mesh(nullptr) {}
    Field(const Mesh* m, const std::string& name)
        : mesh(m), data(m->num_cells(), 0.0f), name(name) {}

    float& operator[](int idx) { return data[idx]; }
    const float& operator[](int idx) const { return data[idx]; }

    int size() const { return static_cast<int>(data.size()); }
    float* ptr() { return data.data(); }
    const float* ptr() const { return data.data(); }

    float min_val() const {
        return *std::min_element(data.begin(), data.end());
    }

    float max_val() const {
        return *std::max_element(data.begin(), data.end());
    }

    float mean_val() const {
        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        return static_cast<float>(sum / data.size());
    }

    float quantile(float q) const {
        std::vector<float> sorted(data);
        int idx = std::clamp(static_cast<int>(q * sorted.size()), 0, static_cast<int>(sorted.size()) - 1);
        std::nth_element(sorted.begin(), sorted.begin() + idx, sorted.end());
        return sorted[idx];
    }

    void rescale(float target_min, float target_max) {
        float lo = min_val();
        float hi = max_val();
        float range = hi - lo;
        if (range < 1e-9f) return;
        float target_range = target_max - target_min;
        for (auto& v : data)
            v = target_min + (v - lo) / range * target_range;
    }

    void shift(float offset) {
        for (auto& v : data)
            v += offset;
    }
};
