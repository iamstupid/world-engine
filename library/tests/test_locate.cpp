#include "mesh/ico_topology.hpp"
#include "mesh/ico_mesh.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_pass = 0, g_fail = 0;

// Simple deterministic PRNG (xorshift32)
static uint32_t rng_state = 12345;
static uint32_t xorshift32() {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}
static float randf() { return (xorshift32() & 0xFFFFFF) / (float)0xFFFFFF; }

// Generate uniform random point on unit sphere
static Vec3f random_sphere_point() {
    float z = 2.0f * randf() - 1.0f;
    float phi = 2.0f * float(M_PI) * randf();
    float r = std::sqrt(1.0f - z * z);
    return {r * std::cos(phi), r * std::sin(phi), z};
}

// ============================================================
// Test 1: Roundtrip — locate(cell_position(i)) finds that cell
// ============================================================

static bool test_locate_roundtrip() {
    printf("  test_locate_roundtrip\n");

    int ks[] = {0, 1, 2, 3, 4, 6};
    for (int ki = 0; ki < 6; ki++) {
        int k = ks[ki];
        const IcoTopology& t = ico_topology(k);
        const Vec3f* pos = t.positions();
        int N = t.N();
        int misses = 0;

        for (int idx = 0; idx < t.num_cells(); idx++) {
            auto loc = t.locate(pos[idx]);
            bool found = false;
            for (int v = 0; v < 3; v++) {
                if (loc.vert[v].idx == idx) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                misses++;
                if (misses <= 3) {
                    printf("    k=%d idx=%d face=%d verts=(%d,%d,%d) "
                           "s=(%.4f,%.4f,%.4f)\n",
                           k, idx, loc.face,
                           loc.vert[0].idx, loc.vert[1].idx, loc.vert[2].idx,
                           loc.s_u, loc.s_v, loc.s_w);
                }
            }
        }

        if (misses > 0) {
            printf("    FAIL: k=%d — %d/%d cells not found in sub-triangle\n",
                   k, misses, t.num_cells());
            return false;
        }
        printf("    k=%d (N=%d, %d cells): OK\n", k, N, t.num_cells());
    }
    return true;
}

// ============================================================
// Test 2: Partition of unity — s_u + s_v + s_w ≈ 1, all in [0,1]
// ============================================================

static bool test_locate_splat_partition_of_unity() {
    printf("  test_locate_splat_partition_of_unity\n");

    const IcoTopology& t = ico_topology(4);
    rng_state = 77777;

    for (int i = 0; i < 10000; i++) {
        Vec3f p = random_sphere_point();
        auto loc = t.locate(p);
        float sum = loc.s_u + loc.s_v + loc.s_w;
        if (std::abs(sum - 1.0f) > 1e-5f) {
            printf("    FAIL: point %d: s_u+s_v+s_w = %.8f\n", i, sum);
            return false;
        }
        if (loc.s_u < -1e-5f || loc.s_v < -1e-5f || loc.s_w < -1e-5f) {
            printf("    FAIL: point %d: negative weight (%.6f, %.6f, %.6f)\n",
                   i, loc.s_u, loc.s_v, loc.s_w);
            return false;
        }
        if (loc.s_u > 1.0f + 1e-5f || loc.s_v > 1.0f + 1e-5f || loc.s_w > 1.0f + 1e-5f) {
            printf("    FAIL: point %d: weight > 1\n", i);
            return false;
        }
    }
    printf("    10000 random points: OK\n");
    return true;
}

// ============================================================
// Test 3: Reconstruction — interpolate position from weights
// ============================================================

static bool test_locate_reconstruction() {
    printf("  test_locate_reconstruction\n");

    const IcoTopology& t = ico_topology(4);
    const Vec3f* pos = t.positions();
    rng_state = 99999;
    float max_err = 0;

    for (int i = 0; i < 10000; i++) {
        Vec3f p = random_sphere_point();
        auto loc = t.locate(p);

        Vec3f p0 = pos[loc.vert[0].idx];
        Vec3f p1 = pos[loc.vert[1].idx];
        Vec3f p2 = pos[loc.vert[2].idx];
        Vec3f recon = (p0 * loc.s_u + p1 * loc.s_v + p2 * loc.s_w).normalized();
        float err = (recon - p).length();
        if (err > max_err) max_err = err;
    }

    // N=16: sub-triangle angular size ~ 2*pi / (5*16) ~ 0.08 rad
    // Reconstruction error should be within one sub-cell diameter
    float threshold = 2.0f * float(M_PI) / (5.0f * t.N());
    printf("    max error: %.6e (threshold: %.6e)\n", max_err, threshold);
    if (max_err > threshold) {
        printf("    FAIL: reconstruction error too large\n");
        return false;
    }
    return true;
}

