#include "mesh/ico_topology.hpp"
#include "mesh/ico_iterator.hpp"
#include <cstdio>
#include <chrono>
#include <cstdint>

using Clock = std::chrono::high_resolution_clock;

// Prevent dead-code elimination
static volatile int sink;

// ============================================================
// Benchmark: compute_neighbors_full (all neighbors per cell)
// ============================================================

static double bench_compute_neighbors(const IcoTopology& topo, int iters) {
    int total_rows  = topo.num_rows();
    const int* rw = topo.row_width();

    int checksum = 0;
    auto t0 = Clock::now();

    for (int it = 0; it < iters; it++) {
        int sector = 0, col_rem = 0;
        for (int r = 0; r < total_rows; r++) {
            int w = rw[r];
            int s = topo.stride_of(r);
            sector = 0; col_rem = 0;
            for (int c = 0; c < w; c++) {
                IcoTopology::NeighborRC nbrs[6];
                int cnt = topo.compute_neighbors_full(r, c, sector, col_rem, nbrs);
                checksum += cnt;
                for (int i = 0; i < cnt; i++) checksum ^= nbrs[i].idx;
                col_rem++;
                if (s > 0 && col_rem >= s) { col_rem = 0; sector++; }
            }
        }
    }

    auto t1 = Clock::now();
    sink = checksum;
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    return elapsed / iters;
}

// ============================================================
// Benchmark: neighbor_at (single-direction random walk)
// ============================================================

static double bench_walk(const IcoTopology& topo, int steps) {
    // Start at an equatorial cell (non-pole, non-vertex)
    int N = topo.N();
    int start_row = N + N / 2;
    int start_col = N / 2;
    int start_sector = start_col / topo.stride_of(start_row);

    IcoIterator it(&topo, start_sector, topo.row_start()[start_row] + start_col,
                   start_row, start_col);

    int checksum = 0;

    // Use a simple deterministic sequence for direction choices
    auto t0 = Clock::now();

    for (int s = 0; s < steps; s++) {
        int nc = it.neighbor_count();
        int dir = s % nc;  // deterministic but covers all directions
        it.walk(dir);
        checksum ^= it.idx();
    }

    auto t1 = Clock::now();
    sink = checksum;
    return std::chrono::duration<double>(t1 - t0).count();
}

// ============================================================
// Benchmark: neighbor_count (O(1) classification)
// ============================================================

static double bench_neighbor_count(const IcoTopology& topo, int iters) {
    int total_rows  = topo.num_rows();
    const int* rw = topo.row_width();

    int checksum = 0;
    auto t0 = Clock::now();

    for (int it = 0; it < iters; it++) {
        int sector = 0, col_rem = 0;
        for (int r = 0; r < total_rows; r++) {
            int w = rw[r];
            int s = topo.stride_of(r);
            sector = 0; col_rem = 0;
            for (int c = 0; c < w; c++) {
                checksum += topo.neighbor_count(r, c, sector, col_rem);
                col_rem++;
                if (s > 0 && col_rem >= s) { col_rem = 0; sector++; }
            }
        }
    }

    auto t1 = Clock::now();
    sink = checksum;
    return std::chrono::duration<double>(t1 - t0).count() / iters;
}

// ============================================================
// Benchmark: IcoIterator sequential walk with neighbor access
// ============================================================

static double bench_iterator_neighbors(const IcoTopology& topo, int iters) {
    int checksum = 0;
    auto t0 = Clock::now();

    for (int it = 0; it < iters; it++) {
        for (auto cell = IcoIterator::begin(&topo);
             cell != IcoIterator::end(&topo); ++cell) {
            IcoIterator nbrs[6];
            int cnt = cell.neighbors(nbrs);
            checksum += cnt;
            for (int i = 0; i < cnt; i++) checksum ^= nbrs[i].idx();
        }
    }

    auto t1 = Clock::now();
    sink = checksum;
    return std::chrono::duration<double>(t1 - t0).count() / iters;
}

int main() {
    printf("Neighbor Benchmark\n");
    printf("==================\n\n");

    struct Run { int k; int iters_bulk; int walk_steps; };
    Run runs[] = {
        { 5, 100, 1'000'000},
        { 7, 10,  1'000'000},
        { 9, 1,   1'000'000},
        {10, 1,   1'000'000},
    };

    printf("%-4s  %10s  %14s  %14s  %14s  %14s  %14s\n",
           "k", "cells",
           "all_nbrs/cell", "count/cell", "iter+nbrs/cell",
           "walk/step", "walk Mstep/s");
    printf("----  ----------  --------------  --------------  "
           "--------------  --------------  --------------\n");

    for (auto& r : runs) {
        const IcoTopology& topo = ico_topology(r.k);
        int cells = topo.num_cells();

        // Warm up
        bench_compute_neighbors(topo, 1);

        double t_bulk  = bench_compute_neighbors(topo, r.iters_bulk);
        double t_count = bench_neighbor_count(topo, r.iters_bulk);
        double t_iter  = bench_iterator_neighbors(topo, r.iters_bulk);
        double t_walk  = bench_walk(topo, r.walk_steps);

        double ns_bulk  = t_bulk  / cells * 1e9;
        double ns_count = t_count / cells * 1e9;
        double ns_iter  = t_iter  / cells * 1e9;
        double ns_walk  = t_walk  / r.walk_steps * 1e9;
        double mstep_s  = r.walk_steps / t_walk / 1e6;

        printf("%-4d  %10d  %11.1f ns  %11.1f ns  %11.1f ns  %11.1f ns  %11.1f\n",
               r.k, cells, ns_bulk, ns_count, ns_iter, ns_walk, mstep_s);
    }

    printf("\n");
    return 0;
}
