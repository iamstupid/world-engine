# WorldEngine — Phase 1 Implementation Specification

## Foundation: Mesh, Server, Client, and Noise Test

---

## 1. Project Structure

```
world_engine/
├── CMakeLists.txt                          # Top-level: builds library, tests
├── README.md
│
├── library/
│   ├── CMakeLists.txt
│   ├── mesh/
│   │   ├── mesh_concept.hpp                # Vec3f, Adjacency, WorldMesh concept
│   │   ├── field.hpp                       # Field<Mesh> — data-on-mesh container
│   │   ├── icosahedral_geodesic.hpp        # IcoMesh declaration
│   │   ├── icosahedral_geodesic.cpp        # IcoMesh implementation
│   │   └── flat_grid.hpp                   # FlatGrid (trivial, for testing/comparison)
│   ├── noise/
│   │   ├── noise_generator.hpp             # Wraps FastNoiseLite for mesh-based generation
│   │   └── noise_generator.cpp
│   ├── io/
│   │   ├── serialize.hpp                   # Binary serialization for field data
│   │   └── serialize.cpp
│   ├── integration/
│   │   ├── CMakeLists.txt
│   │   └── python_bindings.cpp             # pybind11 module
│   └── tests/
│       ├── test_ico_mesh.cpp               # Unit tests: topology, neighbors, areas
│       ├── test_noise.cpp                  # Unit tests: noise on mesh
│       ├── test_serialize.cpp              # Unit tests: round-trip serialization
│       └── visual/
│           ├── vis_ico_mesh.cpp            # Export mesh geometry as OBJ for inspection
│           └── vis_noise_on_ico.cpp        # Export noise-on-mesh as colored OBJ + PNG
│
├── server/
│   ├── requirements.txt
│   ├── app.py                              # FastAPI entry point
│   ├── routes/
│   │   ├── generate.py                     # POST /api/v1/generate/noise
│   │   ├── world.py                        # GET /api/v1/world/{id}/field/{name}
│   │   └── mesh.py                         # GET /api/v1/mesh/ico/{N}
│   ├── services/
│   │   ├── generator.py                    # Calls libworldengine via pybind
│   │   └── world_store.py                  # In-memory world state cache
│   └── models/
│       └── params.py                       # Pydantic request/response models
│
├── client/
│   ├── index.html                          # Single-page app entry
│   ├── style.css                           # Minimal UI styling
│   ├── app.js                              # Main application logic, UI wiring
│   ├── api/
│   │   └── client.js                       # Fetch wrappers for server endpoints
│   ├── renderer/
│   │   ├── engine.js                       # Three.js scene setup, camera, controls
│   │   ├── colormap.js                     # Height→color, biome→color mappings
│   │   ├── geometry/
│   │   │   ├── icosahedral_geodesic.js     # Mesh generation from N
│   │   │   │                               #   - vertex positions on unit sphere
│   │   │   │                               #   - triangle face connectivity
│   │   │   │                               #   - vertex color from field data
│   │   │   │                               #   - equirectangular projection
│   │   │   └── shaders/
│   │   │       ├── terrain.vert.glsl       # Displacement + coloring vertex shader
│   │   │       └── terrain.frag.glsl       # Fragment shader with lighting
│   │   └── views/
│   │       ├── globe_view.js               # 3D sphere rendering
│   │       └── map_view.js                 # 2D equirectangular projection
│   └── ui/
│       ├── parameter_panel.js              # Slider controls for generation params
│       └── view_switcher.js                # Globe / Map / Wireframe toggle
│
└── third_party/
    ├── FastNoiseLite/                      # Header-only noise library (MIT)
    │   └── Cpp/FastNoiseLite.h
    ├── pybind11/                           # Git submodule (BSD)
    ├── stb/                                # stb_image_write.h (public domain)
    └── nlohmann/                           # json.hpp (MIT)
        └── json.hpp
```

---

## 2. IcoMesh — The Core Data Structure

### 2.1 Storage Layout

One flat array of cells, packed row by row from north pole to south pole:

```
Row 0:          ●                       north pole (1 cell)
Row 1:          ○ ○ ○ ○ ○              north cap (5 cells)
Row 2:          ○ ○ ○ ○ ○ ○ ○ ○ ○ ○   north cap (10 cells)
...
Row N-1:        ○ × 5(N-1)             north cap (5(N-1) cells)
Row N:          ■ × 5N                 equator starts (5N cells)
Row N+1:        ■ × 5N                 equator (5N cells)
...
Row 2N:         ■ × 5N                 equator ends (5N cells)
Row 2N+1:       ○ × 5(N-1)             south cap (5(N-1) cells)
...
Row 3N-1:       ○ ○ ○ ○ ○              south cap (5 cells)
Row 3N:         ●                       south pole (1 cell)
```

Total rows: **3N + 1**. Total cells: **10N² + 2**.

Row widths follow a single formula:

| Row range | Width | Zone |
|-----------|-------|------|
| r = 0 | 1 | North pole |
| 1 ≤ r ≤ N-1 | 5r | North cap (widening) |
| N ≤ r ≤ 2N | 5N | Equatorial band (constant) |
| 2N+1 ≤ r ≤ 3N-1 | 5(3N - r) | South cap (narrowing) |
| r = 3N | 1 | South pole |