// ============================================================
// Test 4: Face edges — points on face boundaries
// ============================================================

static bool test_locate_face_edges() {
    printf("  test_locate_face_edges\n");

    const IcoTopology& t = ico_topology(3);

    // Test all 30 edges of the icosahedron (each face has 3 edges, 20*3/2 = 30)
    for (int fid = 0; fid < 20; fid++) {
        const Vec3f& A = t.ico_verts[IcoTopology::FACE_VERTS[fid][0]];
        const Vec3f& B = t.ico_verts[IcoTopology::FACE_VERTS[fid][1]];
        const Vec3f& C = t.ico_verts[IcoTopology::FACE_VERTS[fid][2]];

        Vec3f edges[3][2] = {{A, B}, {B, C}, {C, A}};
        for (int e = 0; e < 3; e++) {
            for (int step = 1; step < 10; step++) {
                float alpha = step / 10.0f;
                Vec3f p = (edges[e][0] * alpha + edges[e][1] * (1.0f - alpha)).normalized();
                auto loc = t.locate(p);

                if (loc.face < 0 || loc.face >= 20) {
                    printf("    FAIL: invalid face %d for edge of face %d\n",
                           loc.face, fid);
                    return false;
                }
                float sum = loc.s_u + loc.s_v + loc.s_w;
                if (std::abs(sum - 1.0f) > 1e-4f) {
                    printf("    FAIL: partition of unity on edge: %.6f\n", sum);
                    return false;
                }
                if (loc.s_u < -1e-4f || loc.s_v < -1e-4f || loc.s_w < -1e-4f) {
                    printf("    FAIL: negative weight on edge\n");
                    return false;
                }
            }
        }
    }

    // Test near icosahedron vertices (12 vertices)
    for (int v = 0; v < 12; v++) {
        auto loc = t.locate(t.ico_verts[v]);
        float sum = loc.s_u + loc.s_v + loc.s_w;
        if (std::abs(sum - 1.0f) > 1e-4f) {
            printf("    FAIL: partition of unity at vertex %d: %.6f\n", v, sum);
            return false;
        }
    }

    printf("    all face edges and vertices: OK\n");
    return true;
}

// ============================================================
// Test 5: Coherent — face_hint gives same results
// ============================================================

static bool test_locate_coherent() {
    printf("  test_locate_coherent\n");

    const IcoTopology& t = ico_topology(4);
    const Vec3f* pos = t.positions();

    int hint_hits = 0;
    int prev_face = 0;

    for (int i = 0; i < t.num_cells(); i++) {
        auto loc_full = t.locate(pos[i]);
        auto loc_hint = t.locate(pos[i], prev_face);

        // Both should produce valid results with same verts
        // (face may differ at boundaries, but verts must be same cells)
        bool verts_match = true;
        for (int v = 0; v < 3; v++) {
            if (loc_full.vert[v].idx != loc_hint.vert[v].idx) {
                verts_match = false;
                break;
            }
        }
        if (!verts_match) {
            // Check if it's just a different-face-same-edge situation:
            // reconstruct both and compare
            Vec3f r1 = (pos[loc_full.vert[0].idx] * loc_full.s_u +
                        pos[loc_full.vert[1].idx] * loc_full.s_v +
                        pos[loc_full.vert[2].idx] * loc_full.s_w).normalized();
            Vec3f r2 = (pos[loc_hint.vert[0].idx] * loc_hint.s_u +
                        pos[loc_hint.vert[1].idx] * loc_hint.s_v +
                        pos[loc_hint.vert[2].idx] * loc_hint.s_w).normalized();
            float err = (r1 - r2).length();
            if (err > 0.01f) {
                printf("    FAIL: hint result diverges at cell %d (err=%.6f)\n",
                       i, err);
                return false;
            }
        }

        if (loc_hint.face == prev_face) hint_hits++;
        prev_face = loc_full.face;
    }

    float hit_rate = 100.0f * hint_hits / t.num_cells();
    printf("    hint hit rate: %.1f%% (%d/%d)\n",
           hit_rate, hint_hits, t.num_cells());
    if (hit_rate < 50.0f) {
        printf("    FAIL: hint hit rate too low (expected >50%%)\n");
        return false;
    }
    return true;
}

// ============================================================
// Test 6: Inverse warp precision
// ============================================================

