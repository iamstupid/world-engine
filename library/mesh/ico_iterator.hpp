#pragma once

#include "ico_topology.hpp"
#include <cstdint>

/// 16-byte iterator with sector tracking for division-free traversal.
/// Supports sequential walk (operator++) and 6-directional neighbor walk.
/// Practical limit: k <= 13 (N <= 8192) due to uint16_t row/col.
class IcoIterator {
    uintptr_t topo_sector_;  // IcoTopology* | sector in low 3 bits
    uint32_t  idx_;
    uint16_t  row_;
    uint16_t  col_;

public:
    IcoIterator() : topo_sector_(0), idx_(0), row_(0), col_(0) {}

    IcoIterator(const IcoTopology* t, int sector, int idx, int row, int col)
        : topo_sector_(reinterpret_cast<uintptr_t>(t) | uintptr_t(sector & 7)),
          idx_(static_cast<uint32_t>(idx)),
          row_(static_cast<uint16_t>(row)),
          col_(static_cast<uint16_t>(col)) {}

    // --- Accessors ---

    const IcoTopology* topo() const {
        return reinterpret_cast<const IcoTopology*>(topo_sector_ & ~uintptr_t(7));
    }
    int sector() const { return int(topo_sector_ & 7); }
    int idx() const { return static_cast<int>(idx_); }
    int row() const { return static_cast<int>(row_); }
    int col() const { return static_cast<int>(col_); }

    int stride() const { return topo()->stride_of(row_); }
    int col_rem() const { return col_ - sector() * stride(); }

    int neighbor_count() const {
        return topo()->neighbor_count(row_, col_, sector(), col_rem());
    }

    // --- Geometry (division-free via maintained sector/col_rem) ---

    Vec3f position() const {
        return topo()->compute_position(row_, col_, sector(), col_rem());
    }

    // --- Sequential walk (division-free) ---

    IcoIterator& operator++() {
        idx_++;
        col_++;
        const int* rw = topo()->row_width();
        if (col_ >= rw[row_]) {
            row_++;
            col_ = 0;
            set_sector(0);
        } else {
            int s = stride();
            if (s > 0 && col_ >= (sector() + 1) * s) {
                set_sector(sector() + 1);
            }
        }
        return *this;
    }

    // --- 6-directional neighbor access ---

    /// Fill out[] with neighbor iterators in CW order from E. Returns count (5 or 6).
    int neighbors(IcoIterator out[6]) const {
        const IcoTopology* t = topo();
        IcoTopology::NeighborRC nbrs[6];
        int cnt = t->compute_neighbors_full(row_, col_, sector(), col_rem(), nbrs);
        for (int i = 0; i < cnt; i++)
            out[i] = IcoIterator(t, nbrs[i].sector, nbrs[i].idx, nbrs[i].row, nbrs[i].col);
        return cnt;
    }

    /// Walk to neighbor in given direction (0=E,1=SE,2=SW,3=W,4=NW,5=NE).
    /// Returns false for pentagon gap.
    bool walk(int dir) {
        IcoTopology::NeighborRC nbr;
        if (!topo()->neighbor_at(row_, col_, sector(), col_rem(), dir, &nbr))
            return false;
        *this = IcoIterator(topo(), nbr.sector, nbr.idx, nbr.row, nbr.col);
        return true;
    }

    // --- Range protocol ---

    int operator*() const { return static_cast<int>(idx_); }
    bool operator==(const IcoIterator& o) const { return idx_ == o.idx_; }
    bool operator!=(const IcoIterator& o) const { return idx_ != o.idx_; }

    // --- Factory ---

    static IcoIterator begin(const IcoTopology* t) {
        return IcoIterator(t, 0, 0, 0, 0);
    }
    static IcoIterator end(const IcoTopology* t) {
        return IcoIterator(t, 0, t->num_cells(), 0, 0);
    }

private:
    void set_sector(int s) {
        topo_sector_ = (topo_sector_ & ~uintptr_t(7)) | uintptr_t(s & 7);
    }
};

static_assert(sizeof(IcoIterator) == 16, "IcoIterator must be 16 bytes");
static_assert(alignof(IcoTopology) >= 8, "IcoTopology must be 8-byte aligned for sector packing");
