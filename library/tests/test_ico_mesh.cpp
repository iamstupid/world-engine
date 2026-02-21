#include <cstdio>
#include <cmath>
#include <cassert>
#include <set>
#include <algorithm>
#include "legacy/mesh/icosahedral_geodesic.hpp"
using namespace legacy;

#define TEST(name) printf("  TEST: %s ... ", #name);
#define PASS() printf("PASSED\n");

void test_cell_count() {
    TEST(cell_count);
    struct { int N; int expected; } cases[] = {
        {1, 12}, {2, 42}, {3, 92}, {4, 162}, {10, 1002}, {100, 100002}
    };
    for (auto& tc : cases) {
        IcoMesh mesh(tc.N);
        assert(mesh.num_cells() == tc.expected);
    }
    PASS();
}

void test_on_unit_sphere() {
    TEST(on_unit_sphere);
    int Ns[] = {1, 2, 4, 10};
    for (int N : Ns) {
        IcoMesh mesh(N);
        for (int i = 0; i < mesh.num_cells(); i++) {
            Vec3f p = mesh.cell_position(i);
            float len = p.length();
            assert(std::abs(len - 1.0f) < 1e-4f);
        }
    }
    PASS();
}

void test_north_pole() {
    TEST(north_pole);
    IcoMesh mesh(4);
    Vec3f north = mesh.cell_position(0);
    assert(north.z > 0.99f);
    PASS();
}

void test_south_pole() {
    TEST(south_pole);
    IcoMesh mesh(4);
    Vec3f south = mesh.cell_position(mesh.num_cells() - 1);
    assert(south.z < -0.99f);
    PASS();
}

void test_N1_matches_icosahedron() {
    TEST(N1_matches_icosahedron);
    IcoMesh mesh(1);
    assert(mesh.num_cells() == 12);
    // All 12 positions should be on unit sphere
    for (int i = 0; i < 12; i++) {
        Vec3f p = mesh.cell_position(i);
        assert(std::abs(p.length() - 1.0f) < 1e-4f);
    }
    PASS();
}

void test_pentagon_count() {
    TEST(pentagon_count);
    int Ns[] = {2, 4, 10, 20};
    for (int N : Ns) {
        IcoMesh mesh(N);
        int pentagon_count = 0;
        for (int i = 0; i < mesh.num_cells(); i++) {
            int nc = mesh.neighbor_count(i);
            assert(nc == 5 || nc == 6);
            if (nc == 5) pentagon_count++;
        }
        assert(pentagon_count == 12);
    }
    PASS();
}

void test_neighbor_symmetry() {
    TEST(neighbor_symmetry);
    int Ns[] = {2, 4, 10};
    for (int N : Ns) {
        IcoMesh mesh(N);
        for (int i = 0; i < mesh.num_cells(); i++) {
            Adjacency nbrs[6];
            int count;
            mesh.neighbors(i, nbrs, &count);
            for (int j = 0; j < count; j++) {
                Adjacency rev[6];
                int rc;
                mesh.neighbors(nbrs[j].idx, rev, &rc);
                bool found = false;
                for (int k = 0; k < rc; k++) {
                    if (rev[k].idx == i) { found = true; break; }
                }
                if (!found) {
                    printf("\nFAILED: cell %d has neighbor %d but not vice versa (N=%d)\n",
                           i, nbrs[j].idx, N);
                    printf("  cell %d row=%d col=%d neighbors:", i, mesh.row_of(i), mesh.col_of(i));
                    for (int k = 0; k < count; k++) printf(" %d", nbrs[k].idx);
                    printf("\n");
                    printf("  cell %d row=%d col=%d neighbors:", nbrs[j].idx,
                           mesh.row_of(nbrs[j].idx), mesh.col_of(nbrs[j].idx));
                    for (int k = 0; k < rc; k++) printf(" %d", rev[k].idx);
                    printf("\n");
                    assert(false && "Neighbor symmetry violated!");
                }
            }
        }
    }
    PASS();
}

void test_no_duplicate_neighbors() {
    TEST(no_duplicate_neighbors);
    int Ns[] = {2, 4, 10};
    for (int N : Ns) {
        IcoMesh mesh(N);
        for (int i = 0; i < mesh.num_cells(); i++) {
            Adjacency nbrs[6];
            int count;
            mesh.neighbors(i, nbrs, &count);
            std::set<int> seen;
            for (int j = 0; j < count; j++) {
                assert(nbrs[j].idx != i && "Self-neighbor!");
                assert(seen.find(nbrs[j].idx) == seen.end() && "Duplicate neighbor!");
                seen.insert(nbrs[j].idx);
            }
        }
    }
    PASS();
}

void test_area_sum() {
    TEST(area_sum);
    fflush(stdout);
    int Ns[] = {4, 10};
    for (int N : Ns) {
        IcoMesh mesh(N);
        float total_area = 0;
        for (int i = 0; i < mesh.num_cells(); i++) {
            total_area += mesh.cell_area(i);
        }
        float expected = 4.0f * M_PI;
        fprintf(stderr, "(N=%d sum=%.4f expected=%.4f) ", N, total_area, expected);
        if (std::abs(total_area - expected) > 0.5f) {
            fprintf(stderr, "\nFAILED: area sum %.4f vs expected %.4f\n", total_area, expected);
            assert(false);
        }
    }
    PASS();
}

