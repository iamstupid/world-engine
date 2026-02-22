#pragma once

#include <vector>
#include <cstdint>
#include <cassert>
#include <mutex>
#include "mesh_concept.hpp"

/// Immutable icosahedral geodesic topology for N = 2^k.
/// Shared via static factory; thread-safe after construction.
class IcoTopology {
public:
    explicit IcoTopology(int k);

    int k() const { return k_; }
    int N() const { return N_; }
    int num_cells() const { return total_cells_; }
    int num_rows() const { return total_rows_; }
    int num_faces_tri() const { return 20 * N_ * N_; }

    // Row table (raw arrays)
    const int* row_start() const { return row_start_.data(); }
    const int* row_width() const { return row_width_.data(); }

    // Row/col from flat index (binary search)
    int row_of(int idx) const;
    int col_of(int idx) const;

    // Stride from row — comparison only, no division
    int stride_of(int row) const {
        if (row <= 0 || row >= 3 * N_) return 0;
        if (row <= N_) return row;
        if (row <= 2 * N_) return N_;
        return 3 * N_ - row;
    }

    // Position from decomposed coords — no division
    Vec3f compute_position(int row, int col, int sector, int col_rem) const;

    // --- Neighbor computation (dir = CW index from E, 0..count-1) ---

    struct NeighborRC { int idx; int16_t row, col; int8_t sector; };

    // --- Inverse point-location ---

    struct LocateResult {
        int face;               // icosahedron base face [0..19]
        float u, v, w;          // natural (pre-warp) barycentrics within base face
        float s_u, s_v, s_w;    // sub-triangle interpolation weights
        NeighborRC vert[3];     // vert[0]↔s_u, vert[1]↔s_v, vert[2]↔s_w
    };

    /// Locate which sub-triangle of the IGM contains unit-sphere point p.
    LocateResult locate(Vec3f p) const;
    LocateResult locate(Vec3f p, int face_hint) const;

    /// Number of neighbors (5 for pentagons, 6 for hexagons). O(1).
    int neighbor_count(int row, int col, int sector, int col_rem) const;

    /// Single-direction neighbor. Returns false for pentagon gap (dir >= count).
    bool neighbor_at(int row, int col, int sector, int col_rem,
                     int dir, NeighborRC* out) const;

    /// All neighbors in CW order. Returns count (5 or 6).
    int compute_neighbors_full(int row, int col, int sector, int col_rem,
                               NeighborRC* out) const;

    /// Flat-index only (backward compat wrapper).
    int compute_neighbors(int row, int col, int sector, int col_rem,
                          int* out_indices) const;

    // Bulk arrays (lazy, thread-safe via call_once)
    const Vec3f* positions() const;          // [num_cells]
    const int*   faces() const;              // [num_faces_tri * 3]

    // Random-access position (has division for sector decomposition)
    Vec3f cell_position(int idx) const;

    static constexpr int FACE_VERTS[20][3] = {
        {0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 5}, {0, 5, 1},
        {1, 6, 2},  {6, 7, 2},
        {2, 7, 3},  {7, 8, 3},
        {3, 8, 4},  {8, 9, 4},
        {4, 9, 5},  {9, 10, 5},
        {5, 10, 1}, {10, 6, 1},
        {11, 7, 6}, {11, 8, 7}, {11, 9, 8}, {11, 10, 9}, {11, 6, 10}
    };

    Vec3f ico_verts[12];

private:
    int k_, N_, total_cells_, total_rows_;
    std::vector<int> row_start_;
    std::vector<int> row_width_;
    mutable std::vector<Vec3f> positions_cache_;
    mutable std::vector<int>   faces_cache_;
    mutable std::once_flag     pos_flag_;
    mutable std::once_flag     faces_flag_;

    void build_row_table();
    void build_ico_verts();
    void build_positions() const;
    void build_faces() const;
    int  face_local_to_cell(int fid, int i, int j) const;
    NeighborRC face_local_to_rc(int fid, int i, int j) const;

    // Emit helper: wrap sector+col in one shot
    inline NeighborRC emit(int r, int c, int s, int ds) const {
        s += ds;
        int w = row_width_[r];
        if (ds < 0 && s < 0) { s += 5; c += w; }
        if (ds > 0 && s > 4) { s -= 5; c -= w; }
        return { row_start_[r] + c, (int16_t)r, (int16_t)c, (int8_t)s };
    }
};

/// Factory: returns static topology for given k (thread-safe, lazily constructed).
const IcoTopology& ico_topology(int k);
