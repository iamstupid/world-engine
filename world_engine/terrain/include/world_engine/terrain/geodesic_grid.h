#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "world_engine/terrain/terrain_types.h"

namespace world_engine::terrain {

// Icosahedral geodesic grid of frequency F.
//
// Cell layout (see docs/references/sample_ico.hpp): 3F+1 rows, 5 sectors.
//   row 0          : 1 cell (north pole)
//   rows 1..F-1    : 5*row cells (top cap, sector stride = row)
//   rows F..2F     : 5*F cells (mid band, stride = F)
//   rows 2F+1..3F-1: 5*(3F-row) cells (bottom cap, stride = 3F-row)
//   row 3F         : 1 cell (south pole)
// Total 10*F^2+2 cells; 12 pentagons (poles + 5 at row F + 5 at row 2F),
// all other cells hexagonal (6 neighbors). Neighbors are returned in
// clockwise order (viewed from outside the sphere).
//
// Geometry: cells map onto the 20 icosahedron faces through uniform
// barycentric lattice coordinates warped by a cubic polynomial
// (f(t) = t + a*t(1-t)(0.5-t) plus symmetric joint term b*u*v*w) before
// gnomonic projection; this equalizes spherical triangle areas to ~±2.4%.
// The inverse map (locate) runs a 2D Newton solve and converges in ~3
// iterations.
class GeodesicGrid {
 public:
  explicit GeodesicGrid(int frequency);

  [[nodiscard]] int frequency() const { return freq_; }
  [[nodiscard]] int cell_count() const { return cell_count_; }
  [[nodiscard]] int row_count() const { return 3 * freq_ + 1; }

  // Indexing.
  [[nodiscard]] int row_width(int row) const;
  [[nodiscard]] int row_offset(int row) const { return row_offsets_[row]; }
  [[nodiscard]] int index(int row, int col) const;  // col wraps modulo row width
  [[nodiscard]] std::pair<int, int> row_col(int index) const;
  // Sector stride of a row (row width / 5); 0 for pole rows.
  [[nodiscard]] int sector_stride(int row) const;

  // Neighbors in clockwise order. Returns the neighbor count (5 or 6);
  // out[count..5] are left untouched.
  int neighbors(int cell, std::array<int, 6>& out) const;
  [[nodiscard]] int neighbor_count(int cell) const;
  [[nodiscard]] bool is_pentagon(int cell) const;

  // Geometry (precomputed).
  [[nodiscard]] const Vec3d& cell_center(int cell) const { return centers_[cell]; }
  [[nodiscard]] double cell_area_sr(int cell) const { return areas_[cell]; }
  [[nodiscard]] const std::vector<Vec3d>& centers() const { return centers_; }

  // Containing lattice triangle of an arbitrary unit direction, with planar
  // interpolation weights (sum to 1). Thread-safe.
  struct BaryLookup {
    std::array<int, 3> cell{};
    std::array<double, 3> weight{};
  };
  [[nodiscard]] BaryLookup locate(const Vec3d& unit_dir) const;

  // Cell of maximum weight for a direction (convenience).
  [[nodiscard]] int locate_cell(const Vec3d& unit_dir) const;

 private:
  struct Face {
    // Corner directions (unit) and inverse of the [A B C] column matrix for
    // gnomonic barycentric extraction.
    Vec3d a, b, c;
    double inv[9];
    int zone = 0;    // 0=top, 1=mid_lower, 2=mid_upper, 3=bottom
    int sector = 0;  // 0..4
  };

  [[nodiscard]] Vec3d lattice_point(int zone, int sector, double x, double y) const;
  void cell_face_coords(int row, int col, int& zone, int& sector, double& x,
                        double& y) const;
  [[nodiscard]] int lattice_cell_index(int zone, int sector, int i, int j) const;

  int freq_ = 0;
  int cell_count_ = 0;
  std::vector<int> row_offsets_;   // 3F+2 entries (last = cell_count)
  std::vector<Vec3d> vertices_;    // 12 icosahedron vertices
  std::vector<Face> faces_;        // 20 faces
  std::vector<Vec3d> centers_;
  std::vector<double> areas_;
};

}  // namespace world_engine::terrain
