add_executable(test_domain
  test_domain.cpp
)
target_link_libraries(test_domain PRIVATE world_engine_terrain)
add_test(NAME test_domain COMMAND test_domain)

add_executable(test_geodesic
  test_geodesic.cpp
)
target_link_libraries(test_geodesic PRIVATE world_engine_terrain)
add_test(NAME test_geodesic COMMAND test_geodesic)

add_executable(test_geodesic_physics
  test_geodesic_physics.cpp
)
target_link_libraries(test_geodesic_physics PRIVATE world_engine_terrain_procedural)
add_test(NAME test_geodesic_physics COMMAND test_geodesic_physics)

add_executable(test_tectonics
  test_tectonics.cpp
)
target_link_libraries(test_tectonics PRIVATE world_engine_terrain_procedural)
add_test(NAME test_tectonics COMMAND test_tectonics)

add_executable(test_determinism
  test_determinism.cpp
)
target_link_libraries(test_determinism PRIVATE world_engine_terrain_procedural)
add_test(NAME test_determinism COMMAND test_determinism)

add_executable(test_layers
  test_layers.cpp
)
target_link_libraries(test_layers PRIVATE world_engine_terrain_procedural)
add_test(NAME test_layers COMMAND test_layers)

add_executable(test_integration_generate
  test_integration_generate.cpp
)
target_link_libraries(test_integration_generate PRIVATE world_engine_terrain_procedural)
add_test(NAME test_integration_generate COMMAND test_integration_generate)
