#include <cstdio>
#include <cmath>
#include <cassert>
#include "mesh/icosahedral_geodesic.hpp"
#include "noise/noise_generator.hpp"

#define TEST(name) printf("  TEST: %s ... ", #name);
#define PASS() printf("PASSED\n");

void test_no_nan_inf() {
    TEST(no_nan_inf);
    IcoMesh mesh(100);
    NoiseParams params;
    params.seed = 42;
    Field<IcoMesh> field = generate_noise(mesh, params);

    for (int i = 0; i < field.size(); i++) {
        assert(!std::isnan(field[i]) && "NaN in noise output");
        assert(!std::isinf(field[i]) && "Inf in noise output");
    }
    PASS();
}

void test_range_after_generation() {
    TEST(range_after_generation);
    IcoMesh mesh(100);
    NoiseParams params;
    params.seed = 42;
    Field<IcoMesh> field = generate_noise(mesh, params);

    float mn = field.min_val();
    float mx = field.max_val();
    printf("(min=%.4f max=%.4f) ", mn, mx);

    // After rescale, range should be [-1, 1]
    assert(mn >= -1.01f && mn < 0.0f && "min should be in [-1, 0)");
    assert(mx > 0.0f && mx <= 1.01f && "max should be in (0, 1]");
    PASS();
}

void test_ocean_fraction() {
    TEST(ocean_fraction);
    IcoMesh mesh(100);
    NoiseParams params;
    params.seed = 42;
    params.ocean_fraction = 0.55f;
    Field<IcoMesh> field = generate_noise(mesh, params);

    int below_zero = 0;
    for (int i = 0; i < field.size(); i++) {
        if (field[i] < 0) below_zero++;
    }
    float actual_frac = (float)below_zero / field.size();
    printf("(actual=%.4f target=%.4f) ", actual_frac, params.ocean_fraction);
    assert(std::abs(actual_frac - params.ocean_fraction) < 0.05f && "Ocean fraction off");
    PASS();
}

void test_seed_determinism() {
    TEST(seed_determinism);
    IcoMesh mesh(20);
    NoiseParams params;
    params.seed = 123;

    Field<IcoMesh> f1 = generate_noise(mesh, params);
    Field<IcoMesh> f2 = generate_noise(mesh, params);

    for (int i = 0; i < f1.size(); i++) {
        assert(f1[i] == f2[i] && "Same seed should produce same output");
    }
    PASS();
}

void test_different_seed() {
    TEST(different_seed);
    IcoMesh mesh(20);
    NoiseParams p1, p2;
    p1.seed = 1;
    p2.seed = 999;

    Field<IcoMesh> f1 = generate_noise(mesh, p1);
    Field<IcoMesh> f2 = generate_noise(mesh, p2);

    int diffs = 0;
    for (int i = 0; i < f1.size(); i++) {
        if (f1[i] != f2[i]) diffs++;
    }
    printf("(diffs=%d/%d) ", diffs, f1.size());
    assert(diffs > f1.size() / 2 && "Different seeds should produce different output");
    PASS();
}

int main() {
    printf("=== Noise Generation Tests ===\n");

    test_no_nan_inf();
    test_range_after_generation();
    test_ocean_fraction();
    test_seed_determinism();
    test_different_seed();

    printf("\nAll noise tests PASSED!\n");
    return 0;
}
