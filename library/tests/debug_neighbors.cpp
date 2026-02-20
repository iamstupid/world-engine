#include <cstdio>
#include <cmath>
#include "mesh/icosahedral_geodesic.hpp"

int main() {
    int N = 2;
    IcoMesh mesh(N);
    printf("N=%d, cells=%d, rows=%d\n", N, mesh.num_cells(), mesh.num_rows());

    // Print row table
    for (int r = 0; r < mesh.num_rows(); r++) {
        printf("  row %d: start=%d width=%d\n", r, mesh.row_start(r), mesh.row_width(r));
    }

    // Check symmetry with verbose output
    for (int i = 0; i < mesh.num_cells(); i++) {
        Adjacency nbrs[6];
        int count;
        mesh.neighbors(i, nbrs, &count);

        printf("cell %d (row=%d col=%d) neighbors[%d]:", i, mesh.row_of(i), mesh.col_of(i), count);
        for (int j = 0; j < count; j++) printf(" %d", nbrs[j].idx);
        printf("\n");
        fflush(stdout);

        for (int j = 0; j < count; j++) {
            Adjacency rev[6];
            int rc;
            mesh.neighbors(nbrs[j].idx, rev, &rc);
            bool found = false;
            for (int k = 0; k < rc; k++) {
                if (rev[k].idx == i) { found = true; break; }
            }
            if (!found) {
                printf("  SYMMETRY FAIL: cell %d -> %d but %d does NOT -> %d\n", i, nbrs[j].idx, nbrs[j].idx, i);
                printf("  cell %d neighbors:", nbrs[j].idx);
                for (int k = 0; k < rc; k++) printf(" %d", rev[k].idx);
                printf("\n");
                fflush(stdout);
            }
        }
    }

    printf("Done.\n");
    return 0;
}
