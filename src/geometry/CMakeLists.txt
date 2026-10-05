# plnr_geo: pure C++ geometry kernel. Must never link or include Qt.
add_library(plnr_geo STATIC
    src/model.cpp
    src/loop_finder.cpp
    src/pick.cpp
    src/infer.cpp
    src/triangulate.cpp
    src/select.cpp
    src/measure.cpp
    src/scene.cpp
    src/scene_ops.cpp
    src/scene_pick.cpp
    src/shapes.cpp
    src/offset.cpp
    src/follow.cpp
    src/intersect.cpp
    src/solid.cpp
    src/csg.cpp
)

target_include_directories(plnr_geo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(plnr_geo PUBLIC cxx_std_20)
