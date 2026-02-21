#pragma once

#include "ico_iterator.hpp"
#include "mesh_concept.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <type_traits>
#include <concepts>
#include <cmath>

template<typename T>
class IcoMesh {
public:
    using value_type = T;
    using topology_type = IcoTopology;

    // --- DataIterator: wraps IcoIterator + T* for data access ---

    class iterator {
        IcoIterator it_;
        T* data_;
    public:
        iterator() : it_(), data_(nullptr) {}
        iterator(IcoIterator it, T* data) : it_(it), data_(data) {}

        T& operator*() { return data_[it_.idx()]; }
        const T& operator*() const { return data_[it_.idx()]; }

        iterator& operator++() { ++it_; return *this; }

        bool operator==(const iterator& o) const { return it_ == o.it_; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }

        Vec3f position() const { return it_.position(); }
        int cell_index() const { return it_.idx(); }
        int row() const { return it_.row(); }
        int col() const { return it_.col(); }
        int sector() const { return it_.sector(); }
        int col_rem() const { return it_.col_rem(); }
        int neighbor_count() const { return it_.neighbor_count(); }

        int neighbors(iterator out[6]) const {
            IcoIterator nbrs[6];
            int cnt = it_.neighbors(nbrs);
            for (int i = 0; i < cnt; i++)
                out[i] = iterator(nbrs[i], data_);
            return cnt;
        }
    };

    class const_iterator {
        IcoIterator it_;
        const T* data_;
    public:
        const_iterator() : it_(), data_(nullptr) {}
        const_iterator(IcoIterator it, const T* data) : it_(it), data_(data) {}

        const T& operator*() const { return data_[it_.idx()]; }

        const_iterator& operator++() { ++it_; return *this; }

        bool operator==(const const_iterator& o) const { return it_ == o.it_; }
        bool operator!=(const const_iterator& o) const { return it_ != o.it_; }

        Vec3f position() const { return it_.position(); }
        int cell_index() const { return it_.idx(); }
    };

    struct coordinate { uint32_t idx; uint16_t row, col; };

    // --- Construction ---

    IcoMesh(const IcoTopology* topo, std::string name = "")
        : topo_(topo), data_(topo->num_cells()), name_(std::move(name)) {}

    IcoMesh(int k, std::string name = "")
        : topo_(&ico_topology(k)), data_(topo_->num_cells()), name_(std::move(name)) {}

    // --- Container ---

    int num_cells() const { return static_cast<int>(data_.size()); }

    T& operator[](int idx) { return data_[idx]; }
    const T& operator[](int idx) const { return data_[idx]; }

    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }

    // --- Topology ---

    const IcoTopology& topology() const { return *topo_; }
    int N() const { return topo_->N(); }
    int k() const { return topo_->k(); }
    Vec3f cell_position(int idx) const { return topo_->cell_position(idx); }

    // --- Iteration ---

    iterator begin() { return iterator(IcoIterator::begin(topo_), data_.data()); }
    iterator end() { return iterator(IcoIterator::end(topo_), data_.data()); }

    const_iterator begin() const { return const_iterator(IcoIterator::begin(topo_), data_.data()); }
    const_iterator end() const { return const_iterator(IcoIterator::end(topo_), data_.data()); }

    // --- Stats (constrained by T) ---

    T min_val() const requires std::totally_ordered<T> {
        return *std::min_element(data_.begin(), data_.end());
    }

    T max_val() const requires std::totally_ordered<T> {
        return *std::max_element(data_.begin(), data_.end());
    }

    float mean_val() const requires std::is_arithmetic_v<T> {
        double sum = std::accumulate(data_.begin(), data_.end(), 0.0);
        return static_cast<float>(sum / data_.size());
    }

    T quantile(float q) const requires std::totally_ordered<T> {
        std::vector<T> sorted(data_);
        int idx = std::clamp(static_cast<int>(q * sorted.size()),
                             0, static_cast<int>(sorted.size()) - 1);
        std::nth_element(sorted.begin(), sorted.begin() + idx, sorted.end());
        return sorted[idx];
    }

    void rescale(T lo, T hi) requires std::is_floating_point_v<T> {
        T cur_lo = min_val();
        T cur_hi = max_val();
        T range = cur_hi - cur_lo;
        if (range < T(1e-9)) return;
        T target_range = hi - lo;
        for (auto& v : data_)
            v = lo + (v - cur_lo) / range * target_range;
    }

    void shift(T offset) requires std::is_arithmetic_v<T> {
        for (auto& v : data_)
            v += offset;
    }

    // --- Name ---

    const std::string& name() const { return name_; }

private:
    const IcoTopology* topo_;
    std::vector<T> data_;
    std::string name_;
};
