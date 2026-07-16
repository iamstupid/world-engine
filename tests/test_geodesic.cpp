#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "world_engine/terrain/geodesic_grid.h"

namespace {

using world_engine::terrain::GeodesicGrid;
using world_engine::terrain::Vec3d;

int g_failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_failures;
  }
}

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double angular_dist(const Vec3d& a, const Vec3d& b) {
  return std::acos(std::min(1.0, std::max(-1.0, dot(a, b))));
}

bool contains(const std::array<int, 6>& arr, int count, int value) {
  for (int i = 0; i < count; ++i) {
    if (arr[i] == value) {
      return true;
    }
  }
  return false;
}

void test_frequency(int f) {
  const GeodesicGrid grid(f);
  char buf[128];

  std::snprintf(buf, sizeof(buf), "F=%d cell count == 10F^2+2", f);
  expect(grid.cell_count() == 10 * f * f + 2, buf);

  int pentagons = 0;
  double area_sum = 0.0;
  double area_min = 1e30;
  double area_max = 0.0;
  std::array<int, 6> nb{};
  std::array<int, 6> nb2{};

  for (int c = 0; c < grid.cell_count(); ++c) {
    const int m = grid.neighbors(c, nb);
    expect(m == 5 || m == 6, "neighbor count is 5 or 6");
    expect(m == grid.neighbor_count(c), "neighbor_count matches neighbors()");
    expect((m == 5) == grid.is_pentagon(c), "is_pentagon matches count");
    if (m == 5) {
      ++pentagons;
    }

    // Validity, no self, no duplicates.
    for (int i = 0; i < m; ++i) {
      expect(nb[i] >= 0 && nb[i] < grid.cell_count(), "neighbor index in range");
      expect(nb[i] != c, "no self neighbor");
      for (int j = i + 1; j < m; ++j) {
        expect(nb[i] != nb[j], "no duplicate neighbors");
      }
    }

    // Symmetry: each neighbor lists us back.
    for (int i = 0; i < m; ++i) {
      const int m2 = grid.neighbors(nb[i], nb2);
      expect(contains(nb2, m2, c), "neighbor symmetry");
    }

    // CW order: consecutive neighbors are themselves adjacent.
    for (int i = 0; i < m; ++i) {
      const int a = nb[i];
      const int b = nb[(i + 1) % m];
      const int m2 = grid.neighbors(a, nb2);
      expect(contains(nb2, m2, b), "consecutive CW neighbors adjacent");
    }

    // Geometry sanity.
    const Vec3d& p = grid.cell_center(c);
    expect(std::abs(dot(p, p) - 1.0) < 1e-12, "center on unit sphere");
    area_sum += grid.cell_area_sr(c);
    area_min = std::min(area_min, grid.cell_area_sr(c));
    area_max = std::max(area_max, grid.cell_area_sr(c));
  }

  std::snprintf(buf, sizeof(buf), "F=%d exactly 12 pentagons (got %d)", f, pentagons);
  expect(pentagons == 12, buf);

  const double four_pi = 4.0 * 3.14159265358979323846;
  std::snprintf(buf, sizeof(buf), "F=%d cell areas sum to 4pi (rel err %.2e)", f,
                std::abs(area_sum - four_pi) / four_pi);
  expect(std::abs(area_sum - four_pi) / four_pi < 1e-9, buf);

  // Pentagons are ~5/6 the area of hexagons; warp residual adds ~5%.
  std::snprintf(buf, sizeof(buf), "F=%d cell area max/min bounded (got %.3f)", f,
                area_max / area_min);
  expect(area_max / area_min < 1.5, buf);

  // locate() roundtrip: the center of every cell locates to that cell with
  // dominant weight.
  for (int c = 0; c < grid.cell_count(); ++c) {
    const auto lk = grid.locate(grid.cell_center(c));
    double wsum = lk.weight[0] + lk.weight[1] + lk.weight[2];
    expect(std::abs(wsum - 1.0) < 1e-9, "locate weights sum to 1");
    int best = 0;
    for (int k = 1; k < 3; ++k) {
      if (lk.weight[k] > lk.weight[best]) {
        best = k;
      }
    }
    if (lk.cell[best] != c || lk.weight[best] < 0.98) {
      std::snprintf(buf, sizeof(buf), "F=%d locate roundtrip cell %d (w=%.4f got %d)",
                    f, c, lk.weight[best], lk.cell[best]);
      expect(false, buf);
    }
  }

  // locate() on random directions: weighted center approximates the query.
  unsigned rng = 12345u + static_cast<unsigned>(f);
  const double spacing = std::sqrt(four_pi / grid.cell_count());
  for (int t = 0; t < 2000; ++t) {
    rng = rng * 1664525u + 1013904223u;
    const double z = 2.0 * ((rng >> 8) / 16777216.0) - 1.0;
    rng = rng * 1664525u + 1013904223u;
    const double phi = 2.0 * 3.14159265358979323846 * ((rng >> 8) / 16777216.0);
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const Vec3d p{r * std::cos(phi), z, r * std::sin(phi)};
    const auto lk = grid.locate(p);
    Vec3d approx{0.0, 0.0, 0.0};
    for (int k = 0; k < 3; ++k) {
      expect(lk.weight[k] > -1e-9, "locate weights nonnegative");
      const Vec3d& q = grid.cell_center(lk.cell[k]);
      approx.x += lk.weight[k] * q.x;
      approx.y += lk.weight[k] * q.y;
      approx.z += lk.weight[k] * q.z;
    }
    const double norm = std::sqrt(dot(approx, approx));
    approx = {approx.x / norm, approx.y / norm, approx.z / norm};
    if (angular_dist(approx, p) > spacing) {
      std::snprintf(buf, sizeof(buf), "F=%d locate random point err %.4f > %.4f", f,
                    angular_dist(approx, p), spacing);
      expect(false, buf);
    }
  }
}

void test_multigrid_nesting() {
  const GeodesicGrid coarse(8);
  const GeodesicGrid fine(16);
  for (int c = 0; c < coarse.cell_count(); ++c) {
    const auto [row, col] = coarse.row_col(c);
    const int cf = fine.index(2 * row, 2 * col);
    const Vec3d& a = coarse.cell_center(c);
    const Vec3d& b = fine.cell_center(cf);
    const double d = std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
    expect(d < 1e-12, "multigrid nesting: coarse (r,c) == fine (2r,2c)");
    if (d >= 1e-12) {
      return;  // avoid failure spam
    }
  }
}

}  // namespace

int main() {
  for (const int f : {1, 2, 3, 4, 7, 16}) {
    test_frequency(f);
  }
  test_multigrid_nesting();
  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("test_geodesic passed\n");
  return 0;
}
