#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <cassert>
#include "mesh_concept.hpp"

class IcoMesh {
public:
    explicit IcoMesh(int subdivision_level);

    int N() const { return N_; }
    int num_cells() const { return total_cells_; }
    int num_rows() const { return total_rows_; }

    // --- Row table ---
    int row_start(int row) const { return row_start_[row]; }
    int row_width(int row) const { return row_width_[row]; }
    int row_of(int idx) const;
    int col_of(int idx) const;

    // --- Geometry (random access) ---
    Vec3f cell_position(int idx) const;
    float cell_area(int idx) const;
    float latitude(int idx) const { return cell_position(idx).latitude(); }
    float longitude(int idx) const { return cell_position(idx).longitude(); }

    // --- Topology (random access) ---
    int neighbor_count(int idx) const;
    void neighbors(int idx, Adjacency* out, int* count) const;

    // Analytic O(1) neighbor computation from (row, col)
    // Returns the number of distinct neighbors (5 for pentagons, 6 for hexagons)
    int compute_neighbors(int row, int col, int* out_indices) const;

    // --- Iterator ---
    struct Iterator {
        const IcoMesh* mesh;
        int      idx;
        uint16_t row;
        uint16_t col;

        enum CellType { FACE, EDGE, VERTEX };
        CellType cell_type() const;
        int neighbor_count() const;

        int face_id() const;
        Vec3f position() const;

        Iterator& operator++();
        bool operator!=(const Iterator& other) const { return idx != other.idx; }
        int operator*() const { return idx; }
    };

    Iterator begin() const { return {this, 0, 0, 0}; }
    Iterator end() const { return {this, total_cells_, static_cast<uint16_t>(total_rows_), 0}; }
    Iterator iterator_at(int idx) const;

    // --- Precomputed data ---
    const std::vector<Vec3f>& positions() const;
    const std::vector<std::array<int, 3>>& faces() const;

    // --- Icosahedron base geometry ---
    static constexpr int FACE_VERTS[20][3] = {
        // North cap (faces 0-4): north pole + upper ring
        {0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 5}, {0, 5, 1},
        // Equatorial band (faces 5-14): alternating up/down
        {1, 6, 2},  {6, 7, 2},    // pair 0
        {2, 7, 3},  {7, 8, 3},    // pair 1
        {3, 8, 4},  {8, 9, 4},    // pair 2
        {4, 9, 5},  {9, 10, 5},   // pair 3
        {5, 10, 1}, {10, 6, 1},   // pair 4
        // South cap (faces 15-19): south pole + lower ring
        {11, 7, 6}, {11, 8, 7}, {11, 9, 8}, {11, 10, 9}, {11, 6, 10}
    };

    Vec3f ico_verts[12];

private:
    int N_, total_cells_, total_rows_;
    std::vector<int> row_start_;
    std::vector<int> row_width_;
    mutable std::vector<Vec3f> positions_;
    mutable std::vector<std::array<int, 3>> faces_;
    mutable std::vector<std::vector<int>> adjacency_;
    mutable bool positions_built_ = false;
    mutable bool faces_built_ = false;
    mutable bool adjacency_built_ = false;

    void build_row_table();
    void build_ico_verts();
    void build_positions() const;
    void build_faces() const;
    void build_adjacency() const;

    // Internal position computation from (row, col)
    Vec3f compute_position(int row, int col) const;

    // Map face-local grid (i, j) to global cell index
    int face_local_to_cell(int fid, int i, int j) const;
};

// Warp constants for coupled polynomial warp
constexpr float WARP_ALPHA = 0.5372f;
constexpr float WARP_BETA  = -0.4637f;
