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

    int col = (col_raw >= row_w) ? col_raw - row_w : col_raw;
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
// face_local_to_rc — like face_local_to_cell but returns NeighborRC
// ============================================================

IcoTopology::NeighborRC IcoTopology::face_local_to_rc(int fid, int i, int j) const {
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

    int col = (col_raw >= row_w) ? col_raw - row_w : col_raw;
    int idx = row_start_[row] + col;
    int stride = stride_of(row);
    int sector = (stride > 0) ? col / stride : 0;
    return { idx, (int16_t)row, (int16_t)col, (int8_t)sector };
}

// ============================================================
// Inverse point-location: locate()
// ============================================================

namespace {

// --- Double-precision warp helpers ---

constexpr double ALPHA_D = 0.5372;
constexpr double BETA_D  = -0.4637;

inline double warp_1d(double x) {
    return x + ALPHA_D * x * (1.0 - x) * (0.5 - x);
}

inline double warp_1d_deriv(double x) {
    return 1.0 + ALPHA_D * (0.5 - 3.0 * x + 3.0 * x * x);
}

struct WarpJacobian {
    double uw, vw;      // warped barycentrics (renormalized)
    double J00, J01, J10, J11;
};

WarpJacobian eval_forward_warp(double u, double v) {
    double w = 1.0 - u - v;

    double fu = warp_1d(u);
    double fv = warp_1d(v);
    double fw = warp_1d(w);
    double c  = BETA_D * u * v * w;

    double uw_raw = fu + c;
    double vw_raw = fv + c;
    double ww_raw = fw + c;
    double S = uw_raw + vw_raw + ww_raw;
    double inv_S = 1.0 / S;

    double uw = uw_raw * inv_S;
    double vw = vw_raw * inv_S;

    // Partial derivatives of the coupling term
    double dc_du = BETA_D * v * (1.0 - 2.0 * u - v);
    double dc_dv = BETA_D * u * (1.0 - u - 2.0 * v);

    // Partials of raw values
    double duw_du = warp_1d_deriv(u) + dc_du;
    double duw_dv = dc_dv;
    double dvw_du = dc_du;
    double dvw_dv = warp_1d_deriv(v) + dc_dv;
    double dww_du = -warp_1d_deriv(w) + dc_du;
    double dww_dv = -warp_1d_deriv(w) + dc_dv;

    double dS_du = duw_du + dvw_du + dww_du;
    double dS_dv = duw_dv + dvw_dv + dww_dv;

    // Quotient rule for renormalized values
    double inv_S2 = inv_S * inv_S;

    WarpJacobian r;
    r.uw = uw;
    r.vw = vw;
    r.J00 = (duw_du * S - uw_raw * dS_du) * inv_S2;
    r.J01 = (duw_dv * S - uw_raw * dS_dv) * inv_S2;
    r.J10 = (dvw_du * S - vw_raw * dS_du) * inv_S2;
    r.J11 = (dvw_dv * S - vw_raw * dS_dv) * inv_S2;
    return r;
}

void inverse_warp(double uw_target, double vw_target, float* out_u, float* out_v) {
    double u = uw_target;
    double v = vw_target;

    for (int iter = 0; iter < 2; iter++) {
        WarpJacobian wr = eval_forward_warp(u, v);
        double ru = wr.uw - uw_target;
        double rv = wr.vw - vw_target;

        double det = wr.J00 * wr.J11 - wr.J01 * wr.J10;
        if (std::abs(det) < 1e-15) break;
        double inv_det = 1.0 / det;

        double du = -(wr.J11 * ru - wr.J01 * rv) * inv_det;
        double dv = -(-wr.J10 * ru + wr.J00 * rv) * inv_det;

        u += du;
        v += dv;
    }

    // Clamp to valid barycentric triangle
    if (u < 0.0) u = 0.0;
    if (v < 0.0) v = 0.0;
    if (u + v > 1.0) {
        double s = u + v;
        u /= s;
        v /= s;
    }

    *out_u = static_cast<float>(u);
    *out_v = static_cast<float>(v);
}

// --- Test if point is inside a specific spherical face ---

bool point_in_face(const Vec3f* ico_v, Vec3f p, int fid) {
    const Vec3f& A = ico_v[IcoTopology::FACE_VERTS[fid][0]];
    const Vec3f& B = ico_v[IcoTopology::FACE_VERTS[fid][1]];
    const Vec3f& C = ico_v[IcoTopology::FACE_VERTS[fid][2]];
    float d_ab = p.dot(A.cross(B));
    float d_bc = p.dot(B.cross(C));
    float d_ca = p.dot(C.cross(A));
    return (d_ab >= -1e-6f && d_bc >= -1e-6f && d_ca >= -1e-6f);
}

// --- Face-finding: brute-force spherical half-plane test ---

int find_face_brute(const Vec3f* ico_v, Vec3f p) {
    float best = -2.0f;
    int best_fid = 0;
    for (int fid = 0; fid < 20; fid++) {
        const Vec3f& A = ico_v[IcoTopology::FACE_VERTS[fid][0]];
        const Vec3f& B = ico_v[IcoTopology::FACE_VERTS[fid][1]];
        const Vec3f& C = ico_v[IcoTopology::FACE_VERTS[fid][2]];

        float d_ab = p.dot(A.cross(B));
        float d_bc = p.dot(B.cross(C));
        float d_ca = p.dot(C.cross(A));

        if (d_ab >= -1e-6f && d_bc >= -1e-6f && d_ca >= -1e-6f)
            return fid;

        float m = std::min({d_ab, d_bc, d_ca});
        if (m > best) { best = m; best_fid = fid; }
    }
    return best_fid;
}

// --- Face-finding: accelerated latitude-band + sector ---
//
// The icosahedron has two "tropics" at z = ±1/√5 separating caps from
// the equatorial band.  Cap sector boundaries are meridians (constant
// longitude), but equatorial sector boundaries are great-circle arcs
// from an upper-ring vertex (lon = s·72°, z = +T) to the adjacent
// lower-ring vertex (lon = s·72° + 36°, z = −T).  So the effective
// boundary longitude shifts by 36° across the band — a latitude-
// adjusted longitude is needed for correct sector classification.

constexpr float TROPIC_Z = 0.4472136f;                 // 1/√5
constexpr float TWO_PI   = 2.0f * float(M_PI);
constexpr float S72      = TWO_PI / 5.0f;              // 72°
constexpr float S36      = float(M_PI) / 5.0f;         // 36°
constexpr float INV_2T   = 1.0f / (2.0f * TROPIC_Z);  // 1/(2T)

int find_face_fast(const Vec3f* ico_v, Vec3f p) {
    int cands[6];
    int nc = 0;

    float lon = std::atan2(p.y, p.x);
    if (lon < 0.0f) lon += TWO_PI;

    // Latitude-adjusted longitude for the equatorial band.
    // t ∈ [0,1]: 0 at upper tropic, 1 at lower tropic.
    // At t, the sector-s boundary sits at lon = s·72° + t·36°,
    // so we subtract t·36° from lon before dividing by 72°.
    float t = std::max(0.0f, std::min(1.0f,
                  (TROPIC_Z - p.z) * INV_2T));
    float eq_lon = lon - t * S36;
    if (eq_lon < 0.0f) eq_lon += TWO_PI;
    int eq_s = (int)(eq_lon / S72) % 5;

    if (p.z > TROPIC_Z) {
        // North cap: boundaries are meridians at s·72°
        int s = (int)(lon / S72) % 5;
        cands[nc++] = s;
        cands[nc++] = (s + 4) % 5;
        // Near tropic: also try equatorial (uses adjusted sector)
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
    } else if (p.z < -TROPIC_Z) {
        // South cap: boundaries at s·72° + 36°
        float slon = lon - S36;
        if (slon < 0.0f) slon += TWO_PI;
        int s = (int)(slon / S72) % 5;
        cands[nc++] = 15 + s;
        cands[nc++] = 15 + (s + 4) % 5;
        // Near tropic: equatorial
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
    } else {
        // Equatorial band: use latitude-adjusted sector
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
        int adj = (eq_s + 4) % 5;
        cands[nc++] = 5 + adj * 2;
        cands[nc++] = 5 + adj * 2 + 1;
        // Near-tropic crossover into cap
        if (p.z > 0.0f) {
            cands[nc++] = (int)(lon / S72) % 5;
        } else {
            float slon = lon - S36;
            if (slon < 0.0f) slon += TWO_PI;
            cands[nc++] = 15 + (int)(slon / S72) % 5;
        }
    }

    for (int ci = 0; ci < nc; ci++) {
        if (point_in_face(ico_v, p, cands[ci]))
            return cands[ci];
    }
    return find_face_brute(ico_v, p);
}

} // anonymous namespace

