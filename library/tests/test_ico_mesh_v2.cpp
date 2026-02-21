#include "mesh/ico_mesh.hpp"
#include <cstdio>
#include <cmath>

static_assert(Mesh<IcoMesh<float>>, "IcoMesh<float> must satisfy Mesh concept");
static_assert(Mesh<IcoMesh<uint8_t>>, "IcoMesh<uint8_t> must satisfy Mesh concept");
static_assert(Mesh<IcoMesh<double>>, "IcoMesh<double> must satisfy Mesh concept");

static int g_pass = 0, g_fail = 0;

static bool test_container() {
    printf("  container ops\n");
    const IcoTopology& t = ico_topology(3); // k=3, N=8, 642 cells
    IcoMesh<float> mesh(&t, "test");

    if (mesh.num_cells() != t.num_cells()) {
        printf("    FAIL: num_cells %d != %d\n", mesh.num_cells(), t.num_cells());
        return false;
    }
    if (mesh.N() != t.N()) {
        printf("    FAIL: N mismatch\n");
        return false;
    }
    if (mesh.k() != t.k()) {
        printf("    FAIL: k mismatch\n");
        return false;
    }
    if (mesh.name() != "test") {
        printf("    FAIL: name mismatch\n");
        return false;
    }

    // Write and read back
    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i);
    for (int i = 0; i < mesh.num_cells(); i++) {
        if (mesh[i] != static_cast<float>(i)) {
            printf("    FAIL: data mismatch at %d\n", i);
            return false;
        }
    }

    return true;
}

static bool test_k_constructor() {
    printf("  k constructor\n");
    IcoMesh<float> mesh(4, "from_k"); // k=4, N=16
    if (mesh.N() != 16 || mesh.k() != 4) {
        printf("    FAIL: k constructor N=%d k=%d\n", mesh.N(), mesh.k());
        return false;
    }
    return true;
}

static bool test_topology_sharing() {
    printf("  topology sharing\n");
    IcoMesh<float> a(5, "a");
    IcoMesh<float> b(5, "b");
    if (&a.topology() != &b.topology()) {
        printf("    FAIL: different topology objects for same k\n");
        return false;
    }
    return true;
}

static bool test_iteration() {
    printf("  iteration with data access\n");
    const IcoTopology& t = ico_topology(3);
    IcoMesh<float> mesh(&t, "iter_test");

    // Fill via index
    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i * 0.01f);

    // Read via iterator
    int count = 0;
    for (auto it = mesh.begin(); it != mesh.end(); ++it) {
        float expected = static_cast<float>(it.cell_index() * 0.01f);
        if (std::abs(*it - expected) > 1e-6f) {
            printf("    FAIL: iterator data mismatch at cell %d\n", it.cell_index());
            return false;
        }

        // Position should match topology
        Vec3f p = it.position();
        Vec3f expected_p = t.cell_position(it.cell_index());
        float dx = p.x - expected_p.x, dy = p.y - expected_p.y, dz = p.z - expected_p.z;
        if (std::abs(dx) > 1e-6f || std::abs(dy) > 1e-6f || std::abs(dz) > 1e-6f) {
            printf("    FAIL: iterator position mismatch at cell %d\n", it.cell_index());
            return false;
        }

        count++;
    }

    if (count != mesh.num_cells()) {
        printf("    FAIL: iterated %d cells, expected %d\n", count, mesh.num_cells());
        return false;
    }

    return true;
}

static bool test_stats() {
    printf("  stats\n");
    IcoMesh<float> mesh(2, "stats"); // k=2, N=4, 162 cells

    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i) / mesh.num_cells();

    float lo = mesh.min_val();
    float hi = mesh.max_val();
    float mean = mesh.mean_val();

    if (std::abs(lo) > 1e-6f) {
        printf("    FAIL: min_val = %f, expected ~0\n", lo);
        return false;
    }
    float expected_hi = static_cast<float>(mesh.num_cells() - 1) / mesh.num_cells();
    if (std::abs(hi - expected_hi) > 1e-6f) {
        printf("    FAIL: max_val = %f, expected %f\n", hi, expected_hi);
        return false;
    }
    float expected_mean = 0.5f * (mesh.num_cells() - 1) / mesh.num_cells();
    if (std::abs(mean - expected_mean) > 1e-3f) {
        printf("    FAIL: mean_val = %f, expected ~%f\n", mean, expected_mean);
        return false;
    }

    // Test shift
    mesh.shift(-mean);
    float new_mean = mesh.mean_val();
    if (std::abs(new_mean) > 1e-3f) {
        printf("    FAIL: mean after shift = %f, expected ~0\n", new_mean);
        return false;
    }

    // Test rescale
    IcoMesh<float> mesh2(2, "rescale");
    for (int i = 0; i < mesh2.num_cells(); i++)
        mesh2[i] = static_cast<float>(i);
    mesh2.rescale(-1.0f, 1.0f);
    if (std::abs(mesh2.min_val() - (-1.0f)) > 1e-6f ||
        std::abs(mesh2.max_val() - 1.0f) > 1e-6f) {
        printf("    FAIL: rescale min=%f max=%f\n", mesh2.min_val(), mesh2.max_val());
        return false;
    }

    return true;
}

static bool test_neighbor_walk() {
    printf("  neighbor walk via DataIterator\n");
    IcoMesh<float> mesh(3, "walk"); // k=3, N=8

    for (int i = 0; i < mesh.num_cells(); i++)
        mesh[i] = static_cast<float>(i);

    auto it = mesh.begin();
    // Skip to a non-pole cell
    for (int i = 0; i < 10; i++) ++it;

    IcoMesh<float>::iterator nbrs[6];
    int cnt = it.neighbors(nbrs);

    if (cnt != 5 && cnt != 6) {
        printf("    FAIL: neighbor count = %d (expected 5 or 6)\n", cnt);
        return false;
    }

    // Each neighbor should have valid data
    for (int i = 0; i < cnt; i++) {
        int ni = nbrs[i].cell_index();
        if (std::abs(*nbrs[i] - static_cast<float>(ni)) > 1e-6f) {
            printf("    FAIL: neighbor data mismatch at neighbor %d\n", i);
            return false;
        }
    }

    return true;
}

int main() {
    printf("IcoMesh<T> v2 test\n");
    printf("==================\n");

    auto run = [](bool (*fn)()) {
        if (fn()) { printf("    PASS\n"); g_pass++; }
        else { g_fail++; }
    };

    run(test_container);
    run(test_k_constructor);
    run(test_topology_sharing);
    run(test_iteration);
    run(test_stats);
    run(test_neighbor_walk);

    printf("\n%d/%d passed.\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