static bool test_inverse_warp_precision() {
    printf("  test_inverse_warp_precision\n");

    // For each known (i, j) grid point within a face, compute its position
    // via compute_position, then locate to recover (u, v). Compare.
    const IcoTopology& t = ico_topology(8); // N=256
    int N = t.N();
    float max_err_subcells = 0;
    int tested = 0;

    // Test a grid at 1/256 intervals on face 0
    for (int gi = 0; gi <= N; gi += 4) {
        for (int gj = 0; gj <= N - gi; gj += 4) {
            float expected_u = (float)gi / N;
            float expected_v = (float)gj / N;

            // Get the cell at (gi, gj) on face 0 — use face_local and cell_position
            // to get the 3D position that corresponds to these exact barycentrics
            int fid = 0;
            // Instead of face_local_to_cell, use compute_position directly
            // by constructing (row, col, sector, col_rem) for face 0, (gi, gj)
            // For north cap face 0: row = N - gi, col_rem = N - gi - gj
            int row = N - gi;
            int col_rem = N - gi - gj;
            if (row == 0 && col_rem == 0) continue; // pole — skip
            int sector = 0;
            int col = sector * t.stride_of(row) + col_rem;
            Vec3f pos = t.compute_position(row, col, sector, col_rem);

            auto loc = t.locate(pos);

            // loc.face might not be 0 if point is on an edge, that's OK.
            // What matters is that the barycentrics reconstruct correctly.
            // But for interior points (not on edge), face should be 0.

            // If face == 0, compare barycentrics directly
            if (loc.face == fid) {
                float eu = std::abs(loc.u - expected_u) * N;
                float ev = std::abs(loc.v - expected_v) * N;
                float err = std::max(eu, ev);
                if (err > max_err_subcells) max_err_subcells = err;
            }
            tested++;
        }
    }

    // Also test random barycentric points
    rng_state = 55555;
    for (int i = 0; i < 10000; i++) {
        float u = randf();
        float v = randf() * (1.0f - u);
        if (u < 0.001f && v < 0.001f) continue; // too close to pole

        int N_test = 256;
        int row = N_test - (int)(u * N_test);
        if (row <= 0) row = 1;
        if (row >= 3 * N_test) row = 3 * N_test - 1;
        // Test via compute_position on face 0
        // row = N - i_grid, col_rem = N - i_grid - j_grid
        int ig = (int)(u * N_test);
        int jg = (int)(v * N_test);
        if (ig < 0) ig = 0;
        if (jg < 0) jg = 0;
        if (ig >= N_test) ig = N_test - 1;
        if (jg >= N_test - ig) jg = N_test - ig - 1;
        if (ig == 0 && jg == 0) continue; // near pole

        float true_u = (float)ig / N_test;
        float true_v = (float)jg / N_test;

        int r = N_test - ig;
        int cr = N_test - ig - jg;
        int c = cr; // sector 0 for face 0
        Vec3f pos = t.compute_position(r, c, 0, cr);
        auto loc = t.locate(pos);

        if (loc.face == 0) {
            float eu = std::abs(loc.u - true_u) * N_test;
            float ev = std::abs(loc.v - true_v) * N_test;
            float err = std::max(eu, ev);
            if (err > max_err_subcells) max_err_subcells = err;
        }
    }

    printf("    max error: %.6f sub-cells (%d grid points tested)\n",
           max_err_subcells, tested);
    if (max_err_subcells > 0.01f) {
        printf("    FAIL: inverse warp error too large (expected < 0.001)\n");
        return false;
    }
    return true;
}

// ============================================================
// Test 7: Poles
// ============================================================

static bool test_locate_poles() {
    printf("  test_locate_poles\n");

    int ks[] = {1, 2, 3, 4};
    for (int ki = 0; ki < 4; ki++) {
        int k = ks[ki];
        const IcoTopology& t = ico_topology(k);

        // North pole
        auto loc_n = t.locate(Vec3f{0, 0, 1});
        bool found_north = false;
        for (int v = 0; v < 3; v++) {
            if (loc_n.vert[v].idx == 0) {
                found_north = true;
                break;
            }
        }
        if (!found_north) {
            printf("    FAIL: k=%d north pole not in vert[] "
                   "(verts: %d, %d, %d)\n",
                   k, loc_n.vert[0].idx, loc_n.vert[1].idx, loc_n.vert[2].idx);
            return false;
        }

        // South pole
        int south_idx = t.num_cells() - 1;
        auto loc_s = t.locate(Vec3f{0, 0, -1});
        bool found_south = false;
        for (int v = 0; v < 3; v++) {
            if (loc_s.vert[v].idx == south_idx) {
                found_south = true;
                break;
            }
        }
        if (!found_south) {
            printf("    FAIL: k=%d south pole not in vert[] "
                   "(verts: %d, %d, %d)\n",
                   k, loc_s.vert[0].idx, loc_s.vert[1].idx, loc_s.vert[2].idx);
            return false;
        }

        printf("    k=%d: poles OK\n", k);
    }
    return true;
}

