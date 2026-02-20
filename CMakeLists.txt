cmake_minimum_required(VERSION 3.18)
project(worldengine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Find zlib
find_package(ZLIB REQUIRED)

# Find pybind11 from venv
find_package(pybind11 REQUIRED)

# Third-party include dirs
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

add_subdirectory(library)
