# WorldEngine Terrain

Procedural spherical terrain generator in C++ with deterministic seed control.

Implemented pipeline:

1. Seamless spherical noise seed.
2. Tectonics stage (Lagrangian plate simulation on an icosahedral geodesic grid: subduction, ridges, collision suturing, rifting).
3. Base elevation combine.
4. Analytical erosion stage (fixed-point + multigrid-inspired passes).
5. Hydrology products (receiver + flow accumulation + river mask).
6. Ocean/lake masks.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release --parallel
```

## Test

```bash
ctest --test-dir build -C Release --output-on-failure
```

## CLI Usage

Preview world:

```bash
./build/tools/terrain_cli/Release/terrain_cli.exe --preview --seed 42 --out output_preview
```

Full 4096x2048 smoke run:

```bash
./build/tools/terrain_cli/Release/terrain_cli.exe --seed 123 --width 4096 --height 2048 --tect-freq 250 --phys-freq 704 --out output_4k
```

CLI outputs:

- `elevation_eroded.png` (16-bit grayscale)
- `elevation_color.png` (8-bit RGB gradient by sea level)
- `ocean_mask.png`
- `river_mask.png`
- `lake_mask.png`

Optional legacy PGM export:

```bash
./build/tools/terrain_cli/Release/terrain_cli.exe --preview --write-pgm --out output_preview
```

## Docs

- Architecture: `docs/ARCHITECTURE.md`
- Algorithms and assumptions: `docs/ALGORITHMS.md`
- API draft: `docs/API.md`
