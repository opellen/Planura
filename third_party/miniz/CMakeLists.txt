# Vendored miniz (v1.M11, DESIGN.md decision 2's ZIP
# container work) -- see README.md in this directory for full provenance
# (upstream URL, pinned commit, version, license). Summary: miniz 3.1.2,
# https://github.com/richgel999/miniz.git @ 77d0dce8627735138c51770d1799a1ef48f2117d,
# public domain (Unlicense) -- the exact same commit openskp's C++ package
# pins via CMake FetchContent; vendored here as physical files instead.
#
# miniz is C, not C++ -- enable_language(C) here (rather than at the root
# CMakeLists.txt) keeps the C toolchain requirement scoped to exactly this
# vendored dependency; every other target in this repo stays C++-only.
enable_language(C)

add_library(plnr_miniz STATIC
    miniz.c
    miniz_tdef.c
    miniz_tinfl.c
    miniz_zip.c
)

# PUBLIC: plnr_io's own .cpp files need to #include "miniz_zip.h" etc.
# directly (no wrapper header from this directory) -- io/CMakeLists.txt links
# plnr_miniz PRIVATE, so this include path does not leak past plnr_io itself.
target_include_directories(plnr_miniz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# PRIVATE, not PUBLIC: a C-language feature requirement must not propagate to
# a linking consumer that has no C sources of its own (plnr_io is C++-only) --
# CMake's per-target C-feature check chokes ("No known features for C
# compiler") when a PUBLIC C feature requirement reaches a target CMake never
# identified a C compiler for. plnr_miniz's own .c files still compile as C11
# either way; PRIVATE just stops the requirement from leaking into plnr_io.
target_compile_features(plnr_miniz PRIVATE c_std_11)