The entire mesh structure is described by two precomputed arrays:

- `row_start[3N+1]` — flat index of the first cell in each row
- `row_width[3N+1]` — number of cells in each row

Every row wraps horizontally: column indices are always taken mod `row_width[r]`.

### 2.2 C++ Interface

```cpp
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>

struct Vec3f {
    float x, y, z;
    Vec3f operator+(Vec3f b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3f operator-(Vec3f b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3f operator*(float s) const { return {x*s, y*s, z*s}; }
    float dot(Vec3f b) const { return x*b.x + y*b.y + z*b.z; }
    Vec3f cross(Vec3f b) const {
        return {y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x};
    }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3f normalized() const { float l = length(); return {x/l, y/l, z/l}; }
    float latitude() const { return std::asin(z / length()); }
    float longitude() const { return std::atan2(y, x); }
};

struct Adjacency {
    int idx;        // neighbor cell flat index
    float distance; // great-circle distance between centers on unit sphere
};

class IcoMesh {
public:
    explicit IcoMesh(int subdivision_level);

    int N() const;                        // subdivision level
    int num_cells() const;                // 10*N*N + 2
    int num_rows() const;                 // 3*N + 1

    // --- Row table ---
    int row_start(int row) const;
    int row_width(int row) const;
    int row_of(int idx) const;
    int col_of(int idx) const;

    // --- Geometry (random access, uses row_of/col_of internally) ---
    Vec3f cell_position(int idx) const;
    float cell_area(int idx) const;
    float latitude(int idx) const;
    float longitude(int idx) const;

    // --- Topology (random access) ---
    int neighbor_count(int idx) const;
    void neighbors(int idx, Adjacency* out, int* count) const;

    // --- Iterator ---
    // 16-byte compact iterator with fused geometry/topology methods.
    // Maintains (row, col) incrementally — no division to locate a cell.
    struct Iterator {
        IcoMesh* mesh;          // 8 bytes
        int      idx;           // 4 bytes (flat index)
        uint16_t row;           // 2 bytes
        uint16_t col;           // 2 bytes — total 16 bytes, aligned

        // --- Classification ---
        enum CellType { FACE, EDGE, VERTEX };
        CellType cell_type() const;
        int neighbor_count() const;         // 5 for VERTEX, 6 otherwise

        // --- Fused geometry: zone + face + barycentrics + warp in one pass ---
        int face_id() const;                // [0..19]
        Vec3f position() const;             // fused face_id + uvw + warp + project

        // --- Ordered adjacency ---
        // direction 0 = smallest neighbor index (right-hand / clockwise).
        // Neighbors returned in clockwise order viewed from outside sphere.
        Iterator adjacent(int direction) const;
        int all_adjacent(std::array<Iterator, 6>& out) const;  // returns count
        int all_adjacent(Iterator* out) const;                  // returns count

        // --- Sequential iteration ---
        Iterator& operator++();
        bool operator!=(const Iterator& other) const;
        int operator*() const;              // yields flat index
    };

    Iterator begin() const;
    Iterator end() const;

    // --- Precomputed data ---
    const std::vector<Vec3f>& positions() const;          // all cell positions
    const std::vector<std::array<int,3>>& faces() const;  // triangle connectivity

    // --- Face corner vertices (20 faces × 3 vertex indices) ---
    static constexpr int FACE_VERTS[20][3] = { /* ... */ };
    const Vec3f ico_verts[12];              // 12 base icosahedron vertices

private:
    int N_, total_cells_, total_rows_;
    std::vector<int> row_start_;
    std::vector<int> row_width_;
    std::vector<Vec3f> positions_;
    mutable std::vector<std::array<int,3>> faces_;

    void build_row_table();
    void build_positions();
    void build_faces() const;
};
```

### 2.3 Row Table Construction

```cpp
void IcoMesh::build_row_table() {
    total_rows_ = 3 * N_ + 1;
    row_start_.resize(total_rows_);
    row_width_.resize(total_rows_);

    int offset = 0;
    for (int r = 0; r < total_rows_; r++) {
        row_start_[r] = offset;

        if (r == 0 || r == total_rows_ - 1) {
            row_width_[r] = 1;                        // poles
        } else if (r <= N_ - 1) {
            row_width_[r] = 5 * r;                    // north cap
        } else if (r <= 2 * N_) {
            row_width_[r] = 5 * N_;                   // equatorial band
        } else {
            row_width_[r] = 5 * (3 * N_ - r);        // south cap
        }

        offset += row_width_[r];
    }
    total_cells_ = offset;  // must equal 10*N*N + 2
}
```

### 2.4 Neighbor Computation

All neighbor logic uses only `row`, `col`, `row_width[row]`, and `row_width[row±1]`.

**Constant-width rows** (equatorial band, rows N through 2N, where `row_width[r] == row_width[r±1]`):

```
Cell at (row r, col c), width W = 5N:
  Same row:     row_start[r] + (c-1+W)%W,   row_start[r] + (c+1)%W
  Row above:    row_start[r-1] + (c-1+W)%W,  row_start[r-1] + c
  Row below:    row_start[r+1] + c,           row_start[r+1] + (c+1)%W
```