IcoTopology::LocateResult IcoTopology::locate(Vec3f p) const {
    return locate(p, -1);
}

IcoTopology::LocateResult IcoTopology::locate(Vec3f p, int face_hint) const {
    p = p.normalized();
    const int N = N_;

    // --- Stage 1: Face finding ---
    int face = -1;

    // Try face_hint first
    if (face_hint >= 0 && face_hint < 20) {
        if (point_in_face(ico_verts, p, face_hint))
            face = face_hint;
    }

    // Accelerated search
    if (face < 0)
        face = find_face_fast(ico_verts, p);

    // --- Stage 2: Warped barycentrics via scalar triple products ---
    const Vec3f& A = ico_verts[FACE_VERTS[face][0]];
    const Vec3f& B = ico_verts[FACE_VERTS[face][1]];
    const Vec3f& C = ico_verts[FACE_VERTS[face][2]];

    float pBC = p.dot(B.cross(C));
    float ApC = A.dot(p.cross(C));
    float ABp = A.dot(B.cross(p));
    float sum = pBC + ApC + ABp;

    float uw, vw;
    if (std::abs(sum) < 1e-12f) {
        uw = vw = 1.0f / 3.0f;
    } else {
        float inv_sum = 1.0f / sum;
        uw = pBC * inv_sum;
        vw = ApC * inv_sum;
    }

    // Clamp for floating-point edge cases
    uw = std::max(0.0f, std::min(1.0f, uw));
    vw = std::max(0.0f, std::min(1.0f - uw, vw));

    // --- Stage 3: Inverse warp (Newton, double precision) ---
    float u, v;
    inverse_warp((double)uw, (double)vw, &u, &v);
    float w = 1.0f - u - v;

    // --- Sub-triangle identification ---
    float fi = u * N;
    float fj = v * N;
    int i = (int)fi;
    int j = (int)fj;

    // Clamp to valid grid range
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i >= N) i = N - 1;
    if (j >= N - i) j = N - i - 1;

    float fu = fi - i;
    float fv = fj - j;

    LocateResult result;
    result.face = face;
    result.u = u;
    result.v = v;
    result.w = w;

    if (fu + fv <= 1.0f) {
        // Lower sub-triangle: (i,j), (i+1,j), (i,j+1)
        result.s_u = 1.0f - fu - fv;
        result.s_v = fu;
        result.s_w = fv;
        result.vert[0] = face_local_to_rc(face, i, j);
        result.vert[1] = face_local_to_rc(face, i + 1, j);
        result.vert[2] = face_local_to_rc(face, i, j + 1);
    } else {
        // Upper sub-triangle: (i+1,j+1), (i+1,j), (i,j+1)
        result.s_u = fu + fv - 1.0f;
        result.s_v = 1.0f - fv;
        result.s_w = 1.0f - fu;
        result.vert[0] = face_local_to_rc(face, i + 1, j + 1);
        result.vert[1] = face_local_to_rc(face, i + 1, j);
        result.vert[2] = face_local_to_rc(face, i, j + 1);
    }

    return result;
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
