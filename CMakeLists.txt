cmake_minimum_required(VERSION 3.24)

project(WorldEngineTerrain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WE_BUILD_TOOLS "Build tools (CLI/server stubs)" ON)
option(WE_BUILD_TESTS "Build tests" ON)

add_subdirectory(world_engine/terrain)
add_subdirectory(world_engine/terrain/procedural)

if (WE_BUILD_TOOLS)
  add_subdirectory(tools/terrain_cli)
endif()

if (WE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
