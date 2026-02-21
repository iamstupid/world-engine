#include "ico_topology.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
constexpr float WARP_ALPHA = 0.5372f;
constexpr float WARP_BETA  = -0.4637f;
} // namespace

// ============================================================
// Construction
// ============================================================

IcoTopology::IcoTopology(int k) : k_(k), N_(1 << k) {
    assert(k >= 0 && k <= 20);
    build_ico_verts();
    build_row_table();
}

void IcoTopology::build_ico_verts() {
    ico_verts[0] = {0, 0, 1};

    const float upper_z = 1.0f / std::sqrt(5.0f);
    const float upper_r = 2.0f / std::sqrt(5.0f);
    for (int i = 0; i < 5; i++) {
        float angle = 2.0f * float(M_PI) * i / 5.0f;
        ico_verts[1 + i] = {
            upper_r * std::cos(angle),
            upper_r * std::sin(angle),
            upper_z
        };
    }

    for (int i = 0; i < 5; i++) {
        float angle = 2.0f * float(M_PI) * i / 5.0f + float(M_PI) / 5.0f;
        ico_verts[6 + i] = {
            upper_r * std::cos(angle),
            upper_r * std::sin(angle),
            -upper_z
        };
    }

    ico_verts[11] = {0, 0, -1};
}

