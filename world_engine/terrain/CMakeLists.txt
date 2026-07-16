add_library(world_engine_terrain
  src/terrain_domain.cpp
  src/terrain_dataset.cpp
  src/geodesic_grid.cpp
)

target_include_directories(world_engine_terrain
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(world_engine_terrain PUBLIC cxx_std_20)