Always 6 neighbors. Wrapping in column handles the horizontal seam.

**Widening rows** (row r has width 5r, row r-1 has width 5(r-1)):

Each of 5 faces contributes r cells in row r and r-1 cells in row r-1. Within a face, track `face_index = c / r` and `face_col = c % r`:

```
face_col == 0 (face boundary vertex):
  Previous row: 1 neighbor at the corresponding face boundary
  → 5 neighbors total (pentagon when it's an icosahedron vertex)

face_col > 0 (face interior):
  Previous row: 2 neighbors at face_col-1 and face_col within the same face
  → 6 neighbors total
```

Global column in previous row: `face_index * (r-1) + face_col_in_prev_row`.

When iterating sequentially, `face_index` and `face_col` are maintained by increment — no division needed.

**Narrowing rows** (south cap): mirror of widening.

**Poles** (row 0 and row 3N): exactly 5 neighbors — all cells in the adjacent row.

**Transition rows** (cap ↔ equator boundary): row N-1 (width 5(N-1)) → row N (width 5N) is widening; row 2N (width 5N) → row 2N+1 (width 5(N-1)) is narrowing. Both handled by the same zipper logic — the row table already encodes the width change, so no special case is needed.

**Pentagon vertex locations** (12 total):

The original icosahedron has 12 vertices where 5 faces meet:

| Location | Count |
|----------|-------|
| Row 0 (north pole) | 1 |
| Row N, cols {0, N, 2N, 3N, 4N} | 5 |
| Row 2N, cols {0, N, 2N, 3N, 4N} | 5 |
| Row 3N (south pole) | 1 |

All other cells have exactly 6 neighbors.

### 2.5 Position Computation — Fused `position()`

Each cell's 3D position on the unit sphere is computed by a single fused method that determines the icosahedral face, derives barycentric coordinates, applies the polynomial warp, and projects to the sphere — all from the same zone/stride/col_rem intermediates.

The 12 icosahedron base vertices are defined analytically (2 poles + upper ring of 5 at latitude atan(1/2) + lower ring of 5 at latitude -atan(1/2), offset by 36°). The 20 face definitions are triples of these vertex indices, ordered as: faces 0–4 (north cap, touching north pole), faces 5–14 (equatorial band, alternating up/down), faces 15–19 (south cap, touching south pole).

```cpp
constexpr float WARP_ALPHA = 0.5372f;  // 1D redistribution strength
constexpr float WARP_BETA  = -0.4637f; // coupled center correction

Vec3f Iterator::position() const {
    int N = mesh->N();

    // --- Poles: direct vertex ---
    if (row == 0)       return mesh->ico_verts[0];      // north pole
    if (row == 3 * N)   return mesh->ico_verts[11];     // south pole

    int N2 = 2 * N;

    // --- Zone classification (done once, reused for face + barycentrics) ---
    //   0 = top cap (row 1..N), 1 = equator (row N+1..2N), 2 = bottom cap (row 2N+1..3N-1)
    int zone = (row <= N) ? 0 : ((row > N2) ? 2 : 1);

    // Row stride = cells per face-slice in this row
    int stride = (zone == 0) ? row : ((zone == 2) ? (3 * N - row) : N);

    int col_no  = col / stride;       // which of 5 face sectors (0..4)
    int col_rem = col % stride;       // position within face sector

    // --- Face ID (reuses zone, col_no, col_rem) ---
    int fid;
    if (zone == 1) {
        bool is_down = col_rem > (row - N);
        fid = 5 + col_no * 2 + (is_down ? 1 : 0);
    } else {
        fid = (zone == 0 ? 0 : 15) + col_no;
    }

    // --- Barycentric coordinates (reuses zone, stride, col_rem) ---
    // (u, v, w) are integer coords in [0..N], normalized to [0..1] below
    float u, v, w;
    if (zone == 0) {
        // Top cap: apex at pole (row 0), base at row N
        u = (float)(N - row) / N;          // 1 at pole, 0 at base
        v = (float)col_rem / N;
        w = 1.0f - u - v;
    } else if (zone == 2) {
        // Bottom cap: mirror of top
        int mirror = 3 * N - row;
        u = (float)(N - mirror) / N;
        v = (float)col_rem / N;
        w = 1.0f - u - v;
    } else {
        // Equatorial band: two triangle orientations
        int d = row - N;                    // depth into equatorial band (0..N)
        if (col_rem <= d) {
            // Up-triangle (even faces 5,7,9,11,13)
            u = (float)(N - d) / N;
            v = (float)col_rem / N;
            w = 1.0f - u - v;
        } else {
            // Down-triangle (odd faces 6,8,10,12,14)
            // Invert: apex at row 2N, base at row N
            u = (float)d / N;
            v = (float)(N - col_rem) / N;
            w = 1.0f - u - v;
        }
    }

    // --- Coupled polynomial warp ---
    float uw = u + WARP_ALPHA * u * (1.0f - u) * (0.5f - u);
    float vw = v + WARP_ALPHA * v * (1.0f - v) * (0.5f - v);
    float ww = w + WARP_ALPHA * w * (1.0f - w) * (0.5f - w);

    float c = u * v * w * WARP_BETA;
    uw += c;  vw += c;  ww += c;

    float s = uw + vw + ww;
    uw /= s;  vw /= s;  ww /= s;

    // --- Project to sphere ---
    const Vec3f& A = mesh->ico_verts[FACE_VERTS[fid][0]];
    const Vec3f& B = mesh->ico_verts[FACE_VERTS[fid][1]];
    const Vec3f& C = mesh->ico_verts[FACE_VERTS[fid][2]];

    return (A * uw + B * vw + C * ww).normalized();
}
```

