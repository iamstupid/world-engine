#include "icosahedral_geodesic.hpp"
#include <algorithm>
#include <cstring>

// ============================================================
// Construction
// ============================================================

IcoMesh::IcoMesh(int subdivision_level) : N_(subdivision_level) {
    assert(N_ >= 1);
    build_ico_verts();
    build_row_table();
}

void IcoMesh::build_ico_verts() {
    // North pole
    ico_verts[0] = {0, 0, 1};

    // Upper ring: 5 vertices at latitude atan(1/2)
    const float upper_z = 1.0f / std::sqrt(5.0f);
    const float upper_r = 2.0f / std::sqrt(5.0f);
    for (int i = 0; i < 5; i++) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / 5.0f;
        ico_verts[1 + i] = {
            upper_r * std::cos(angle),
            upper_r * std::sin(angle),
            upper_z
        };
    }

    // Lower ring: 5 vertices at latitude -atan(1/2), offset by 36°
    for (int i = 0; i < 5; i++) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / 5.0f + static_cast<float>(M_PI) / 5.0f;
        ico_verts[6 + i] = {
            upper_r * std::cos(angle),
            upper_r * std::sin(angle),
            -upper_z
        };
    }

    // South pole
    ico_verts[11] = {0, 0, -1};
}

void IcoMesh::build_row_table() {
    total_rows_ = 3 * N_ + 1;
    row_start_.resize(total_rows_);
    row_width_.resize(total_rows_);

    int offset = 0;
    for (int r = 0; r < total_rows_; r++) {
        row_start_[r] = offset;

        if (r == 0 || r == total_rows_ - 1) {
            row_width_[r] = 1;
        } else if (r <= N_ - 1) {
            row_width_[r] = 5 * r;
        } else if (r <= 2 * N_) {
            row_width_[r] = 5 * N_;
        } else {
            row_width_[r] = 5 * (3 * N_ - r);
        }

        offset += row_width_[r];
    }
    total_cells_ = offset;
    assert(total_cells_ == 10 * N_ * N_ + 2);
}

// ============================================================
// Row/col lookup
// ============================================================

