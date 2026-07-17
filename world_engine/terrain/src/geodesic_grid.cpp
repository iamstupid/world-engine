#include "world_engine/terrain/geodesic_grid.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace world_engine::terrain {
namespace {

constexpr double kPi = 3.14159265358979323846;
// Warp constants validated against docs/references/sample_ico.hpp geometry
// experiments (docs/TECTONICS_PLAN.md): sub-triangle spherical area
// max/min = 1.048 vs 1.97 unwarped; Jacobian strictly positive.
constexpr double kWarpAlpha = 0.5372;
constexpr double kWarpBeta = -0.4637;

Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d mul(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d cross(const Vec3d& a, const Vec3d& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3d normalized(const Vec3d& v) {
  const double n = std::sqrt(dot(v, v));
  return {v.x / n, v.y / n, v.z / n};
}

double warp_f(double t) { return t + kWarpAlpha * t * (1.0 - t) * (0.5 - t); }
double warp_f_prime(double t) { return 1.0 + kWarpAlpha * (0.5 - 3.0 * t + 3.0 * t * t); }

// (u, v) -> warped barycentric weights (bu, bv, bw), sum 1.
void warp_bary(double u, double v, double& bu, double& bv, double& bw) {
  const double w = 1.0 - u - v;
  const double joint = kWarpBeta * u * v * w;
  double fu = warp_f(u) + joint;
  double fv = warp_f(v) + joint;
  double fw = warp_f(w) + joint;
  const double s = fu + fv + fw;
  bu = fu / s;
  bv = fv / s;
  bw = fw / s;
}

// Spherical triangle area (Oosterom-Strackee), orientation-insensitive.
double spherical_tri_area(const Vec3d& a, const Vec3d& b, const Vec3d& c) {
  const double num = std::abs(dot(a, cross(b, c)));
  const double den = 1.0 + dot(a, b) + dot(b, c) + dot(c, a);
  return 2.0 * std::atan2(num, den);
}

bool invert3x3(const Vec3d& a, const Vec3d& b, const Vec3d& c, double inv[9]) {
  // Columns are a, b, c.
  const double m[9] = {a.x, b.x, c.x, a.y, b.y, c.y, a.z, b.z, c.z};
  const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                     m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (std::abs(det) < 1e-15) {
    return false;
  }
  const double id = 1.0 / det;
  inv[0] = (m[4] * m[8] - m[5] * m[7]) * id;
  inv[1] = (m[2] * m[7] - m[1] * m[8]) * id;
  inv[2] = (m[1] * m[5] - m[2] * m[4]) * id;
  inv[3] = (m[5] * m[6] - m[3] * m[8]) * id;
  inv[4] = (m[0] * m[8] - m[2] * m[6]) * id;
  inv[5] = (m[2] * m[3] - m[0] * m[5]) * id;
  inv[6] = (m[3] * m[7] - m[4] * m[6]) * id;
  inv[7] = (m[1] * m[6] - m[0] * m[7]) * id;
  inv[8] = (m[0] * m[4] - m[1] * m[3]) * id;
  return true;
}

}  // namespace

GeodesicGrid::GeodesicGrid(int frequency) : freq_(frequency) {
  if (frequency < 1) {
    throw std::invalid_argument("GeodesicGrid frequency must be >= 1");
  }
  const int n = freq_;
  const int rows = 3 * n + 1;

  row_offsets_.resize(rows + 1, 0);
  for (int r = 0; r < rows; ++r) {
    row_offsets_[r + 1] = row_offsets_[r] + row_width(r);
  }
  cell_count_ = row_offsets_[rows];
  assert(cell_count_ == 10 * n * n + 2);

  // Icosahedron vertices: index 0 = north pole, 1..5 = upper ring (V_k),
  // 6..10 = lower ring (W_k), 11 = south pole.
  vertices_.resize(12);
  vertices_[0] = {0.0, 0.0, 1.0};
  vertices_[11] = {0.0, 0.0, -1.0};
  const double lat = std::atan(0.5);
  for (int k = 0; k < 5; ++k) {
    const double lon_v = 2.0 * kPi * k / 5.0;
    const double lon_w = lon_v + kPi / 5.0;
    vertices_[1 + k] = {std::cos(lat) * std::cos(lon_v), std::cos(lat) * std::sin(lon_v),
                        std::sin(lat)};
    vertices_[6 + k] = {std::cos(lat) * std::cos(lon_w), std::cos(lat) * std::sin(lon_w),
                        -std::sin(lat)};
  }

  // 20 faces: zone 0 = top (pole, V_k, V_{k+1}); zone 1 = mid lower
  // (V_k, W_k, V_{k+1}); zone 2 = mid upper (W_{k+1}, W_k, V_{k+1});
  // zone 3 = bottom (pole, W_k, W_{k+1}).
  faces_.resize(20);
  for (int k = 0; k < 5; ++k) {
    const int k1 = (k + 1) % 5;
    Face& top = faces_[k];
    top.a = vertices_[0];
    top.b = vertices_[1 + k];
    top.c = vertices_[1 + k1];
    top.zone = 0;
    top.sector = k;

    Face& mid_lo = faces_[5 + k];
    mid_lo.a = vertices_[1 + k];
    mid_lo.b = vertices_[6 + k];
    mid_lo.c = vertices_[1 + k1];
    mid_lo.zone = 1;
    mid_lo.sector = k;

    Face& mid_hi = faces_[10 + k];
    mid_hi.a = vertices_[6 + k1];
    mid_hi.b = vertices_[6 + k];
    mid_hi.c = vertices_[1 + k1];
    mid_hi.zone = 2;
    mid_hi.sector = k;

    Face& bot = faces_[15 + k];
    bot.a = vertices_[11];
    bot.b = vertices_[6 + k];
    bot.c = vertices_[6 + k1];
    bot.zone = 3;
    bot.sector = k;
  }
  for (Face& f : faces_) {
    const bool ok = invert3x3(f.a, f.b, f.c, f.inv);
    assert(ok);
    (void)ok;
  }

  // Precompute cell centers.
  centers_.resize(cell_count_);
  for (int cell = 0; cell < cell_count_; ++cell) {
    const auto [row, col] = row_col(cell);
    if (row == 0) {
      centers_[cell] = vertices_[0];
      continue;
    }
    if (row == 3 * n) {
      centers_[cell] = vertices_[11];
      continue;
    }
    int zone = 0;
    int sector = 0;
    double x = 0.0;
    double y = 0.0;
    cell_face_coords(row, col, zone, sector, x, y);
    centers_[cell] = lattice_point(zone, sector, x, y);
  }

  // Cell areas: one third of the incident lattice-triangle fan. Every lattice
  // triangle is counted at its three corners, so the areas sum to 4*pi.
  areas_.resize(cell_count_, 0.0);
  for (int cell = 0; cell < cell_count_; ++cell) {
    std::array<int, 6> nb{};
    const int m = neighbors(cell, nb);
    double acc = 0.0;
    for (int i = 0; i < m; ++i) {
      acc += spherical_tri_area(centers_[cell], centers_[nb[i]],
                                centers_[nb[(i + 1) % m]]);
    }
    areas_[cell] = acc / 3.0;
  }
}

int GeodesicGrid::row_width(int row) const {
  const int n = freq_;
  if (row == 0 || row == 3 * n) {
    return 1;
  }
  if (row < n) {
    return 5 * row;
  }
  if (row <= 2 * n) {
    return 5 * n;
  }
  return 5 * (3 * n - row);
}

int GeodesicGrid::sector_stride(int row) const { return row_width(row) / 5; }

int GeodesicGrid::index(int row, int col) const {
  const int w = row_width(row);
  int c = col % w;
  if (c < 0) {
    c += w;
  }
  return row_offsets_[row] + c;
}

std::pair<int, int> GeodesicGrid::row_col(int index) const {
  const auto it = std::upper_bound(row_offsets_.begin(), row_offsets_.end(), index);
  const int row = static_cast<int>(it - row_offsets_.begin()) - 1;
  return {row, index - row_offsets_[row]};
}

int GeodesicGrid::neighbor_count(int cell) const {
  return is_pentagon(cell) ? 5 : 6;
}

bool GeodesicGrid::is_pentagon(int cell) const {
  const auto [row, col] = row_col(cell);
  const int n = freq_;
  if (row == 0 || row == 3 * n) {
    return true;
  }
  if (row != n && row != 2 * n) {
    return false;
  }
  return col % sector_stride(row) == 0;
}

int GeodesicGrid::neighbors(int cell, std::array<int, 6>& out) const {
  const int n = freq_;
  const auto [row, col] = row_col(cell);

  // Poles (see sample_ico.hpp: north pole sectors wind CCW seen from above,
  // so CW order is 0,4,3,2,1; south pole is 0,1,2,3,4).
  if (row == 0) {
    out[0] = index(1, 0);
    out[1] = index(1, 4);
    out[2] = index(1, 3);
    out[3] = index(1, 2);
    out[4] = index(1, 1);
    return 5;
  }
  if (row == 3 * n) {
    for (int d = 0; d < 5; ++d) {
      out[d] = index(3 * n - 1, d);
    }
    return 5;
  }

  const int stride = sector_stride(row);
  const int sec = col / stride;
  const int rem = col % stride;

  const bool top = row <= n;
  const bool bot = row >= 2 * n;
  const bool mid = row >= n && row <= 2 * n;
  const bool vridge = rem == 0;
  // half encodes {vridge:1, top:2, bot:4, mid:8} and selects the CW
  // direction pattern (sample_ico.hpp "typ >> 1").
  const int half = (vridge ? 1 : 0) | (top ? 2 : 0) | (bot ? 4 : 0) | (mid ? 8 : 0);

  // SNEF: current/north/south sector widths and effective columns.
  int rw = 0;
  int nm = 0;
  int sm = 0;
  if (top && mid) {  // TMID (row == n)
    rw = n;
    nm = -1;
    sm = 0;
  } else if (bot && mid) {  // BMID (row == 2n)
    rw = n;
    nm = 0;
    sm = -1;
  } else if (mid) {
    rw = n;
  } else if (top) {
    rw = row;
    nm = -1;
    sm = 1;
  } else {  // bottom cap
    rw = 3 * n - row;
    nm = 1;
    sm = -1;
  }
  const int nef = col + nm * sec;
  const int sef = col + sm * sec;

  const auto at = [this](int r, int c) { return index(r, c); };
  const int e = at(row, col + 1);
  const int se = at(row + 1, sef + 1);
  const int s = at(row + 1, sef);
  const int sw = at(row + 1, sef - 1);
  const int w = at(row, col - 1);
  const int nw = at(row - 1, nef - 1);
  const int nn = at(row - 1, nef);
  const int ne = at(row - 1, nef + 1);

  switch (half) {
    case 2:  // top interior:      E SE S W NW N
      out = {e, se, s, w, nw, nn};
      return 6;
    case 3:  // top vridge:        E SE S SW W N
      out = {e, se, s, sw, w, nn};
      return 6;
    case 4:  // bottom interior
    case 8:  // mid interior
    case 9:  // mid vridge
    case 12: // BMID interior:     E S SW W N NE
      out = {e, s, sw, w, nn, ne};
      return 6;
    case 5:  // bottom vridge:     E S W NW N NE
      out = {e, s, w, nw, nn, ne};
      return 6;
    case 10:  // TMID interior:    E S SW W NW N
      out = {e, s, sw, w, nw, nn};
      return 6;
    case 11:  // TMID vertex:      E S SW W N (pentagon)
      out[0] = e; out[1] = s; out[2] = sw; out[3] = w; out[4] = nn;
      return 5;
    case 13:  // BMID vertex:      E S W N NE (pentagon)
      out[0] = e; out[1] = s; out[2] = w; out[3] = nn; out[4] = ne;
      return 5;
    default:
      assert(false);
      return 0;
  }
}

Vec3d GeodesicGrid::lattice_point(int zone, int sector, double x, double y) const {
  const double n = static_cast<double>(freq_);
  const Face& face = faces_[zone * 5 + sector];
  double u = 0.0;
  double v = 0.0;
  // Face lattice frames (x, y) -> barycentric (u toward a, v toward b,
  // w toward c):
  //   top:       x = n*v, y = n*w
  //   mid lower: x = n*v, y = n*w        (x = row-n, y = col_rem)
  //   mid upper: x = n*(1-w), y = n*(1-v)
  //   bottom:    x = n*u, y = n*w
  switch (zone) {
    case 0:
    case 1:
      v = x / n;
      u = 1.0 - v - y / n;
      break;
    case 2:
      v = 1.0 - y / n;
      u = 1.0 - v - (1.0 - x / n);
      break;
    case 3:
      u = x / n;
      v = 1.0 - u - y / n;
      break;
    default:
      assert(false);
  }
  double bu = 0.0;
  double bv = 0.0;
  double bw = 0.0;
  warp_bary(u, v, bu, bv, bw);
  return normalized(add(add(mul(face.a, bu), mul(face.b, bv)), mul(face.c, bw)));
}

void GeodesicGrid::cell_face_coords(int row, int col, int& zone, int& sector,
                                    double& x, double& y) const {
  const int n = freq_;
  const int stride = sector_stride(row);
  sector = col / stride;
  const int rem = col % stride;
  if (row <= n) {
    zone = 0;
    x = static_cast<double>(row - rem);
    y = static_cast<double>(rem);
  } else if (row <= 2 * n) {
    const int s = row - n;
    if (s + rem <= n) {
      zone = 1;
      x = static_cast<double>(s);
      y = static_cast<double>(rem);
    } else {
      zone = 2;
      x = static_cast<double>(s);
      y = static_cast<double>(rem);
    }
  } else {
    zone = 3;
    x = static_cast<double>(row - 2 * n);
    y = static_cast<double>(rem);
  }
  // Mid zones share the (x = row-n, y = rem) frame; lattice_point maps them
  // through the appropriate face. For zone 2 convert to that face's frame.
  if (zone == 2) {
    // face frame of mid upper: x' = s, y' = rem (same numbers; conversion to
    // barycentric happens inside lattice_point).
  }
}

int GeodesicGrid::lattice_cell_index(int zone, int sector, int i, int j) const {
  const int n = freq_;
  switch (zone) {
    case 0: {  // top: row = i + j, col_rem = j, stride = row
      const int row = i + j;
      if (row == 0) {
        return 0;
      }
      return index(row, sector * sector_stride(row) + j);
    }
    case 1:
    case 2: {  // mid rhombus frame (s, t): row = n + i, col_rem = j
      const int row = n + i;
      if (row == 3 * n) {
        return cell_count_ - 1;
      }
      return index(row, sector * sector_stride(row) + j);
    }
    case 3: {  // bottom: row = 2n + i, col_rem = j, stride = n - i
      const int row = 2 * n + i;
      if (row == 3 * n) {
        return cell_count_ - 1;
      }
      return index(row, sector * sector_stride(row) + j);
    }
    default:
      assert(false);
      return 0;
  }
}

GeodesicGrid::BaryLookup GeodesicGrid::locate(const Vec3d& p) const {
  const int n = freq_;

  // 1. Containing face: maximize the minimum gnomonic barycentric coordinate.
  int best_face = 0;
  double best_score = -1e30;
  double lam[3] = {0.0, 0.0, 0.0};
  for (int f = 0; f < 20; ++f) {
    const double* iv = faces_[f].inv;
    const double l0 = iv[0] * p.x + iv[1] * p.y + iv[2] * p.z;
    const double l1 = iv[3] * p.x + iv[4] * p.y + iv[5] * p.z;
    const double l2 = iv[6] * p.x + iv[7] * p.y + iv[8] * p.z;
    const double score = std::min(l0, std::min(l1, l2));
    if (score > best_score) {
      best_score = score;
      best_face = f;
      lam[0] = l0;
      lam[1] = l1;
      lam[2] = l2;
    }
  }
  const Face& face = faces_[best_face];
  const double lsum = lam[0] + lam[1] + lam[2];
  const double tu = std::max(0.0, lam[0] / lsum);
  const double tv = std::max(0.0, lam[1] / lsum);

  // 2. Newton: solve warp_bary(u, v) = (tu, tv). ~3 iterations (quadratic
  // convergence from the gnomonic initial guess).
  double u = tu;
  double v = tv;
  for (int it = 0; it < 8; ++it) {
    const double w = 1.0 - u - v;
    const double joint = kWarpBeta * u * v * w;
    const double fu = warp_f(u) + joint;
    const double fv = warp_f(v) + joint;
    const double fw = warp_f(w) + joint;
    const double s = fu + fv + fw;
    const double ru = fu / s - tu;
    const double rv = fv / s - tv;
    if (std::abs(ru) + std::abs(rv) < 1e-13) {
      break;
    }
    // d(joint)/du = beta*(vw - uv), d(joint)/dv = beta*(uw - uv)
    const double ju = kWarpBeta * (v * w - u * v);
    const double jv = kWarpBeta * (u * w - u * v);
    const double dfu_du = warp_f_prime(u) + ju;
    const double dfu_dv = jv;
    const double dfv_du = ju;
    const double dfv_dv = warp_f_prime(v) + jv;
    const double dfw_du = -warp_f_prime(w) + ju;
    const double dfw_dv = -warp_f_prime(w) + jv;
    const double ds_du = dfu_du + dfv_du + dfw_du;
    const double ds_dv = dfu_dv + dfv_dv + dfw_dv;
    const double a00 = (dfu_du * s - fu * ds_du) / (s * s);
    const double a01 = (dfu_dv * s - fu * ds_dv) / (s * s);
    const double a10 = (dfv_du * s - fv * ds_du) / (s * s);
    const double a11 = (dfv_dv * s - fv * ds_dv) / (s * s);
    const double det = a00 * a11 - a01 * a10;
    if (std::abs(det) < 1e-18) {
      break;
    }
    u -= (a11 * ru - a01 * rv) / det;
    v -= (-a10 * ru + a00 * rv) / det;
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0 - u);
  }
  const double w = 1.0 - u - v;

  // 3. Face barycentric -> lattice frame (x, y).
  double x = 0.0;
  double y = 0.0;
  switch (face.zone) {
    case 0:
    case 1:
      x = n * v;
      y = n * w;
      break;
    case 2:
      x = n * (1.0 - w);
      y = n * (1.0 - v);
      break;
    case 3:
      x = n * u;
      y = n * w;
      break;
    default:
      break;
  }
  constexpr double kEps = 1e-9;
  x = std::clamp(x, 0.0, static_cast<double>(n) - kEps);
  y = std::clamp(y, 0.0, static_cast<double>(n) - kEps);

  // 4. Unit-cell simplex split.
  int i = static_cast<int>(x);
  int j = static_cast<int>(y);
  const double fx = x - i;
  const double fy = y - j;

  BaryLookup out;
  if (fx + fy <= 1.0) {
    out.cell = {lattice_cell_index(face.zone, face.sector, i, j),
                lattice_cell_index(face.zone, face.sector, i + 1, j),
                lattice_cell_index(face.zone, face.sector, i, j + 1)};
    out.weight = {1.0 - fx - fy, fx, fy};
  } else {
    out.cell = {lattice_cell_index(face.zone, face.sector, i + 1, j + 1),
                lattice_cell_index(face.zone, face.sector, i + 1, j),
                lattice_cell_index(face.zone, face.sector, i, j + 1)};
    out.weight = {fx + fy - 1.0, 1.0 - fy, 1.0 - fx};
  }
  return out;
}

int GeodesicGrid::locate_cell(const Vec3d& p) const {
  const BaryLookup lk = locate(p);
  int best = 0;
  for (int k = 1; k < 3; ++k) {
    if (lk.weight[k] > lk.weight[best]) {
      best = k;
    }
  }
  return lk.cell[best];
}

GeodesicGrid::RhombusCoord GeodesicGrid::cell_to_rhombus(int cell) const {
  const int n = freq_;
  if (cell == 0) {
    return {-1, 0, 0};
  }
  if (cell == cell_count_ - 1) {
    return {-2, 0, 0};
  }
  const auto [row, col] = row_col(cell);
  const int stride = sector_stride(row);
  const int sec = col / stride;
  const int c = col % stride;
  RhombusCoord out;
  if (row < n) {
    // Top cap: north rhombus, top-face frame (a, b) = (row - c, c).
    out.rhombus = sec;
    out.i = (row - c) - 1;
    out.j = c;
  } else if (row < 2 * n) {
    const int s = row - n;
    const int t = c;
    if (s + t <= n - 1) {
      // Mid-lower triangle -> north rhombus: (a, b) = (n - t, s + t).
      out.rhombus = sec;
      out.i = (n - t) - 1;
      out.j = s + t;
    } else {
      // Mid-upper triangle -> south rhombus: (a', b') = (2n - s - t, t).
      out.rhombus = 5 + sec;
      out.i = (2 * n - s - t) - 1;
      out.j = t;
    }
  } else if (row == 2 * n) {
    // W ring -> south rhombus: (a', b') = (n - c, c).
    out.rhombus = 5 + sec;
    out.i = (n - c) - 1;
    out.j = c;
  } else {
    // Bottom cap -> south rhombus: (a', b') = (n - s' - c, c), s' = row - 2n.
    const int sp = row - 2 * n;
    out.rhombus = 5 + sec;
    out.i = (n - sp - c) - 1;
    out.j = c;
  }
  return out;
}

int GeodesicGrid::rhombus_to_cell(int rhombus, int i, int j) const {
  const int n = freq_;
  if (rhombus == -1) {
    return 0;
  }
  if (rhombus == -2) {
    return cell_count_ - 1;
  }
  const int a = i + 1;
  const int b = j;
  if (rhombus < 5) {
    const int sec = rhombus;
    if (a + b <= n - 1) {
      // Top face: row = a + b, rem = b.
      const int row = a + b;
      return index(row, sec * sector_stride(row) + b);
    }
    // Mid-lower: t = n - a, s = a + b - n; row = n + s, rem = t.
    const int row = a + b;  // n + (a + b - n)
    return index(row, sec * n + (n - a));
  }
  const int sec = rhombus - 5;
  if (a + b <= n - 1) {
    // Bottom cap: s' = n - a - b, row = 2n + s', rem = b.
    const int row = 2 * n + (n - a - b);
    return index(row, sec * sector_stride(row) + b);
  }
  if (a + b == n) {
    // W ring (row 2n): rem = b.
    return index(2 * n, sec * n + b);
  }
  // Mid-upper: s = 2n - a - b, t = b; row = n + s, rem = t.
  const int row = 3 * n - a - b;  // n + (2n - a - b)
  return index(row, sec * n + b);
}

}  // namespace world_engine::terrain