The zone classification, stride, col_no, and col_rem are computed once and drive both face identification and barycentric coordinate extraction. No separate `face_id()` + `get_uvw()` calls — the shared intermediates are fused into a single pass.

**Why the warp works.** Naive `lerp + normalize` creates ~92% area variation (max/min ≈ 1.92). The 1D cubic `α·x·(1-x)·(0.5-x)` redistributes points along each barycentric axis, hitting a wall at 1.146. The coupled `β·u·v·w` term breaks through this wall by counteracting the center-bulge from sphere projection — `u·v·w` is zero on all face edges and maximal at the center, directly targeting the distortion source. Together they achieve ~5% variation (1.05) at only 1.4× the cost of lerp.

| Method | Area max/min | Cost vs lerp |
|--------|-------------|-------------|
| Lerp + normalize | 1.92 | 1.0× |
| Nested slerp | 1.18 | 3.1× |
| **Coupled poly warp** | **1.05** | **1.4×** |

**`face_id()` is still available as a standalone method** for cases where only the face is needed (e.g. face-based spatial queries). It reuses the same zone/stride/col_no logic:

```cpp
int Iterator::face_id() const {
    if (row == 0) return 0;
    if (row == 3 * mesh->N()) return 15;

    int N = mesh->N();
    int zone = (row <= N) ? 0 : ((row > 2*N) ? 2 : 1);
    int stride = (zone == 0) ? row : ((zone == 2) ? (3*N - row) : N);
    int col_no = col / stride;

    if (zone == 1) {
        bool is_down = (col % stride) > (row - N);
        return 5 + col_no * 2 + (is_down ? 1 : 0);
    }
    return (zone == 0 ? 0 : 15) + col_no;
}
```

### 2.6 Face List for Rendering

Triangle connectivity for WebGL, generated from adjacent row pairs:

| Row pair type | Method |
|---------------|--------|
| Same-width (equatorial band) | Each column pair → quad → 2 triangles |
| Different-width (caps) | Zipper pattern between rows |
| Pole to row 1 / row 3N-1 | Fan of 5 triangles |

Total triangles ≈ 2 × num_cells (Euler formula: F ≈ 2V for a triangulated sphere).

---

## 3. Field — Data on Mesh

```cpp
template<typename Mesh>
struct Field {
    const Mesh* mesh;
    std::vector<float> data;
    std::string name;

    Field(const Mesh* m, const std::string& name)
        : mesh(m), data(m->num_cells(), 0.0f), name(name) {}

    float& operator[](int idx) { return data[idx]; }
    const float& operator[](int idx) const { return data[idx]; }

    int size() const { return static_cast<int>(data.size()); }
    float* ptr() { return data.data(); }
    const float* ptr() const { return data.data(); }

    float min_val() const;
    float max_val() const;
    float quantile(float q) const;                      // q-th quantile (0..1), uses nth_element
    void rescale(float target_min, float target_max);   // linear remap to [target_min, target_max]
    void shift(float offset);                            // add offset to all values
};
```

---

## 4. Noise Generation on Mesh

```cpp
struct NoiseParams {
    int seed = 42;
    int octaves = 6;
    float frequency = 1.5f;        // on unit sphere, ~1-5 is useful
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float warp_amplitude = 0.0f;   // domain warp (0 = off)
    float ocean_fraction = 0.55f;  // target fraction of cells below sea level
};

template<typename Mesh>
Field<Mesh> generate_noise(const Mesh& mesh, const NoiseParams& params) {
    Field<Mesh> field(&mesh, "elevation");

    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    noise.SetSeed(params.seed);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(params.octaves);
    noise.SetFractalLacunarity(params.lacunarity);
    noise.SetFractalGain(params.gain);
    noise.SetFrequency(params.frequency);

    FastNoiseLite warp;
    if (params.warp_amplitude > 0) {
        warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
        warp.SetDomainWarpAmp(params.warp_amplitude);
        warp.SetSeed(params.seed + 1);
        warp.SetFrequency(params.frequency * 0.5f);
    }

    #pragma omp parallel for
    for (int i = 0; i < mesh.num_cells(); i++) {
        Vec3f p = mesh.cell_position(i);
        float x = p.x, y = p.y, z = p.z;
        if (params.warp_amplitude > 0) {
            warp.DomainWarp(x, y, z);
        }
        field[i] = noise.GetNoise(x, y, z);
    }

    // Shift so that the ocean_fraction quantile lands at zero.
    // elevation < 0 → ocean, elevation >= 0 → land.
    float cutoff = field.quantile(params.ocean_fraction);
    field.shift(-cutoff);

    // Rescale so peaks reach ~1.0 and deepest ocean reaches ~-1.0
    float peak = field.max_val();
    float abyss = field.min_val();
    float scale = std::max(std::abs(peak), std::abs(abyss));
    if (scale > 1e-6f) {
        for (int i = 0; i < field.size(); i++)
            field[i] /= scale;
    }

    return field;
}
```

