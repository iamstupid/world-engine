add_executable(terrain_cli
  main.cpp
  ${CMAKE_SOURCE_DIR}/third_party/lodepng/lodepng.cpp
)

target_link_libraries(terrain_cli
  PRIVATE
    world_engine_terrain_procedural
)

target_include_directories(terrain_cli
  PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/lodepng
)

target_compile_features(terrain_cli PRIVATE cxx_std_20)
