#include "tectonics/plate_assign.hpp"
#include "noise/noise_generator.hpp"
#include "io/serialize.hpp"
#include <cassert>
#include <cstdio>
#include <set>
#include <vector>
#include <queue>

static void test_all_cells_assigned() {
    const int k = 4; // N=16, 2562 cells
    const IcoTopology& topo = ico_topology(k);

    PlateAssignParams params;
    params.num_plates = 12;
    params.seed = 42;

    auto result = assign_plates(&topo, params);
    assert(result.num_cells() == topo.num_cells());

    for (int i = 0; i < result.num_cells(); i++) {
        assert(result[i] < params.num_plates);
    }
    printf("  PASS: all cells assigned with valid plate IDs\n");
}

static void test_every_plate_has_cells() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);

    PlateAssignParams params;
    params.num_plates = 12;
    params.seed = 42;

    auto result = assign_plates(&topo, params);

    std::vector<int> plate_count(params.num_plates, 0);
    for (int i = 0; i < result.num_cells(); i++) {
        plate_count[result[i]]++;
    }

    for (int p = 0; p < params.num_plates; p++) {
        assert(plate_count[p] > 0);
    }
    printf("  PASS: every plate has > 0 cells\n");
}

static void test_deterministic() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);

    PlateAssignParams params;
    params.num_plates = 8;
    params.seed = 123;

    auto r1 = assign_plates(&topo, params);
    auto r2 = assign_plates(&topo, params);

    for (int i = 0; i < r1.num_cells(); i++) {
        assert(r1[i] == r2[i]);
    }
    printf("  PASS: same seed produces identical output\n");
}

static void test_different_seeds() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);

    PlateAssignParams p1, p2;
    p1.num_plates = 8;
    p1.seed = 42;
    p2.num_plates = 8;
    p2.seed = 999;

    auto r1 = assign_plates(&topo, p1);
    auto r2 = assign_plates(&topo, p2);

    int diffs = 0;
    for (int i = 0; i < r1.num_cells(); i++) {
        if (r1[i] != r2[i]) diffs++;
    }
    assert(diffs > 0);
    printf("  PASS: different seeds produce different output (%d cells differ)\n", diffs);
}

static void test_spatial_contiguity() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);
    const int num_cells = topo.num_cells();

    PlateAssignParams params;
    params.num_plates = 8;
    params.seed = 42;

    auto result = assign_plates(&topo, params);

    // Build adjacency for BFS
    std::vector<int> adj_idx(num_cells * 6, -1);
    std::vector<int8_t> adj_count(num_cells);
    for (int i = 0; i < num_cells; i++) {
        int r = topo.row_of(i);
        int c = i - topo.row_start()[r];
        int s = topo.stride_of(r);
        int sector = (s > 0) ? c / s : 0;
        int col_rem = (s > 0) ? c - sector * s : 0;
        IcoTopology::NeighborRC nbrs[6];
        int cnt = topo.compute_neighbors_full(r, c, sector, col_rem, nbrs);
        adj_count[i] = static_cast<int8_t>(cnt);
        for (int d = 0; d < cnt; d++)
            adj_idx[i * 6 + d] = nbrs[d].idx;
    }

    // For each plate, BFS from one of its cells and check all cells are reachable
    for (int p = 0; p < params.num_plates; p++) {
        // Find first cell of this plate
        int start = -1;
        int total = 0;
        for (int i = 0; i < num_cells; i++) {
            if (result[i] == p) {
                if (start < 0) start = i;
                total++;
            }
        }
        assert(start >= 0);

        // BFS
        std::vector<bool> visited(num_cells, false);
        std::queue<int> q;
        q.push(start);
        visited[start] = true;
        int reached = 0;

        while (!q.empty()) {
            int cur = q.front(); q.pop();
            reached++;
            for (int d = 0; d < adj_count[cur]; d++) {
                int nbr = adj_idx[cur * 6 + d];
                if (nbr >= 0 && !visited[nbr] && result[nbr] == p) {
                    visited[nbr] = true;
                    q.push(nbr);
                }
            }
        }

        assert(reached == total);
    }
    printf("  PASS: all plates are spatially contiguous\n");
}

static void test_with_elevation() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);

    // Generate elevation to pass as bias input
    NoiseParams np;
    np.seed = 42;
    auto elevation = generate_noise<IcoMesh<float>>(&topo, np);

    PlateAssignParams params;
    params.num_plates = 12;
    params.seed = 42;
    params.ocean_bias = 2.0f;

    auto result = assign_plates(&topo, params, elevation.data());

    for (int i = 0; i < result.num_cells(); i++) {
        assert(result[i] < params.num_plates);
    }
    printf("  PASS: plate assignment with elevation bias works\n");
}

static void test_serialize_roundtrip() {
    const int k = 4;
    const IcoTopology& topo = ico_topology(k);

    PlateAssignParams params;
    params.num_plates = 12;
    params.seed = 42;

    auto result = assign_plates(&topo, params);

    // Serialize
    auto packet = serialize_field(result, 1);

    // Deserialize
    auto deserialized = deserialize_field<uint16_t>(packet.data(), packet.size(), &topo);

    assert(deserialized.num_cells() == result.num_cells());
    for (int i = 0; i < result.num_cells(); i++) {
        assert(deserialized[i] == result[i]);
    }
    printf("  PASS: uint16 FILD serialize/deserialize roundtrip\n");
}

int main() {
    printf("=== test_plate_assign ===\n");
    test_all_cells_assigned();
    test_every_plate_has_cells();
    test_deterministic();
    test_different_seeds();
    test_spatial_contiguity();
    test_with_elevation();
    test_serialize_roundtrip();
    printf("All tests passed.\n");
    return 0;
}