**Elevation convention**: 0 = sea level. Negative = ocean floor. Positive = land. The `ocean_fraction` parameter controls what percentage of the surface is ocean by shifting the noise distribution so that quantile lands at zero. The final rescale to [-1, 1] keeps the range predictable for colormaps and displacement.

Key insight: FastNoiseLite natively supports 3D `GetNoise(x,y,z)`. Since every cell has a 3D position on the unit sphere, we sample the noise volume at those positions. No seams, no projection artifacts.

---

## 5. Binary Serialization Protocol

### 5.1 Design Principles

- Binary for large data (10M floats × 4 bytes = 40MB). JSON only for metadata.
- Mesh topology never sent — client reconstructs from N alone.
- Field data as compact binary with optional gzip.
- Little-endian throughout. Streamable (header first).

### 5.2 Mesh Metadata (JSON)

```
GET /api/v1/mesh/ico/{N}

Response:
{
  "N": 1000,
  "num_cells": 10000002,
  "num_rows": 3001,
  "num_faces": 19999996
}
```

Client builds geometry locally from N. This endpoint is for validation only.

### 5.3 Field Data Packet (Binary)

```
FIELD DATA PACKET ("FILD")
────────────────────────────────────────────────
Offset  Size    Field
────────────────────────────────────────────────
0       4       magic: 0x46494C44 ("FILD")
4       4       version: uint32 = 1
8       4       N: uint32 (subdivision level)
12      4       num_cells: uint32
16      4       dtype: uint32
                  0 = float32
                  1 = uint8 (biome IDs, etc.)
                  2 = float16
                  3 = vec2_float32 (8 bytes/cell)
20      4       compression: uint32
                  0 = none
                  1 = gzip
                  2 = delta + gzip
24      4       payload_size: uint32 (bytes after header)
28      16      name: char[16] (null-padded ASCII)
44      4       reserved: uint32 = 0
48      ...     payload (payload_size bytes)
────────────────────────────────────────────────
```

Compression at N=1000 (10M float32, 40MB raw): gzip → ~15-25MB, delta+gzip → ~8-15MB. Delta encoding exploits the fact that adjacent cells in the flat array are geographically close.

### 5.4 Streaming for Large Fields

For N > 2000, use HTTP chunked transfer. Client can begin partial rendering while data is still downloading.

### 5.5 Generation Request

```
POST /api/v1/generate/noise
{
  "mesh_type": "ico",
  "N": 500,
  "noise": {
    "seed": 42, "octaves": 6, "frequency": 1.5,
    "lacunarity": 2.0, "gain": 0.5,
    "warp_amplitude": 0.3, "ocean_fraction": 0.55
  }
}

Response:
{
  "world_id": "w_abc123",
  "mesh_type": "ico",
  "N": 500,
  "fields": ["elevation"],
  "elapsed_ms": 342
}
```

Client then fetches: `GET /api/v1/world/w_abc123/field/elevation`

---

## 6. Client Architecture

### 6.1 icosahedral_geodesic.js — Client-Side Mesh

The client reconstructs mesh geometry from N alone, matching the C++ implementation exactly.

```javascript
export class IcoGeodesicMesh {
    constructor(N) {
        this.N = N;
        this.numCells = 10 * N * N + 2;
        this.numRows = 3 * N + 1;

        this.rowStart = new Int32Array(this.numRows);
        this.rowWidth = new Int32Array(this.numRows);
        this._buildRowTable();

        this._positions = null;   // Float32Array(numCells * 3), lazy
        this._faces = null;       // Uint32Array(numFaces * 3), lazy
    }

    _buildRowTable() {
        let offset = 0;
        const N = this.N;
        const totalRows = this.numRows;
        for (let r = 0; r < totalRows; r++) {
            this.rowStart[r] = offset;
            if (r === 0 || r === totalRows - 1)     this.rowWidth[r] = 1;
            else if (r <= N - 1)                     this.rowWidth[r] = 5 * r;
            else if (r <= 2 * N)                     this.rowWidth[r] = 5 * N;
            else                                     this.rowWidth[r] = 5 * (3 * N - r);
            offset += this.rowWidth[r];
        }
    }

    getPositions() {
        if (!this._positions) this._buildPositions();
        return this._positions;
    }

    getFaces() {
        if (!this._faces) this._buildFaces();
        return this._faces;
    }

    createGeometry(fieldData, colormap) {
        const positions = this.getPositions();
        const faces = this.getFaces();
        const colors = colormap.map(fieldData);

        const geom = new THREE.BufferGeometry();
        geom.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geom.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geom.setIndex(new THREE.BufferAttribute(faces, 1));
        // No computeVertexNormals() — normals computed analytically in vertex shader
        // (normal = normalize(displaced_position) for radial displacement)
        return geom;
    }

    createDisplacedGeometry(fieldData, colormap, exaggeration = 1.0) {
        const base = this.getPositions();
        const displaced = new Float32Array(base.length);
        for (let i = 0; i < this.numCells; i++) {
            // elevation is signed: negative = ocean, positive = land
            const r = 1.0 + fieldData[i] * exaggeration;
            displaced[i*3]   = base[i*3]   * r;
            displaced[i*3+1] = base[i*3+1] * r;
            displaced[i*3+2] = base[i*3+2] * r;
        }
        // Build geometry with displaced positions + vertex colors...
        // Ocean floor is displaced inward (r < 1), land outward (r > 1)
    }

    _buildPositions() {
        this._positions = new Float32Array(this.numCells * 3);
        // For each (row, col): determine icosahedron face,
        // interpolate face corner vertices, normalize to unit sphere.
        // Must produce identical ordering to C++ implementation.
    }

    _buildFaces() {
        // Zip adjacent row pairs into triangles.
        // Same-width pairs → quad → 2 triangles.
        // Different-width pairs → zipper triangles.
        // Poles → fan of 5 triangles.
    }
}
```

