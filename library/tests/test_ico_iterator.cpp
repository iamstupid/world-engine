#include "mesh/ico_iterator.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

static int g_pass = 0, g_fail = 0;

static bool test_iterator_k(int k) {
    int N = 1 << k;
    printf("  k=%d (N=%d, cells=%d)\n", k, N, 10 * N * N + 2);

    const IcoTopology& t = ico_topology(k);

    // --- Sequential iteration ---
    auto it = IcoIterator::begin(&t);
    auto end = IcoIterator::end(&t);

    int count = 0;
    for (; it != end; ++it) {
        int idx = *it;
        if (idx != count) {
            printf("    FAIL: sequential idx expected %d got %d\n", count, idx);
            return false;
        }

        int expected_row = t.row_of(idx);
        int expected_col = t.col_of(idx);
        if (it.row() != expected_row || it.col() != expected_col) {
            printf("    FAIL: row/col at idx %d: expected (%d,%d) got (%d,%d)\n",
                   idx, expected_row, expected_col, it.row(), it.col());
            return false;
        }

        int s = t.stride_of(it.row());
        int expected_sector = (s > 0) ? (it.col() / s) : 0;
        if (it.sector() != expected_sector) {
            printf("    FAIL: sector at idx %d (row=%d, col=%d): expected %d got %d\n",
                   idx, it.row(), it.col(), expected_sector, it.sector());
            return false;
        }

        int expected_col_rem = (s > 0) ? (it.col() % s) : 0;
        if (it.col_rem() != expected_col_rem) {
            printf("    FAIL: col_rem at idx %d: expected %d got %d\n",
                   idx, expected_col_rem, it.col_rem());
            return false;
        }

        Vec3f pos = it.position();
        Vec3f expected_pos = t.cell_position(idx);
        float dx = pos.x - expected_pos.x;
        float dy = pos.y - expected_pos.y;
        float dz = pos.z - expected_pos.z;
        if (std::abs(dx) > 1e-6f || std::abs(dy) > 1e-6f || std::abs(dz) > 1e-6f) {
            printf("    FAIL: position mismatch at idx %d\n", idx);
            return false;
        }

        count++;
    }

    if (count != t.num_cells()) {
        printf("    FAIL: iterated %d cells, expected %d\n", count, t.num_cells());
        return false;
    }

    // --- Neighbor consistency ---
    for (auto it = IcoIterator::begin(&t); it != IcoIterator::end(&t); ++it) {
        int idx = *it;

        IcoIterator nbr_its[6];
        int it_cnt = it.neighbors(nbr_its);

        int nbr_idx[6];
        int t_cnt = t.compute_neighbors(it.row(), it.col(), it.sector(), it.col_rem(), nbr_idx);

        if (it_cnt != t_cnt) {
            printf("    FAIL: neighbor count at idx %d: it=%d topo=%d\n", idx, it_cnt, t_cnt);
            return false;
        }

        // Compare as sorted index sets
        int it_indices[6];
        for (int i = 0; i < it_cnt; i++) it_indices[i] = *nbr_its[i];
        std::sort(it_indices, it_indices + it_cnt);
        std::sort(nbr_idx, nbr_idx + t_cnt);

        for (int i = 0; i < it_cnt; i++) {
            if (it_indices[i] != nbr_idx[i]) {
                printf("    FAIL: neighbor set mismatch at idx %d\n", idx);
                return false;
            }
        }

        // Verify neighbor iterator states
        for (int i = 0; i < it_cnt; i++) {
            int ni = *nbr_its[i];
            int nr = t.row_of(ni);
            int nc = t.col_of(ni);
            if (nbr_its[i].row() != nr || nbr_its[i].col() != nc) {
                printf("    FAIL: neighbor iterator row/col at idx %d, nbr %d\n", idx, i);
                return false;
            }
        }
    }

    return true;
}

int main() {
    printf("IcoIterator test\n");
    printf("================\n");

    printf("sizeof(IcoIterator) = %zu\n", sizeof(IcoIterator));
    if (sizeof(IcoIterator) != 16) {
        printf("FAIL: expected 16 bytes, got %zu\n", sizeof(IcoIterator));
        return 1;
    }

    for (int k = 0; k <= 7; k++) {
        if (test_iterator_k(k)) {
            printf("    PASS\n");
            g_pass++;
        } else {
            g_fail++;
        }
    }

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
