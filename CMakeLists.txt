# Root CMake project for Planura.
cmake_minimum_required(VERSION 3.24)

project(planura LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(third_party/miniz)
# Ordo (MIT) is developed in its own repository. Consumption modes:
#   -DORDO_USE_INSTALLED=ON  resolve an installed package via find_package(ordo
#                            CONFIG) -- point CMAKE_PREFIX_PATH at the prefix
#   otherwise, source resolution order:
#   1. -DORDO_DIR=<path>   explicit override (also used for local co-development)
#   2. src/framework/ordo  vendored in-tree — the public mirror ships this copy,
#                          so a release tarball builds with no extra checkout
#   3. ../ordo             sibling checkout, the normal development layout
# Either way the app links the namespaced targets (ordo::core / ordo::qt),
# which exist in both modes (ALIAS in-tree, IMPORTED when installed).
option(ORDO_USE_INSTALLED "Resolve ordo via find_package instead of source" OFF)
if(ORDO_USE_INSTALLED)
    find_package(ordo CONFIG REQUIRED)
    message(STATUS "Ordo framework: installed package ${ordo_DIR} (v${ordo_VERSION})")
else()
    set(ORDO_DIR "" CACHE PATH "Path to the Ordo framework repository")
    if(NOT ORDO_DIR)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/framework/ordo/CMakeLists.txt")
            set(ORDO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/framework/ordo")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ordo/CMakeLists.txt")
            set(ORDO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ordo")
        else()
            message(FATAL_ERROR
                "Ordo not found. Check out the ordo repository next to this one, "
                "configure with -DORDO_DIR=<path-to-ordo>, or use "
                "-DORDO_USE_INSTALLED=ON with CMAKE_PREFIX_PATH.")
        endif()
    endif()
    message(STATUS "Ordo framework: ${ORDO_DIR}")
    add_subdirectory("${ORDO_DIR}" "${CMAKE_BINARY_DIR}/ordo")
endif()
add_subdirectory(src/geometry)
add_subdirectory(src/app)
add_subdirectory(tests)