### 6.2 Colormap

```javascript
// Elevation is signed: negative = ocean, positive = land, range [-1, 1]
export const TERRAIN_COLORMAP = [
    [-1.00, [0.00, 0.00, 0.13]],  // deep ocean
    [-0.40, [0.00, 0.15, 0.40]],  // mid ocean
    [-0.05, [0.00, 0.30, 0.70]],  // shallow water
     [0.00, [0.76, 0.70, 0.50]],  // coastline / beach
     [0.05, [0.13, 0.55, 0.13]],  // lowland green
     [0.25, [0.09, 0.45, 0.09]],  // forest green
     [0.45, [0.55, 0.35, 0.17]],  // highland brown
     [0.70, [0.55, 0.55, 0.55]],  // mountain gray
     [0.90, [0.85, 0.85, 0.85]],  // snow line
     [1.00, [1.00, 1.00, 1.00]],  // peak white
];

export function sampleColormap(value, colormap) {
    // Linear interpolation between adjacent stops
}

export function mapFieldToColors(fieldData, colormap) {
    const colors = new Float32Array(fieldData.length * 3);
    for (let i = 0; i < fieldData.length; i++) {
        const [r, g, b] = sampleColormap(fieldData[i], colormap);
        colors[i*3] = r; colors[i*3+1] = g; colors[i*3+2] = b;
    }
    return colors;
}
```

### 6.3 Rendering Views

**Globe View** (`globe_view.js`):
- Three.js scene with OrbitControls
- IcoMesh as `THREE.Mesh` with vertex colors
- Elevation displacement: vertex pushed outward by `1 + h * exaggeration`
- **Analytical vertex normals in shader** — since displacement is purely radial, `normal = normalize(displaced_position)` is exact. Do NOT use `computeVertexNormals()` (expensive face traversal) or `dFdx`/`dFdy` (screen-space approximation, visible faceting at low N). The vertex shader computes:
  ```glsl
  vec3 displaced = position * (1.0 + elevation * exaggeration);
  vec3 normal = normalize(displaced);
  ```
- Directional light + hemisphere light
- Semi-transparent blue sphere at sea level for ocean
- Wireframe overlay toggle

**Map View** (`map_view.js`):
- Canvas-based 2D rendering
- Equirectangular projection: cell lat/lon → pixel
- Nearest-neighbor or barycentric interpolation
- Pan/zoom with mouse
- Click-to-query: pixel → cell index → field value

**Web Worker offloading** (Phase 1.5): move mesh geometry computation and FILD binary parsing into a Web Worker. Transfer `Float32Array` buffers back to the main thread via `Transferable Objects` (zero-copy). Keeps the UI responsive during N=500+ mesh builds (~1s).

---

## 7. Server Implementation

### 7.1 FastAPI App

```python
from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from routes import generate, world, mesh

app = FastAPI(title="WorldEngine API", version="0.1.0")

app.include_router(generate.router, prefix="/api/v1")
app.include_router(world.router, prefix="/api/v1")
app.include_router(mesh.router, prefix="/api/v1")

app.mount("/", StaticFiles(directory="../client", html=True), name="client")
```

### 7.2 API Endpoints

```
GET  /api/v1/mesh/ico/{N}                → JSON { N, num_cells, num_rows, num_faces }
POST /api/v1/generate/noise              → JSON { world_id, fields, elapsed_ms }
GET  /api/v1/world/{id}/field/{name}     → Binary FILD packet
GET  /api/v1/world/{id}/field/{name}/stats → JSON { min, max, mean, std }
GET  /api/v1/world/{id}/query?lat=&lon=  → JSON { cell_idx, elevation, ... }
GET  /api/v1/worlds                      → JSON [{ world_id, N, created_at, fields }]
DELETE /api/v1/world/{id}
```

### 7.3 Python Bindings (pybind11)

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include "mesh/icosahedral_geodesic.hpp"
#include "mesh/field.hpp"
#include "noise/noise_generator.hpp"
#include "io/serialize.hpp"