void test_area_uniformity() {
    TEST(area_uniformity);
    IcoMesh mesh(10);
    float min_area = 1e9, max_area = 0;
    for (int i = 0; i < mesh.num_cells(); i++) {
        float a = mesh.cell_area(i);
        min_area = std::min(min_area, a);
        max_area = std::max(max_area, a);
    }
    float ratio = max_area / min_area;
    printf("(ratio=%.3f) ", ratio);
    // With face-aligned triangulation, cell area ratio ~1.24 (pentagons have 5/6 area)
    // Hex-only ratio ~1.04, triangle ratio ~1.05 (matching optimizer)
    assert(ratio < 1.30f && "Area too non-uniform");
    PASS();
}

void test_iterator_coverage() {
    TEST(iterator_coverage);
    int Ns[] = {1, 2, 4, 10};
    for (int N : Ns) {
        IcoMesh mesh(N);
        int visit_count = 0;
        std::set<int> visited;
        for (auto it = mesh.begin(); it != mesh.end(); ++it) {
            visited.insert(*it);
            visit_count++;
        }
        assert(visit_count == mesh.num_cells());
        assert((int)visited.size() == mesh.num_cells());
    }
    PASS();
}

void test_iterator_position_matches() {
    TEST(iterator_position_matches_random_access);
    IcoMesh mesh(4);
    for (auto it = mesh.begin(); it != mesh.end(); ++it) {
        Vec3f pos_iter = it.position();
        Vec3f pos_rand = mesh.cell_position(*it);
        float dx = pos_iter.x - pos_rand.x;
        float dy = pos_iter.y - pos_rand.y;
        float dz = pos_iter.z - pos_rand.z;
        float err = std::sqrt(dx*dx + dy*dy + dz*dz);
        assert(err < 1e-5f);
    }
    PASS();
}

void test_analytic_vs_face_adjacency() {
    TEST(analytic_vs_face_adjacency);
    fflush(stdout);
    int Ns[] = {1, 2, 3, 4, 5, 10, 20};
    for (int N : Ns) {
        IcoMesh mesh(N);
        // Build face-based adjacency for comparison
        const auto& face_list = mesh.faces();
        std::vector<std::set<int>> face_adj(mesh.num_cells());
        for (const auto& tri : face_list) {
            face_adj[tri[0]].insert(tri[1]); face_adj[tri[1]].insert(tri[0]);
            face_adj[tri[1]].insert(tri[2]); face_adj[tri[2]].insert(tri[1]);
            face_adj[tri[2]].insert(tri[0]); face_adj[tri[0]].insert(tri[2]);
        }

        int mismatches = 0;
        for (int idx = 0; idx < mesh.num_cells(); idx++) {
            int row = mesh.row_of(idx);
            int col = mesh.col_of(idx);
            int analytic[6];
            int cnt = mesh.compute_neighbors(row, col, analytic);

            std::set<int> analytic_set(analytic, analytic + cnt);
            if (analytic_set != face_adj[idx]) {
                if (mismatches < 10) {
                    fprintf(stderr, "MISMATCH N=%d cell %d (row=%d col=%d):\n", N, idx, row, col);
                    fprintf(stderr, "  analytic(%d):", cnt);
                    for (int k = 0; k < cnt; k++) fprintf(stderr, " %d", analytic[k]);
                    fprintf(stderr, "\n  face-based(%d):", (int)face_adj[idx].size());
                    for (int n : face_adj[idx]) fprintf(stderr, " %d", n);
                    fprintf(stderr, "\n");
                    for (int n : face_adj[idx]) {
                        if (analytic_set.find(n) == analytic_set.end())
                            fprintf(stderr, "  MISSING: %d (row=%d col=%d)\n", n, mesh.row_of(n), mesh.col_of(n));
                    }
                    for (int k = 0; k < cnt; k++) {
                        if (face_adj[idx].find(analytic[k]) == face_adj[idx].end())
                            fprintf(stderr, "  EXTRA: %d (row=%d col=%d)\n", analytic[k], mesh.row_of(analytic[k]), mesh.col_of(analytic[k]));
                    }
                }
                mismatches++;
            }
        }
        if (mismatches > 0) {
            fprintf(stderr, "FAILED: %d mismatches out of %d cells for N=%d\n",
                   mismatches, mesh.num_cells(), N);
            fflush(stderr);
            assert(false && "Analytic neighbors don't match face-based!");
        }
        fprintf(stderr, "(N=%d OK) ", N);
    }
    PASS();
}

int main() {
    printf("=== IcoMesh Unit Tests ===\n");

    test_cell_count();
    test_on_unit_sphere();
    test_north_pole();
    test_south_pole();
    test_N1_matches_icosahedron();
    test_pentagon_count();
    test_neighbor_symmetry();
    test_no_duplicate_neighbors();
    test_area_sum();
    test_area_uniformity();
    test_iterator_coverage();
    test_iterator_position_matches();
    test_analytic_vs_face_adjacency();

    printf("\nAll IcoMesh tests PASSED!\n");
    return 0;
}
