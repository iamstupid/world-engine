# WorldEngine — Build Environment

## Platform

| Component | Value |
|-----------|-------|
| Host OS | Windows 11 Home China 10.0.26100 |
| Build Environment | WSL2 — Debian 13 (trixie) |
| WSL Kernel | 6.6.87.2-microsoft-standard-WSL2 |
| Architecture | x86_64 |
| WSL Distro Name | Debian |
| WSL User | zball (passwordless sudo via `/etc/sudoers.d/zball`) |

## Why WSL

The project outputs a web-based visualization (Three.js client served by FastAPI).
No Windows-native compilation is needed — everything builds and runs in Linux.
WSL provides a clean Linux toolchain without cross-compilation hassles.

## Toolchain

| Tool | Version | Source |
|------|---------|--------|
| GCC | 14.2.0 (Debian 14.2.0-19) | `apt: build-essential` |
| G++ | 14.2.0 (Debian 14.2.0-19) | `apt: build-essential` |
| CMake | 3.31.6 | `apt: cmake` |
| GNU Make | 4.4.1 | `apt: build-essential` |
| Python | 3.13.5 | `apt: python3` |
| pip | 26.0.1 | upgraded via pip |
| pkg-config | 1.8.1 | `apt: pkg-config` |
| zlib | 1.3.1 | `apt: zlib1g-dev` |
| git | 2.47.3 | pre-installed |
| curl | 8.14.1 | pre-installed |

## Python Virtual Environment

Location: `/mnt/d/game dev/world_engine/.venv`

Activate: `source .venv/bin/activate`

| Package | Version | Purpose |
|---------|---------|---------|
| fastapi | 0.129.0 | HTTP API server |
| uvicorn | 0.41.0 | ASGI server |
| pydantic | 2.12.5 | Request/response models |
| numpy | 2.4.2 | Array operations, field data |
| pybind11 | 3.0.2 | C++/Python bindings |

## C++ Third-Party Libraries (header-only, vendored)

| Library | License | Included via |
|---------|---------|-------------|
| FastNoiseLite | MIT | `third_party/FastNoiseLite/Cpp/FastNoiseLite.h` |
| pybind11 | BSD | pip install + CMake `find_package(pybind11)` |
| stb_image_write.h | Public Domain | `third_party/stb/stb_image_write.h` |
| nlohmann/json.hpp | MIT | `third_party/nlohmann/json.hpp` |
| zlib | zlib license | system (`apt: zlib1g-dev`) |

## Project Path

- Windows: `D:\game dev\world_engine\`
- WSL: `/mnt/d/game dev/world_engine/`

## Build Commands

```bash
# Enter WSL
wsl -d Debian

# Navigate to project
cd "/mnt/d/game dev/world_engine"

# Activate Python venv
source .venv/bin/activate

# Build C++ library
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
./test_ico_mesh
./test_noise
./test_serialize

# Start server
cd ../server
uvicorn app:app --reload --host 0.0.0.0 --port 8000
```

## C++ Standard

C++17 (`-std=c++17`) with `-Wall -Wextra -fPIC`.

## pybind11 CMake Integration

pybind11 is installed via pip. CMake finds it via:
```cmake
find_package(pybind11 REQUIRED HINTS "/mnt/d/game dev/world_engine/.venv/lib/python3.13/site-packages/pybind11/share/cmake/pybind11")
```

Or via `pybind11_DIR` variable.