int IcoMesh::row_of(int idx) const {
    int lo = 0, hi = total_rows_ - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (row_start_[mid] <= idx)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

int IcoMesh::col_of(int idx) const {
    int r = row_of(idx);
    return idx - row_start_[r];
}

// ============================================================
// Position computation — fused face_id + barycentrics + warp
// ============================================================

Vec3f IcoMesh::compute_position(int row, int col) const {
    if (row == 0) return ico_verts[0];
    if (row == 3 * N_) return ico_verts[11];

    int N = N_;
    int N2 = 2 * N;

    // Zone classification:
    //   zone 0 (north cap): rows 1..N
    //   zone 1 (equatorial): rows N+1..2N
    //   zone 2 (south cap): rows 2N+1..3N-1
    int zone = (row <= N) ? 0 : ((row > N2) ? 2 : 1);
    int stride = (zone == 0) ? row : ((zone == 2) ? (3 * N - row) : N);
    int col_no  = col / stride;
    int col_rem = col % stride;

    // Face ID
    int fid;
    if (zone == 1) {
        int d = row - N;
        bool is_down = (d + col_rem) > N;
        fid = 5 + col_no * 2 + (is_down ? 1 : 0);
    } else {
        fid = (zone == 0 ? 0 : 15) + col_no;
    }

    // Barycentric coordinates
    //
    // North cap face k = {pole, upper[k], upper[k+1]}:
    //   Grid has 'row' cells per sector at row r. Each cell at (r, j) maps:
    //   u = (N-r)/N  [pole weight]
    //   v = (r-j)/N  [upper[k] weight]
    //   w = j/N      [upper[k+1] weight]
    //
    // Equatorial up-face = {upper[k], lower[k], upper[k+1]}:
    //   Grid cell at (d, j) with d+j <= N:
    //   u = (N-d-j)/N  [upper[k] weight]
    //   v = d/N         [lower[k] weight]
    //   w = j/N         [upper[k+1] weight]
    //
    // Equatorial down-face = {lower[k], lower[k+1], upper[k+1]}:
    //   Grid cell at (d, j) with d+j > N:
    //   u = (N-j)/N         [lower[k] weight]
    //   v = (d+j-N)/N       [lower[k+1] weight]
    //   w = (N-d)/N         [upper[k+1] weight]
    //
    // South cap face k = {pole, lower[(k+1)%5], lower[k]}:
    //   mirror = 3N - row. Each cell at (mirror, j) maps:
    //   u = (N-mirror)/N    [south pole weight]
    //   v = j/N             [lower[(k+1)%5] weight]
    //   w = (mirror-j)/N    [lower[k] weight]
    //
    float u, v, w;
    if (zone == 0) {
        u = (float)(N - row) / N;
        v = (float)(row - col_rem) / N;
        w = (float)col_rem / N;
    } else if (zone == 2) {
        int mirror = 3 * N - row;
        u = (float)(N - mirror) / N;
        v = (float)col_rem / N;
        w = (float)(mirror - col_rem) / N;
    } else {
        int d = row - N;
        if ((d + col_rem) <= N) {
            // Up-face
            u = (float)(N - d - col_rem) / N;
            v = (float)d / N;
            w = (float)col_rem / N;
        } else {
            // Down-face
            u = (float)(N - col_rem) / N;
            v = (float)(d + col_rem - N) / N;
            w = (float)(N - d) / N;
        }
    }

    // Coupled polynomial warp
    float uw = u + WARP_ALPHA * u * (1.0f - u) * (0.5f - u);
    float vw = v + WARP_ALPHA * v * (1.0f - v) * (0.5f - v);
    float ww = w + WARP_ALPHA * w * (1.0f - w) * (0.5f - w);

    float c_ = u * v * w * WARP_BETA;
    uw += c_; vw += c_; ww += c_;

    float s = uw + vw + ww;
    uw /= s; vw /= s; ww /= s;

    // Project to sphere
    const Vec3f& A = ico_verts[FACE_VERTS[fid][0]];
    const Vec3f& B = ico_verts[FACE_VERTS[fid][1]];
    const Vec3f& C = ico_verts[FACE_VERTS[fid][2]];

    return (A * uw + B * vw + C * ww).normalized();
}

Vec3f IcoMesh::cell_position(int idx) const {
    if (positions_built_) return positions_[idx];
    int r = row_of(idx);
    int c = idx - row_start_[r];
    return compute_position(r, c);
}

// ============================================================
// Face generation — the foundation for neighbors
// ============================================================

const std::vector<std::array<int, 3>>& IcoMesh::faces() const {
    if (!faces_built_) build_faces();
    return faces_;
}

int IcoMesh::face_local_to_cell(int fid, int i, int j) const {
    int N = N_;
    int row, col_raw, row_w;

    if (fid < 5) {
        // North cap face f: {pole, upper[f], upper[f+1]}
        row = N - i;
        col_raw = fid * (N - i) + (N - i - j);
        row_w = (row == 0) ? 1 : 5 * row;
    } else if (fid < 15) {
        int s = (fid - 5) / 2;
        if ((fid - 5) % 2 == 0) {
            // Equatorial up-face: {upper[s], lower[s], upper[s+1]}
            row = N + j;
            col_raw = s * N + (N - i - j);
        } else {
            // Equatorial down-face: {lower[s], lower[s+1], upper[s+1]}
            row = N + i + j;
            col_raw = s * N + (N - i);
        }
        row_w = 5 * N;
    } else {
        // South cap face f: {pole, lower[(f+1)%5], lower[f]}
        int f = fid - 15;
        row = 2 * N + i;
        col_raw = f * (N - i) + j;
        row_w = (row == 3 * N) ? 1 : 5 * (3 * N - row);
    }

    int col = ((col_raw % row_w) + row_w) % row_w;
    return row_start_[row] + col;
}

void IcoMesh::build_faces() const {
    faces_.clear();
    faces_.reserve(20 * N_ * N_);

    for (int fid = 0; fid < 20; fid++) {
        for (int i = 0; i < N_; i++) {
            for (int j = 0; j < N_ - i; j++) {
                // Up sub-triangle: (i,j) → (i+1,j) → (i,j+1)
                int a = face_local_to_cell(fid, i, j);
                int b = face_local_to_cell(fid, i + 1, j);
                int c = face_local_to_cell(fid, i, j + 1);
                faces_.push_back({a, b, c});

                // Down sub-triangle: (i+1,j) → (i+1,j+1) → (i,j+1)
                if (i + j + 1 < N_) {
                    int d = face_local_to_cell(fid, i + 1, j + 1);
                    faces_.push_back({b, d, c});
                }
            }
        }
    }

    faces_built_ = true;
}

// ============================================================
// Adjacency — built from face list (for verification only)
// ============================================================

void IcoMesh::build_adjacency() const {
    if (!faces_built_) build_faces();

    adjacency_.clear();
    adjacency_.resize(total_cells_);

    auto add_edge = [&](int u, int v) {
        auto& nbrs = adjacency_[u];
        for (int n : nbrs) {
            if (n == v) return;
        }
        nbrs.push_back(v);
    };

    for (const auto& tri : faces_) {
        add_edge(tri[0], tri[1]); add_edge(tri[1], tri[0]);
        add_edge(tri[1], tri[2]); add_edge(tri[2], tri[1]);
        add_edge(tri[2], tri[0]); add_edge(tri[0], tri[2]);
    }

    adjacency_built_ = true;
}

// ============================================================
// Analytic O(1) neighbor computation
// ============================================================
//
// Each cell has 6 neighbors (hexagon) or 5 (pentagon at the 12
// icosahedron vertices). In (Δrow, Δcol_rem) space:
//
//   row-1 (contracting): col_rem + {0, -1}
//   row  0 (same row):   col     + {-1, +1}
//   row+1 (expanding):   col_rem + {0, +1}
//
// where col' = f * new_stride + (c + offset), mod row_width.
//
// Edge cells at c=0 sit on icosahedral face boundaries.  The
// contracting c-1 wraps to an unrelated sector (wrong), while
// the expanding direction gains a third cross-face neighbor at
// c-1.  Fix: for c=0, use only one contracting neighbor (c+0)
// and three expanding neighbors (c+{-1,0,+1}).  Deduplication
// collapses duplicates at pentagon vertices.

int IcoMesh::compute_neighbors(int row, int col, int* out) const {
    int N = N_;
    int cnt = 0;

    // Helper: add cell at (r, c) with modular wrap, deduplicating
    auto add = [&](int r, int c) {
        int w = row_width_[r];
        int cc = ((c % w) + w) % w;
        int idx = row_start_[r] + cc;
        for (int k = 0; k < cnt; k++)
            if (out[k] == idx) return;
        out[cnt++] = idx;
    };

    // --- Poles ---
    if (row == 0) {
        for (int k = 0; k < 5; k++)
            add(1, k);
        return cnt;
    }
    if (row == 3 * N) {
        for (int k = 0; k < 5; k++)
            add(3 * N - 1, k);
        return cnt;
    }

    // --- Same-row neighbors (all zones) ---
    add(row, col - 1);
    add(row, col + 1);

    if (row <= N - 1) {
        // --- North cap interior (rows 1..N-1) ---
        int f = col / row;
        int c = col % row;

        // Down (expanding): row+1, stride grows by 1
        int next = (row + 1 <= N - 1) ? (row + 1) : N;
        int rdown = row + 1;
        add(rdown, f * next + c);
        add(rdown, f * next + c + 1);
        if (c == 0) add(rdown, f * next + c - 1); // cross-face

        // Up (contracting): row-1, stride shrinks by 1
        if (row == 1) {
            add(0, 0); // pole
        } else {
            int prev = row - 1;
            if (c == 0) {
                add(row - 1, f * prev + c); // only one
            } else {
                add(row - 1, f * prev + c - 1);
                add(row - 1, f * prev + c);
            }
        }

    } else if (row == N) {
        // --- Row N: north cap / equatorial boundary ---
        int f = col / N;
        int c = col % N;

        // Down into equatorial: row N+1 (width 5*N)
        add(N + 1, col - 1);
        add(N + 1, col);

        // Up into cap: row N-1 (contracting)
        if (N == 1) {
            add(0, 0); // pole
        } else if (c == 0) {
            add(N - 1, f * (N - 1) + c); // pentagon: only one
        } else {
            add(N - 1, f * (N - 1) + c - 1);
            add(N - 1, f * (N - 1) + c);
        }

    } else if (row > N && row < 2 * N) {
        // --- Equatorial interior (rows N+1..2N-1) ---
        add(row - 1, col);
        add(row - 1, col + 1);
        add(row + 1, col - 1);
        add(row + 1, col);

    } else if (row == 2 * N) {
        // --- Row 2N: equatorial / south cap boundary ---
        int f = col / N;
        int c = col % N;

        // Up into equatorial: row 2N-1 (width 5*N)
        add(2 * N - 1, col);
        add(2 * N - 1, col + 1);

        // Down into south cap: row 2N+1 (contracting)
        if (N == 1) {
            add(3 * N, 0); // south pole
        } else if (c == 0) {
            add(2 * N + 1, f * (N - 1) + c); // pentagon: only one
        } else {
            add(2 * N + 1, f * (N - 1) + c - 1);
            add(2 * N + 1, f * (N - 1) + c);
        }

    } else {
        // --- South cap interior (rows 2N+1..3N-1) ---
        int mirror = 3 * N - row;
        int f = col / mirror;
        int c = col % mirror;

        // Up (expanding): row-1, stride grows by 1
        int next = (row - 1 >= 2 * N + 1) ? (mirror + 1) : N;
        int rup = row - 1;
        add(rup, f * next + c);
        add(rup, f * next + c + 1);
        if (c == 0) add(rup, f * next + c - 1); // cross-face

        // Down (contracting): row+1, stride shrinks by 1
        if (mirror == 1) {
            add(3 * N, 0); // south pole
        } else {
            int prev = mirror - 1;
            if (c == 0) {
                add(row + 1, f * prev + c); // only one
            } else {
                add(row + 1, f * prev + c - 1);
                add(row + 1, f * prev + c);
            }
        }
    }

    return cnt;
}

// ============================================================
// Neighbor computation — analytic O(1)
// ============================================================

int IcoMesh::neighbor_count(int idx) const {
    int r = row_of(idx);
    int c = idx - row_start_[r];
    int nbr_indices[6];
    return compute_neighbors(r, c, nbr_indices);
}

void IcoMesh::neighbors(int idx, Adjacency* out, int* count) const {
    int r = row_of(idx);
    int c = idx - row_start_[r];
    int nbr_indices[6];
    *count = compute_neighbors(r, c, nbr_indices);

    Vec3f p1 = cell_position(idx);
    for (int i = 0; i < *count; i++) {
        Vec3f p2 = cell_position(nbr_indices[i]);
        float dist = std::acos(std::clamp(p1.dot(p2), -1.0f, 1.0f));
        out[i] = {nbr_indices[i], dist};
    }
}

// ============================================================
// Cell area — barycentric dual: 1/3 of each incident triangle
// ============================================================

float IcoMesh::cell_area(int idx) const {
    if (!faces_built_) build_faces();

    float area = 0.0f;
    for (const auto& tri : faces_) {
        if (tri[0] != idx && tri[1] != idx && tri[2] != idx) continue;
        Vec3f a = cell_position(tri[0]);
        Vec3f b = cell_position(tri[1]);
        Vec3f c = cell_position(tri[2]);
        // Spherical triangle area ≈ flat triangle area for small triangles
        float tri_area = 0.5f * (b - a).cross(c - a).length();
        area += tri_area / 3.0f;
    }
    return area;
}

// ============================================================
// Iterator
// ============================================================

IcoMesh::Iterator::CellType IcoMesh::Iterator::cell_type() const {
    int N = mesh->N();

    // Poles
    if (row == 0 || row == 3 * N) return VERTEX;

    // Pentagon vertices
    if (row == N && (col % N == 0)) return VERTEX;
    if (row == 2 * N && (col % N == 0)) return VERTEX;

    // Edge cells
    int w = mesh->row_width(row);
    int stride = w / 5;
    if (stride > 0 && (col % stride == 0)) return EDGE;

    return FACE;
}

int IcoMesh::Iterator::neighbor_count() const {
    return mesh->neighbor_count(idx);
}

int IcoMesh::Iterator::face_id() const {
    int N = mesh->N();
    if (row == 0) return 0;
    if (row == 3 * N) return 15;

    int zone = (row <= N) ? 0 : ((row > 2 * N) ? 2 : 1);
    int stride = (zone == 0) ? row : ((zone == 2) ? (3 * N - row) : N);
    int col_no = col / stride;

    if (zone == 1) {
        int d = row - N;
        int col_rem = col % stride;
        bool is_down = (d + col_rem) > N;
        return 5 + col_no * 2 + (is_down ? 1 : 0);
    }
    return (zone == 0 ? 0 : 15) + col_no;
}

Vec3f IcoMesh::Iterator::position() const {
    return mesh->compute_position(row, col);
}

IcoMesh::Iterator& IcoMesh::Iterator::operator++() {
    idx++;
    col++;
    if (row < mesh->num_rows() && col >= mesh->row_width(row)) {
        col = 0;
        row++;
    }
    return *this;
}

IcoMesh::Iterator IcoMesh::iterator_at(int idx) const {
    int r = row_of(idx);
    int c = idx - row_start_[r];
    return {this, idx, static_cast<uint16_t>(r), static_cast<uint16_t>(c)};
}

// ============================================================
// Precomputed positions
// ============================================================

const std::vector<Vec3f>& IcoMesh::positions() const {
    if (!positions_built_) build_positions();
    return positions_;
}

void IcoMesh::build_positions() const {
    positions_.resize(total_cells_);
    int idx = 0;
    for (int r = 0; r < total_rows_; r++) {
        int w = row_width_[r];
        for (int c = 0; c < w; c++) {
            positions_[idx++] = compute_position(r, c);
        }
    }
    positions_built_ = true;
}