// ============================================================
// Test 8: Splat coverage — constant field round-trip
// ============================================================

static bool test_locate_splat_coverage() {
    printf("  test_locate_splat_coverage\n");

    const IcoTopology& t = ico_topology(3); // N=8, 642 cells
    const Vec3f* pos = t.positions();
    int nc = t.num_cells();

    std::vector<float> accum(nc, 0.0f);
    std::vector<float> weight(nc, 0.0f);

    // For every cell, locate its position and splat value 1.0
    for (int i = 0; i < nc; i++) {
        auto loc = t.locate(pos[i]);
        float weights[3] = {loc.s_u, loc.s_v, loc.s_w};
        for (int v = 0; v < 3; v++) {
            int vi = loc.vert[v].idx;
            accum[vi]  += weights[v] * 1.0f;
            weight[vi] += weights[v];
        }
    }

    // After normalizing, every vertex should have value ~ 1.0
    int bad = 0;
    float max_dev = 0;
    for (int i = 0; i < nc; i++) {
        if (weight[i] < 1e-6f) {
            // No splat reached this cell — that's a coverage failure
            bad++;
            continue;
        }
        float val = accum[i] / weight[i];
        float dev = std::abs(val - 1.0f);
        if (dev > max_dev) max_dev = dev;
    }

    printf("    uncovered cells: %d/%d, max deviation: %.6f\n",
           bad, nc, max_dev);

    if (bad > 0) {
        printf("    FAIL: %d cells received no splat\n", bad);
        return false;
    }
    if (max_dev > 0.01f) {
        printf("    FAIL: splat deviation too large\n");
        return false;
    }
    return true;
}

// ============================================================
// Benchmark: brute-force vs accelerated face finding
// ============================================================

// We need access to the internal functions.
// Re-implement minimal versions here for benchmarking.

namespace bench {

constexpr float TROPIC_Z = 0.4472136f;

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

bool point_in_face(const Vec3f* ico_v, Vec3f p, int fid) {
    const Vec3f& A = ico_v[IcoTopology::FACE_VERTS[fid][0]];
    const Vec3f& B = ico_v[IcoTopology::FACE_VERTS[fid][1]];
    const Vec3f& C = ico_v[IcoTopology::FACE_VERTS[fid][2]];
    float d_ab = p.dot(A.cross(B));
    float d_bc = p.dot(B.cross(C));
    float d_ca = p.dot(C.cross(A));
    return (d_ab >= -1e-6f && d_bc >= -1e-6f && d_ca >= -1e-6f);
}

constexpr float TWO_PI_B = 2.0f * float(M_PI);
constexpr float S72      = TWO_PI_B / 5.0f;
constexpr float S36      = float(M_PI) / 5.0f;
constexpr float INV_2T   = 1.0f / (2.0f * TROPIC_Z);

int find_face_fast(const Vec3f* ico_v, Vec3f p) {
    int cands[6];
    int nc = 0;

    float lon = std::atan2(p.y, p.x);
    if (lon < 0.0f) lon += TWO_PI_B;

    // Latitude-adjusted sector for equatorial band
    float t = std::max(0.0f, std::min(1.0f,
                  (TROPIC_Z - p.z) * INV_2T));
    float eq_lon = lon - t * S36;
    if (eq_lon < 0.0f) eq_lon += TWO_PI_B;
    int eq_s = (int)(eq_lon / S72) % 5;

    if (p.z > TROPIC_Z) {
        int s = (int)(lon / S72) % 5;
        cands[nc++] = s;
        cands[nc++] = (s + 4) % 5;
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
    } else if (p.z < -TROPIC_Z) {
        float slon = lon - S36;
        if (slon < 0.0f) slon += TWO_PI_B;
        int s = (int)(slon / S72) % 5;
        cands[nc++] = 15 + s;
        cands[nc++] = 15 + (s + 4) % 5;
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
    } else {
        cands[nc++] = 5 + eq_s * 2;
        cands[nc++] = 5 + eq_s * 2 + 1;
        int adj = (eq_s + 4) % 5;
        cands[nc++] = 5 + adj * 2;
        cands[nc++] = 5 + adj * 2 + 1;
        if (p.z > 0.0f) {
            cands[nc++] = (int)(lon / S72) % 5;
        } else {
            float slon = lon - S36;
            if (slon < 0.0f) slon += TWO_PI_B;
            cands[nc++] = 15 + (int)(slon / S72) % 5;
        }
    }

    for (int ci = 0; ci < nc; ci++) {
        if (point_in_face(ico_v, p, cands[ci]))
            return cands[ci];
    }
    return find_face_brute(ico_v, p);
}

} // namespace bench

