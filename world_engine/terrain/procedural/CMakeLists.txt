find_package(OpenMP)

add_library(world_engine_terrain_procedural
  src/pipeline.cpp
  src/param_schema.cpp
  src/graph_ops.cpp
  src/procedural_generator.cpp
  src/stages/noise_stage.cpp
  src/stages/tectonics_stage.cpp
  src/stages/analytical_erosion_stage.cpp
  src/stages/geodesic_physics_stage.cpp
  src/stages/hydrology_stage.cpp
  src/stages/masks_stage.cpp
)

target_include_directories(world_engine_terrain_procedural
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/world_engine/terrain/include
    ${CMAKE_SOURCE_DIR}/third_party/FastNoiseLite
)

target_link_libraries(world_engine_terrain_procedural
  PUBLIC
    world_engine_terrain
)

if(OpenMP_CXX_FOUND)
  target_link_libraries(world_engine_terrain_procedural PUBLIC OpenMP::OpenMP_CXX)
endif()

target_compile_features(world_engine_terrain_procedural PUBLIC cxx_std_20)
