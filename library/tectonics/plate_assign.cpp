#include "plate_assign.hpp"
#include "FastNoiseLite.h"
#include <queue>
#include <random>
#include <set>
#include <cmath>
#include <limits>
#include <cstdint>

IcoMesh<uint16_t> assign_plates(const IcoTopology* topo,
                                 const PlateAssignParams& params,
                                 const float* elevation) {
    const int num_cells = topo->num_cells();

    // --- 1. Pre-build positions ---
    const Vec3f* pos = topo->positions();

    // --- 2. Pre-build adjacency table ---
    // Each cell has up to 6 neighbors. Store flat.
    std::vector<int> adj_idx(num_cells * 6, -1);
    std::vector<int8_t> adj_count(num_cells);

    for (int i = 0; i < num_cells; i++) {
        int r = topo->row_of(i);
        int c = i - topo->row_start()[r];
        int s = topo->stride_of(r);
        int sector = (s > 0) ? c / s : 0;
        int col_rem = (s > 0) ? c - sector * s : 0;

        IcoTopology::NeighborRC nbrs[6];
        int cnt = topo->compute_neighbors_full(r, c, sector, col_rem, nbrs);
        adj_count[i] = static_cast<int8_t>(cnt);
        for (int d = 0; d < cnt; d++) {
            adj_idx[i * 6 + d] = nbrs[d].idx;
        }
    }

    // --- 3. Per-vertex FBM cost weights ---
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.SetSeed(params.seed + 1000);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(params.weight_octaves);
    noise.SetFractalLacunarity(params.weight_lacunarity);
    noise.SetFractalGain(params.weight_gain);
    noise.SetFrequency(params.weight_frequency);

    std::vector<float> weight(num_cells);
    for (int i = 0; i < num_cells; i++) {
        float raw = noise.GetNoise(pos[i].x, pos[i].y, pos[i].z);
        // raw is roughly in [-1, 1], rescale to [0.5, 1.5]
        weight[i] = 1.0f + raw * 0.5f;
        // Apply ocean bias: ocean cells get higher cost, pushing boundaries into ocean
        if (elevation && elevation[i] <= 0.0f) {
            weight[i] *= params.ocean_bias;
        }
    }

    // --- 4. Seed selection (unique cells) ---
    std::mt19937 rng(static_cast<unsigned>(params.seed));
    std::uniform_int_distribution<int> dist(0, num_cells - 1);

    std::vector<int> seeds;
    seeds.reserve(params.num_plates);
    std::set<int> used;
    while (static_cast<int>(seeds.size()) < params.num_plates) {
        int idx = dist(rng);
        if (used.insert(idx).second) {
            seeds.push_back(idx);
        }
    }

    // --- 5. Multi-seed Dijkstra ---
    IcoMesh<uint16_t> result(topo, "plate_id");
    constexpr uint16_t UNASSIGNED = std::numeric_limits<uint16_t>::max();
    for (int i = 0; i < num_cells; i++) {
        result[i] = UNASSIGNED;
    }

    std::vector<float> cost(num_cells, std::numeric_limits<float>::infinity());

    // Min-heap: (cost, cell_index)
    using PQEntry = std::pair<float, int>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    for (int p = 0; p < params.num_plates; p++) {
        int s = seeds[p];
        cost[s] = 0.0f;
        result[s] = static_cast<uint16_t>(p);
        pq.push({0.0f, s});
    }

    while (!pq.empty()) {
        auto [cur_cost, cur] = pq.top();
        pq.pop();

        // Skip stale entries
        if (cur_cost > cost[cur]) continue;

        uint16_t plate = result[cur];
        int cnt = adj_count[cur];

        for (int d = 0; d < cnt; d++) {
            int nbr = adj_idx[cur * 6 + d];
            if (nbr < 0) continue;

            // Edge cost: Euclidean distance * destination weight
            Vec3f diff = pos[nbr] - pos[cur];
            float edge_dist = diff.length();
            float new_cost = cur_cost + edge_dist * weight[nbr];

            if (new_cost < cost[nbr]) {
                cost[nbr] = new_cost;
                result[nbr] = plate;
                pq.push({new_cost, nbr});
            }
        }
    }

    return result;
}
