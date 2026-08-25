# Root CMake project for Planura.
cmake_minimum_required(VERSION 3.24)

project(planura LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(third_party/miniz)
add_subdirectory(src/framework/ordo)
add_subdirectory(src/geometry)
add_subdirectory(src/app)
add_subdirectory(tests)
