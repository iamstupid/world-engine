# WorldEngine core library

if(EMSCRIPTEN)
    # --- WASM target ---
    add_executable(worldengine_wasm
        mesh/ico_topology.cpp
        noise/noise_generator.cpp
        tectonics/plate_assign.cpp
        io/serialize.cpp
        integration/wasm_bindings.cpp
    )

    target_include_directories(worldengine_wasm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${THIRD_PARTY_DIR}/FastNoiseLite/Cpp
    )

    # USE_ZLIB must be both a compile and link flag for Emscripten ports
    target_compile_options(worldengine_wasm PRIVATE -sUSE_ZLIB=1)

    target_link_options(worldengine_wasm PRIVATE
        --bind
        -sUSE_ZLIB=1
        -sMODULARIZE=1
        -sEXPORT_NAME=WorldEngineModule
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=worker
        -sNO_FILESYSTEM=1
        -sWASM=1
        -sEXPORTED_FUNCTIONS=_malloc,_free
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8"
        -O2
    )

    set_target_properties(worldengine_wasm PROPERTIES
        OUTPUT_NAME "worldengine"
        SUFFIX ".js"
    )

    # Copy build outputs to client/wasm/
    add_custom_command(TARGET worldengine_wasm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/client/wasm
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:worldengine_wasm>/worldengine.js
            ${CMAKE_SOURCE_DIR}/client/wasm/worldengine.js
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:worldengine_wasm>/worldengine.wasm
            ${CMAKE_SOURCE_DIR}/client/wasm/worldengine.wasm
    )

else()
    # --- Native build ---
    add_library(worldengine SHARED
        legacy/mesh/icosahedral_geodesic.cpp
        mesh/ico_topology.cpp
        legacy/noise/noise_generator.cpp
        noise/noise_generator.cpp
        tectonics/plate_assign.cpp
        legacy/io/serialize.cpp
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

    add_executable(test_ico_topology tests/test_ico_topology.cpp)
    target_link_libraries(test_ico_topology PRIVATE worldengine)

    add_executable(test_ico_iterator tests/test_ico_iterator.cpp)
    target_link_libraries(test_ico_iterator PRIVATE worldengine)

    add_executable(test_ico_mesh_v2 tests/test_ico_mesh_v2.cpp)
    target_link_libraries(test_ico_mesh_v2 PRIVATE worldengine)

    add_executable(test_serialize_v2 tests/test_serialize_v2.cpp)
    target_link_libraries(test_serialize_v2 PRIVATE worldengine)

    add_executable(test_v1_v2_equivalence tests/test_v1_v2_equivalence.cpp)
    target_link_libraries(test_v1_v2_equivalence PRIVATE worldengine)

    add_executable(test_locate tests/test_locate.cpp)
    target_link_libraries(test_locate PRIVATE worldengine)

    add_executable(test_plate_assign tests/test_plate_assign.cpp)
    target_link_libraries(test_plate_assign PRIVATE worldengine)

    # Visual tests
    add_executable(bench_neighbors tests/bench_neighbors.cpp)
    target_link_libraries(bench_neighbors PRIVATE worldengine)

    add_executable(vis_ico_mesh tests/visual/vis_ico_mesh.cpp)
    target_link_libraries(vis_ico_mesh PRIVATE worldengine)

    add_executable(vis_noise_on_ico tests/visual/vis_noise_on_ico.cpp)
    target_link_libraries(vis_noise_on_ico PRIVATE worldengine)

endif()