namespace py = pybind11;

PYBIND11_MODULE(worldengine, m) {
    m.doc() = "WorldEngine: Physically-based world generation";

    py::class_<IcoMesh>(m, "IcoMesh")
        .def(py::init<int>(), py::arg("N"))
        .def("num_cells", &IcoMesh::num_cells)
        .def("N", &IcoMesh::N)
        .def("positions_numpy", [](const IcoMesh& m) {
            const auto& pos = m.positions();
            return py::array_t<float>(
                {(int)pos.size(), 3},
                {3 * sizeof(float), sizeof(float)},
                (float*)pos.data()
            );
        });

    py::class_<Field<IcoMesh>>(m, "IcoField")
        // Zero-copy view into the C++ vector. Safe: py::cast(f) prevents
        // GC of the Field while the array exists. Do NOT hold this array
        // beyond request scope if the Field can be evicted from WorldStore.
        // For the HTTP wire path, use serialize_field() which copies to py::bytes.
        .def("to_numpy", [](Field<IcoMesh>& f) {
            return py::array_t<float>(
                {f.size()}, {sizeof(float)}, f.ptr(), py::cast(f)
            );
        })
        .def_readonly("name", &Field<IcoMesh>::name);

    m.def("generate_noise_ico", [](int N, py::dict params) {
        IcoMesh mesh(N);
        NoiseParams np;
        np.seed = params["seed"].cast<int>();
        np.octaves = params["octaves"].cast<int>();
        np.frequency = params["frequency"].cast<float>();
        np.lacunarity = params["lacunarity"].cast<float>();
        np.gain = params["gain"].cast<float>();
        np.warp_amplitude = params["warp_amplitude"].cast<float>();
        np.ocean_fraction = params["ocean_fraction"].cast<float>();

        py::gil_scoped_release release;
        auto field = generate_noise(mesh, np);
        py::gil_scoped_acquire acquire;
        return field;
    });

    m.def("serialize_field", [](const Field<IcoMesh>& f, int N, int compression) {
        auto bytes = serialize_field_packet(f, N, compression);
        return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    });
}
```

---

## 8. Test Plan

### 8.1 C++ Unit Tests

**`test_ico_mesh.cpp`**:

1. Construct IcoMesh(N=4). Verify `num_cells() == 162`.
2. Verify all positions on unit sphere (`|pos| == 1.0 ± 1e-5`).
3. For every cell, `neighbor_count ∈ {5, 6}`. Exactly 12 have 5.
4. For every cell, each neighbor's neighbor list contains the original (symmetry).
5. Sum of all `cell_area()` ≈ 4π.
6. Area uniformity: `max_area / min_area < 1.10` (coupled poly warp should achieve ~1.05; lerp+normalize gives ~1.92).
7. Iterator: exactly `num_cells` visits, no repeats.
8. Test at N = 1, 2, 3, 10, 100.

**`test_noise.cpp`**:

1. Generate noise on IcoMesh(N=100). No NaN/Inf.
2. After generation: `min_val ∈ [-1, 0)`, `max_val ∈ (0, 1]`, values span both sides of zero.
3. Fraction of cells < 0 ≈ `ocean_fraction ± 0.05`.
4. Same seed → same output. Different seed → different output.

**`test_serialize.cpp`**:

1. Round-trip uncompressed: serialize → deserialize → exact equality.
2. Round-trip gzip: compressed size < raw, deserialize → equality.
3. Header magic, version, N, num_cells correct.

### 8.2 Visual Tests

**`vis_ico_mesh.cpp`**: Export IcoMesh(N=8) as OBJ. Open in MeshLab/Blender — spherical, no holes, no degenerate triangles.

**`vis_noise_on_ico.cpp`**: Noise on IcoMesh(N=200), export colored OBJ + equirectangular PNG. Organic continents, no seams, no polar artifacts.

### 8.3 Integration Test

1. Build library + pybind.
2. Start FastAPI server.
3. POST generate, GET field binary, verify FILD header + payload length.
4. Client renders globe + map — no seams, no holes, organic terrain.

---

## 9. Implementation Notes

### 9.1 Build Order

```
 1. Vec3f + IcoMesh row table + positions   → test: on-sphere, correct count
 2. IcoMesh neighbors                       → test: symmetry, 12 pentagons
 3. IcoMesh iterator                        → test: full coverage, matches random-access
 4. IcoMesh face generation                 → test: export OBJ, visual check
 5. Noise generation                        → test: colored OBJ + equirectangular PNG
 6. Binary serialization                    → test: round-trip
 7. pybind11 bindings                       → test: Python script
 8. FastAPI server                          → test: curl
 9. Client icosahedral_geodesic.js          → test: cross-validate positions vs C++