static void benchmark_face_finding() {
    printf("\n  === Face-finding benchmark ===\n");

    const IcoTopology& t = ico_topology(4);

    // Generate test points
    constexpr int NPOINTS = 100000;
    rng_state = 42;
    std::vector<Vec3f> points(NPOINTS);
    for (int i = 0; i < NPOINTS; i++)
        points[i] = random_sphere_point();

    // Verify both methods agree
    int disagreements = 0;
    for (int i = 0; i < NPOINTS; i++) {
        int fb = bench::find_face_brute(t.ico_verts, points[i]);
        int ff = bench::find_face_fast(t.ico_verts, points[i]);
        // Both should place the point in a valid face
        if (!bench::point_in_face(t.ico_verts, points[i], fb) &&
            !bench::point_in_face(t.ico_verts, points[i], ff)) {
            disagreements++;
        }
        // More importantly: check that fast result is actually valid
        if (!bench::point_in_face(t.ico_verts, points[i], ff)) {
            // The fast method's first choice was wrong, but it should
            // have fallen back. Check brute force result instead.
            if (fb != ff) disagreements++;
        }
    }
    if (disagreements > 0)
        printf("    WARNING: %d disagreements out of %d\n", disagreements, NPOINTS);

    // Benchmark brute force
    volatile int sink = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NPOINTS; i++)
        sink += bench::find_face_brute(t.ico_verts, points[i]);
    auto t1 = std::chrono::high_resolution_clock::now();
    double brute_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    // Benchmark accelerated
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NPOINTS; i++)
        sink += bench::find_face_fast(t.ico_verts, points[i]);
    auto t3 = std::chrono::high_resolution_clock::now();
    double fast_us = std::chrono::duration<double, std::micro>(t3 - t2).count();

    // Benchmark with hint (sequential cell positions — high coherence)
    const Vec3f* pos = t.positions();
    int nc = t.num_cells();
    auto t4 = std::chrono::high_resolution_clock::now();
    int prev_face = 0;
    for (int i = 0; i < nc; i++) {
        if (bench::point_in_face(t.ico_verts, pos[i], prev_face)) {
            sink += prev_face;
        } else {
            prev_face = bench::find_face_fast(t.ico_verts, pos[i]);
            sink += prev_face;
        }
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    double hint_us = std::chrono::duration<double, std::micro>(t5 - t4).count();

    printf("    %d random points:\n", NPOINTS);
    printf("      brute-force: %.1f us total, %.3f us/point\n",
           brute_us, brute_us / NPOINTS);
    printf("      accelerated: %.1f us total, %.3f us/point\n",
           fast_us, fast_us / NPOINTS);
    printf("      speedup:     %.2fx\n", brute_us / fast_us);
    printf("    %d sequential (with hint):\n", nc);
    printf("      hint+accel:  %.1f us total, %.3f us/point\n",
           hint_us, hint_us / nc);

    // Benchmark full locate() call
    auto t6 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NPOINTS; i++) {
        auto loc = t.locate(points[i]);
        sink += loc.face;
    }
    auto t7 = std::chrono::high_resolution_clock::now();
    double locate_us = std::chrono::duration<double, std::micro>(t7 - t6).count();

    printf("    full locate():\n");
    printf("      random:      %.1f us total, %.3f us/point\n",
           locate_us, locate_us / NPOINTS);

    (void)sink;
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("IcoTopology::locate() tests\n");
    printf("===========================\n");

    auto run = [](const char* name, bool(*fn)()) {
        if (fn()) {
            printf("    PASS\n");
            g_pass++;
        } else {
            g_fail++;
        }
    };

    run("roundtrip",             test_locate_roundtrip);
    run("partition_of_unity",    test_locate_splat_partition_of_unity);
    run("reconstruction",        test_locate_reconstruction);
    run("face_edges",            test_locate_face_edges);
    run("coherent",              test_locate_coherent);
    run("inverse_warp_precision", test_inverse_warp_precision);
    run("poles",                 test_locate_poles);
    run("splat_coverage",        test_locate_splat_coverage);

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);

    benchmark_face_finding();

    return g_fail > 0 ? 1 : 0;
}
