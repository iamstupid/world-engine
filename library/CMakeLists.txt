# WorldEngine core library
add_library(worldengine SHARED
    mesh/icosahedral_geodesic.cpp
    noise/noise_generator.cpp
    io/serialize.cpp
)

target_include_directories(worldengine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THIRD_PARTY_DIR}/FastNoiseLite/Cpp
    ${THIRD_PARTY_DIR}/stb
    ${THIRD_PARTY_DIR}/nlohmann
)

target_link_libraries(worldengine PUBLIC ZLIB::ZLIB)

# pybind11 Python module
pybind11_add_module(worldengine_py integration/python_bindings.cpp)
target_link_libraries(worldengine_py PRIVATE worldengine)

# Unit tests
add_executable(test_ico_mesh tests/test_ico_mesh.cpp)
target_link_libraries(test_ico_mesh PRIVATE worldengine)

add_executable(test_noise tests/test_noise.cpp)
target_link_libraries(test_noise PRIVATE worldengine)

add_executable(test_serialize tests/test_serialize.cpp)
target_link_libraries(test_serialize PRIVATE worldengine)

# Visual tests
add_executable(vis_ico_mesh tests/visual/vis_ico_mesh.cpp)
target_link_libraries(vis_ico_mesh PRIVATE worldengine)

add_executable(vis_noise_on_ico tests/visual/vis_noise_on_ico.cpp)
target_link_libraries(vis_noise_on_ico PRIVATE worldengine)