10. Client globe view                       → test: visual
11. Client map view                         → test: visual
12. Client parameter panel                  → test: end-to-end regeneration
```

### 9.2 Critical Correctness Checks

- **C++ and JS array ordering must match.** Both pack rows north-pole-to-south-pole using the same `row_width` formula. The row table is the single source of truth. Cross-validate: export C++ positions for N=10 as JSON, compare with JS, max error < 1e-6.

- **Neighbor symmetry is non-negotiable.** If A lists B, B must list A. Asymmetry breaks conservation in erosion/diffusion.

- **12 pentagons at fixed locations.** Row 0 (north pole), row 3N (south pole), row N at cols {0, N, 2N, 3N, 4N}, row 2N at cols {0, N, 2N, 3N, 4N}. These have 5 neighbors; all others have 6. Wrong counts silently corrupt every downstream algorithm.

- **Every row wraps in column.** `col = ((col % w) + w) % w` for all neighbor and face computations. Width varies by row (5r for caps, 5N for equator), so always use `row_width[r]`. Off-by-one at the seam creates a visible artifact.

### 9.3 Performance Targets

| Operation | N=100 | N=500 | N=1000 |
|-----------|-------|-------|--------|
| Mesh construction | < 10ms | < 100ms | < 500ms |
| Noise generation | < 50ms | < 500ms | < 2s |
| Serialization (gzip) | < 20ms | < 200ms | < 1s |
| Client mesh build (JS) | < 100ms | < 1s | < 5s |
| Client render (WebGL) | 60fps | 30fps | 10fps+ |

For N > 1000, consider LOD: generate at full N, render at reduced N.

### 9.4 Dependencies

**C++ (third_party/)**: FastNoiseLite (MIT), pybind11 (BSD), stb_image_write.h (public domain), nlohmann/json.hpp (MIT), zlib (system).

**Python**: fastapi ≥ 0.100, uvicorn ≥ 0.20, pydantic ≥ 2.0, numpy ≥ 1.24.

**Client**: Three.js (CDN or npm). Vanilla JS + ES modules, no build step.

---

## 10. Phase 1 → 1.5 Transition: Wasm Unification

### 10.1 Why Immediately After Phase 1

Phase 1 produces two independent implementations of the mesh geometry: C++ (server-side) and vanilla JS (client-side). This dual-code problem gets worse with every feature added. Post-Phase 1, the mesh module will grow to include: differential operators (∇, ∇², divergence), resampling/decimation, mipmap chains, custom interpolation kernels, and flow-routing stencils. Maintaining two implementations of these is unsustainable.

**The fix**: compile the C++ `IcoMesh` to WebAssembly via Emscripten. The client loads the `.wasm` module and calls the same C++ code that the server uses. This gives 100% dual-end consistency, native-speed mesh computation in the browser, and a single codebase to maintain.

Do this transition while the surface area is small (~200 lines of mesh code). Waiting until Phase 2+ means migrating thousands of lines with subtle behavioral quirks baked in.

### 10.2 Wasm Architecture

```
C++ IcoMesh (single source of truth)
    │
    ├──► pybind11 → Python → FastAPI server
    │
    └──► Emscripten → .wasm + JS glue
              │
              └──► Browser: Web Worker loads wasm module
                     ├── buildPositions(N) → Float32Array (Transferable)
                     ├── buildFaces(N) → Uint32Array (Transferable)
                     ├── parseFILD(buffer) → Float32Array (Transferable)
                     └── (future) resample, mipmap, differential ops
```

The cross-validation test (C++ vs JS positions for N=10, max error < 1e-6) becomes trivially satisfied — it's the same code. Delete `icosahedral_geodesic.js` entirely.

### 10.3 N Must Be a Multiple of 16

The erosion algorithm requires hierarchical processing via mipmap-like decimation: N → N/2 → N/4 → N/8 → N/16. Each level must produce integer row widths (row width = 5r for caps, 5N for equator). This requires N to be divisible by 16 at minimum.

Constraining N = 16k for integer k ≥ 1 gives valid subdivisions at every level down to N/16. Practical values: 16, 32, 48, 64, 96, 128, 160, 192, 256, 320, 384, 512, 768, 1024, ...

The API should validate this and reject non-conforming N values with a clear error.

### 10.4 Interpolation Quality on Triangle Meshes

A fundamental challenge of icosahedral meshes: all primitives are triangles, so barycentric interpolation is **genuinely linear** (a flat plane through three values). On a quad mesh, bilinear interpolation is implicitly quadratic (product of two linears). This matters for:

- **Gradient estimation**: piecewise constant per triangle, discontinuous at edges. Naive finite differences produce staircase artifacts in flow routing.
- **Laplacian (∇²)**: cotangent-weight formula gives C⁰ at best. Diffusion exhibits visible faceting.
- **Resampling**: downsampling by area-averaging is fine, but upsampling introduces flat-shaded artifacts.

We will need custom higher-order interpolation kernels — likely local quadratic least-squares fits over 2-ring neighborhoods, projected into the tangent plane. This is exactly the kind of algorithm that must be identical server/client, reinforcing the Wasm unification argument.

### 10.5 Other Future-Proofing

**Mesh abstraction**: All endpoints accept `mesh_type`. Field binary format is mesh-agnostic (flat float array by cell ID). Adding flat/torus meshes requires only new mesh classes with the same interface.

**Multiple fields**: World store holds named fields. Currently "elevation". Same infrastructure for temperature, precipitation, biomes, etc.

**Pipeline extensibility**: Each generation stage (tectonics, erosion, climate) is a separate route module calling the C++ library through pybind.