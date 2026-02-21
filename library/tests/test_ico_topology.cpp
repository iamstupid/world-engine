#include "legacy/mesh/icosahedral_geodesic.hpp"
#include "mesh/ico_topology.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

static int g_pass = 0, g_fail = 0;

static bool test_k(int k) {
    int N = 1 << k;
    printf("  k=%d (N=%d, cells=%d)\n", k, N, 10 * N * N + 2);

    legacy::IcoMesh v1(N);
    const IcoTopology& v2 = ico_topology(k);

    // Dimensions
    if (v1.num_cells() != v2.num_cells()) {
        printf("    FAIL: num_cells v1=%d v2=%d\n", v1.num_cells(), v2.num_cells());
        return false;
    }
    if (v1.num_rows() != v2.num_rows()) {
        printf("    FAIL: num_rows v1=%d v2=%d\n", v1.num_rows(), v2.num_rows());
        return false;
    }

    // Row table
    for (int r = 0; r < v2.num_rows(); r++) {
        if (v1.row_start(r) != v2.row_start()[r]) {
            printf("    FAIL: row_start mismatch at row %d: v1=%d v2=%d\n",
                   r, v1.row_start(r), v2.row_start()[r]);
            return false;
        }
        if (v1.row_width(r) != v2.row_width()[r]) {
            printf("    FAIL: row_width mismatch at row %d: v1=%d v2=%d\n",
                   r, v1.row_width(r), v2.row_width()[r]);
            return false;
        }
    }

    // Positions
    const auto& v1_pos = v1.positions();
    const Vec3f* v2_pos = v2.positions();
    for (int i = 0; i < v2.num_cells(); i++) {
        Vec3f p1 = v1_pos[i];
        Vec3f p2 = v2_pos[i];
        float dx = p1.x - p2.x, dy = p1.y - p2.y, dz = p1.z - p2.z;
        if (std::abs(dx) > 1e-6f || std::abs(dy) > 1e-6f || std::abs(dz) > 1e-6f) {
            printf("    FAIL: position mismatch at cell %d (row=%d, col=%d)\n",
                   i, v2.row_of(i), v2.col_of(i));
            printf("      v1=(%f, %f, %f)\n", p1.x, p1.y, p1.z);
            printf("      v2=(%f, %f, %f)\n", p2.x, p2.y, p2.z);
            return false;
        }
    }

    // Neighbors
    for (int i = 0; i < v2.num_cells(); i++) {
        int r = v2.row_of(i);
        int c = v2.col_of(i);
        int s = v2.stride_of(r);
        int sector = 0, col_rem = 0;
        if (s > 0) {
            sector = c / s;
            col_rem = c - sector * s;
        }

        int v1_nbrs[6], v2_nbrs[6];
        int v1_cnt = v1.compute_neighbors(r, c, v1_nbrs);
        int v2_cnt = v2.compute_neighbors(r, c, sector, col_rem, v2_nbrs);

        if (v1_cnt != v2_cnt) {
            printf("    FAIL: neighbor count at cell %d (row=%d, col=%d): v1=%d v2=%d\n",
                   i, r, c, v1_cnt, v2_cnt);
            return false;
        }

        std::sort(v1_nbrs, v1_nbrs + v1_cnt);
        std::sort(v2_nbrs, v2_nbrs + v2_cnt);
        for (int j = 0; j < v1_cnt; j++) {
            if (v1_nbrs[j] != v2_nbrs[j]) {
                printf("    FAIL: neighbor set mismatch at cell %d (row=%d, col=%d)\n",
                       i, r, c);
                printf("      v1:");
                for (int x = 0; x < v1_cnt; x++) printf(" %d", v1_nbrs[x]);
                printf("\n      v2:");
                for (int x = 0; x < v2_cnt; x++) printf(" %d", v2_nbrs[x]);
                printf("\n");
                return false;
            }
        }
    }

    // Faces
    const auto& v1_faces = v1.faces();
    const int* v2_faces = v2.faces();
    int num_tris = v2.num_faces_tri();
    if ((int)v1_faces.size() != num_tris) {
        printf("    FAIL: face count v1=%d v2=%d\n", (int)v1_faces.size(), num_tris);
        return false;
    }
    for (int t = 0; t < num_tris; t++) {
        if (v1_faces[t][0] != v2_faces[t * 3] ||
            v1_faces[t][1] != v2_faces[t * 3 + 1] ||
            v1_faces[t][2] != v2_faces[t * 3 + 2]) {
            printf("    FAIL: face mismatch at triangle %d: "
                   "v1=(%d,%d,%d) v2=(%d,%d,%d)\n", t,
                   v1_faces[t][0], v1_faces[t][1], v1_faces[t][2],
                   v2_faces[t * 3], v2_faces[t * 3 + 1], v2_faces[t * 3 + 2]);
            return false;
        }
    }

    // Factory returns same object
    const IcoTopology& v2b = ico_topology(k);
    if (&v2 != &v2b) {
        printf("    FAIL: factory returned different object for same k\n");
        return false;
    }

    return true;
}

int main() {
    printf("IcoTopology v2 equivalence test\n");
    printf("================================\n");

    for (int k = 0; k <= 7; k++) {
        if (test_k(k)) {
            printf("    PASS\n");
            g_pass++;
        } else {
            g_fail++;
        }
    }

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