void IcoTopology::build_row_table() {
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
// Row/col lookup (binary search)
// ============================================================

int IcoTopology::row_of(int idx) const {
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

int IcoTopology::col_of(int idx) const {
    return idx - row_start_[row_of(idx)];
}

// ============================================================
// Position — division-free (sector/col_rem provided by caller)
// ============================================================

Vec3f IcoTopology::compute_position(int row, int col, int sector, int col_rem) const {
    if (row == 0)      return ico_verts[0];
    if (row == 3 * N_) return ico_verts[11];

    const int N = N_;
    const int zone = (row <= N) ? 0 : ((row > 2 * N) ? 2 : 1);

    // Face ID
    int fid;
    if (zone == 1) {
        int d = row - N;
        fid = 5 + sector * 2 + ((d + col_rem) > N ? 1 : 0);
    } else {
        fid = (zone == 0 ? 0 : 15) + sector;
    }

    // Barycentric coordinates
    const float invN = 1.0f / N;
    float u, v, w;
    if (zone == 0) {
        u = float(N - row)       * invN;
        v = float(row - col_rem) * invN;
        w = float(col_rem)       * invN;
    } else if (zone == 2) {
        int mirror = 3 * N - row;
        u = float(N - mirror)       * invN;
        v = float(col_rem)          * invN;
        w = float(mirror - col_rem) * invN;
    } else {
        int d = row - N;
        if ((d + col_rem) <= N) {
            u = float(N - d - col_rem) * invN;
            v = float(d)              * invN;
            w = float(col_rem)        * invN;
        } else {
            u = float(N - col_rem)     * invN;
            v = float(d + col_rem - N) * invN;
            w = float(N - d)           * invN;
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

    const Vec3f& A = ico_verts[FACE_VERTS[fid][0]];
    const Vec3f& B = ico_verts[FACE_VERTS[fid][1]];
    const Vec3f& C = ico_verts[FACE_VERTS[fid][2]];

    return (A * uw + B * vw + C * ww).normalized();
}

Vec3f IcoTopology::cell_position(int idx) const {
    int r = row_of(idx);
    int c = idx - row_start_[r];
    int s = stride_of(r);
    if (s == 0) return compute_position(r, c, 0, 0);
    int sector = c / s;
    int col_rem = c - sector * s;
    return compute_position(r, c, sector, col_rem);
}

// ============================================================
// Cell type classification (bitflag encoding)
//
//   Bit layout:  [MID:4][BOT:3][TOP:2][VRIDGE:1][VERTEX:0]
//
//   typ >> 2  →  zone  (1=TOP, 2=BOT, 4=MID, 5=TMID, 6=BMID)
//   typ >> 1  →  direction pattern (which CW neighbor order)
//
//   Direction patterns (CW from E):
//     2  TOP face      :  E  SE  S   W   NW  N
//     3  TOP vridge    :  E  SE  S   SW  W   N
//     4  BOT face      :  E  S   SW  W   N   NE
//     5  BOT vridge    :  E  S   W   NW  N   NE
//     8  MID face      :  E  S   SW  W   N   NE
//     9  MID vridge    :  E  S   SW  W   N   NE
//    10  TMID face     :  E  S   SW  W   NW  N
//    11  TMID vertex   :  E  S   SW  W   N       (pentagon)
//    12  BMID face     :  E  S   SW  W   N   NE
//    13  BMID vertex   :  E  S   W   N   NE      (pentagon)
// ============================================================

namespace {

enum CellType : int {
    CT_VERTEX    = 1,
    CT_VRIDGE    = 2,
    CT_TOP       = 4,
    CT_BOT       = 8,
    CT_MID       = 16
};

inline CellType get_cell_type(int row, int col_rem, int N) {
    int top = (row <= N)               * CT_TOP;
    int bot = (row >= 2 * N)           * CT_BOT;
    int mid = (row >= N && row <= 2*N) * CT_MID;
    int hr  = ((top && mid) || (bot && mid));
    int vr  = (col_rem == 0)           * CT_VRIDGE;
    int vt  = ((hr && vr) || row == 0 || row == 3*N) * CT_VERTEX;
    return static_cast<CellType>(top | bot | mid | vr | vt);
}

} // anonymous namespace

// ============================================================
// Preprocessor machinery for direction dispatch
// ============================================================

#define PP_CAT_(a, b)  a##b
#define PP_CAT(a, b)   PP_CAT_(a, b)

#define PP_NARG(...)    PP_NARG_(__VA_ARGS__, PP_RSEQ_())
#define PP_NARG_(...)   PP_N_(__VA_ARGS__)
#define PP_N_(                                                       \
    _1, _2, _3, _4, _5, _6, _7, _8,                                 \
    _9,_10,_11,_12,_13,_14,_15,_16, N_, ...) N_
#define PP_RSEQ_()      16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

// SWI_k: switch body with k cases (for neighbor_at)
#define SWI_5(a,b,c,d,e)       case 0: a; break; case 1: b; break; \
    case 2: c; break; case 3: d; break; case 4: e; break;
#define SWI_6(a,b,c,d,e,f_)    SWI_5(a,b,c,d,e) case 5: f_; break;
#define SWI(x, ...) switch(x) { PP_CAT(SWI_, PP_NARG(__VA_ARGS__))(__VA_ARGS__) }

// STI_k: emit k statements sequentially (for compute_neighbors)
#define STI_5(a,b,c,d,e)       a;b;c;d;e;
#define STI_6(a,b,c,d,e,f_)    a;b;c;d;e;f_;
#define STITCH(...) PP_CAT(STI_, PP_NARG(__VA_ARGS__))(__VA_ARGS__)

// SNEF_: compute (rw, nw, sw, nef, sef) per zone in one switch case
#define SNEF_(cas, row_w, nm, sm)  \
    case cas:                      \
        rw  = row_w;               \
        nw  = row_w + (nm);        \
        sw  = row_w + (sm);        \
        nef = col + (nm) * f;      \
        sef = col + (sm) * f;      \
        break;

// Direction macros (ICO_ prefix to avoid collision with local N variable).
// Each references local vars: row, col, f, c, rw, nw, sw, nef, sef
// and the per-function em_ macro.
#define ICO_E  em_(row,   col+1,  f,  (c+1>=rw))
#define ICO_SE em_(row+1, sef+1,  f,  (c+1>=sw))
#define ICO_S  em_(row+1, sef,    f,  (sw > 0 && c >= sw))
#define ICO_SW em_(row+1, sef-1,  f, -(c<1))
#define ICO_W  em_(row,   col-1,  f, -(c<1))
#define ICO_NW em_(row-1, nef-1,  f, -(c<1))
#define ICO_N  em_(row-1, nef,    f,  (nw > 0 && c >= nw))
#define ICO_NE em_(row-1, nef+1,  f,  (c+1>=nw))

// ============================================================
// Neighbors — type-classified dispatch (sample_ico pattern)
// ============================================================

int IcoTopology::neighbor_count(int row, int col, int sector, int col_rem) const {
    if (row == 0 || row == 3 * N_) return 5;
    return 6 - (get_cell_type(row, col_rem, N_) & CT_VERTEX);
}

bool IcoTopology::neighbor_at(int row, int col, int sector, int col_rem,
                              int dir, NeighborRC* out) const {
    const int N = N_;
    const int f = sector;
    const int c = col_rem;

    // Poles
    if (row == 0) {
        if (dir >= 5) return false;
        int s = (5 - dir) % 5;
        *out = emit(1, s, s, 0);
        return true;
    }
    if (row == 3 * N) {
        if (dir >= 5) return false;
        *out = emit(3*N - 1, dir, dir, 0);
        return true;
    }

    const CellType typ = get_cell_type(row, c, N);
    if (dir >= 6 - (typ & CT_VERTEX)) return false;

#define em_(...) *out = emit(__VA_ARGS__); return true

    int rw, nw, sw, sef, nef;
    switch (typ >> 2) {
        SNEF_(1,  row,       (-1),  1)
        SNEF_(2,  3*N - row,  1,   (-1))
        SNEF_(4,  N,          0,    0)
        SNEF_(5,  N,         (-1),  0)
        SNEF_(6,  N,          0,   (-1))
    }

    switch (typ >> 1) {
        case 2:  SWI(dir, ICO_E, ICO_SE, ICO_S, ICO_W, ICO_NW, ICO_N) break;
        case 3:  SWI(dir, ICO_E, ICO_SE, ICO_S, ICO_SW, ICO_W, ICO_N) break;
        case 4: case 8: case 9: case 12:
                 SWI(dir, ICO_E, ICO_S, ICO_SW, ICO_W, ICO_N, ICO_NE) break;
        case 5:  SWI(dir, ICO_E, ICO_S, ICO_W, ICO_NW, ICO_N, ICO_NE) break;
        case 10: SWI(dir, ICO_E, ICO_S, ICO_SW, ICO_W, ICO_NW, ICO_N) break;
        case 11: SWI(dir, ICO_E, ICO_S, ICO_SW, ICO_W, ICO_N) break;
        case 13: SWI(dir, ICO_E, ICO_S, ICO_W, ICO_N, ICO_NE) break;
    }

#undef em_
    return false;
}

int IcoTopology::compute_neighbors_full(int row, int col, int sector, int col_rem,
                                        NeighborRC* out) const {
    const int N = N_;
    const int f = sector;
    const int c = col_rem;
    int cnt = 0;

#define em_(...) (out[cnt++] = emit(__VA_ARGS__))

    // Poles
    if (row == 0) {
        for (int d = 5; d >= 1; --d) em_(1, d % 5, d % 5, 0);
        return cnt;
    }
    if (row == 3 * N) {
        for (int d = 0; d < 5; ++d) em_(3*N - 1, d, d, 0);
        return cnt;
    }

    const CellType typ = get_cell_type(row, c, N);

    int rw, nw, sw, sef, nef;
    switch (typ >> 2) {
        SNEF_(1,  row,       (-1),  1)
        SNEF_(2,  3*N - row,  1,   (-1))
        SNEF_(4,  N,          0,    0)
        SNEF_(5,  N,         (-1),  0)
        SNEF_(6,  N,          0,   (-1))
    }

    switch (typ >> 1) {
        case 2:  STITCH(ICO_E, ICO_SE, ICO_S, ICO_W, ICO_NW, ICO_N) return cnt;
        case 3:  STITCH(ICO_E, ICO_SE, ICO_S, ICO_SW, ICO_W, ICO_N) return cnt;
        case 4: case 8: case 9: case 12:
                 STITCH(ICO_E, ICO_S, ICO_SW, ICO_W, ICO_N, ICO_NE) return cnt;
        case 5:  STITCH(ICO_E, ICO_S, ICO_W, ICO_NW, ICO_N, ICO_NE) return cnt;
        case 10: STITCH(ICO_E, ICO_S, ICO_SW, ICO_W, ICO_NW, ICO_N) return cnt;
        case 11: STITCH(ICO_E, ICO_S, ICO_SW, ICO_W, ICO_N) return cnt;
        case 13: STITCH(ICO_E, ICO_S, ICO_W, ICO_N, ICO_NE) return cnt;
    }

#undef em_
    return cnt;
}

// Clean up file-scope macros
#undef ICO_E
#undef ICO_SE
#undef ICO_S
#undef ICO_SW
#undef ICO_W
#undef ICO_NW
#undef ICO_N
#undef ICO_NE
#undef SNEF_
#undef STITCH
#undef STI_5
#undef STI_6
#undef SWI
#undef SWI_5
#undef SWI_6
#undef PP_RSEQ_
#undef PP_N_
#undef PP_NARG_
#undef PP_NARG
#undef PP_CAT_
#undef PP_CAT

int IcoTopology::compute_neighbors(int row, int col, int sector, int col_rem,
                                   int* out) const {
    NeighborRC full[6];
    int cnt = compute_neighbors_full(row, col, sector, col_rem, full);
    for (int i = 0; i < cnt; i++)
        out[i] = full[i].idx;
    return cnt;
}

// ============================================================
// Face generation
// ============================================================

int IcoTopology::face_local_to_cell(int fid, int i, int j) const {
    int N = N_;
    int row, col_raw, row_w;

    if (fid < 5) {
        row = N - i;
        col_raw = fid * (N - i) + (N - i - j);
        row_w = (row == 0) ? 1 : 5 * row;
    } else if (fid < 15) {
        int s = (fid - 5) / 2;
        if ((fid - 5) % 2 == 0) {
            row = N + j;
            col_raw = s * N + (N - i - j);
        } else {
            row = N + i + j;
            col_raw = s * N + (N - i);
        }
        row_w = 5 * N;
    } else {
        int f = fid - 15;
        row = 2 * N + i;
        col_raw = f * (N - i) + j;
        row_w = (row == 3 * N) ? 1 : 5 * (3 * N - row);
    }

    int col = ((col_raw % row_w) + row_w) % row_w;
    return row_start_[row] + col;
}

void IcoTopology::build_faces() const {
    int total_tris = 20 * N_ * N_;
    faces_cache_.resize(total_tris * 3);
    int idx = 0;

    for (int fid = 0; fid < 20; fid++) {
        for (int i = 0; i < N_; i++) {
            for (int j = 0; j < N_ - i; j++) {
                int a = face_local_to_cell(fid, i, j);
                int b = face_local_to_cell(fid, i + 1, j);
                int c = face_local_to_cell(fid, i, j + 1);
                faces_cache_[idx++] = a;
                faces_cache_[idx++] = b;
                faces_cache_[idx++] = c;

                if (i + j + 1 < N_) {
                    int d = face_local_to_cell(fid, i + 1, j + 1);
                    faces_cache_[idx++] = b;
                    faces_cache_[idx++] = d;
                    faces_cache_[idx++] = c;
                }
            }
        }
    }

    assert(idx == total_tris * 3);
}

// ============================================================
// Bulk export (lazy, thread-safe)
// ============================================================

void IcoTopology::build_positions() const {
    positions_cache_.resize(total_cells_);
    int idx = 0;
    for (int r = 0; r < total_rows_; r++) {
        int w = row_width_[r];
        int s = stride_of(r);
        int sector = 0, col_rem = 0;
        for (int c = 0; c < w; c++) {
            positions_cache_[idx++] = compute_position(r, c, sector, col_rem);
            col_rem++;
            if (s > 0 && col_rem >= s) {
                col_rem = 0;
                sector++;
            }
        }
    }
}

const Vec3f* IcoTopology::positions() const {
    std::call_once(pos_flag_, &IcoTopology::build_positions, this);
    return positions_cache_.data();
}

const int* IcoTopology::faces() const {
    std::call_once(faces_flag_, &IcoTopology::build_faces, this);
    return faces_cache_.data();
}

// ============================================================
// Factory
// ============================================================

const IcoTopology& ico_topology(int k) {
    static constexpr int MAX_K = 20;
    assert(k >= 0 && k <= MAX_K);
    static std::array<std::optional<IcoTopology>, MAX_K + 1> cache;
    static std::array<std::once_flag, MAX_K + 1> flags;
    std::call_once(flags[k], [&]{ cache[k].emplace(k); });
    return *cache[k];
}
