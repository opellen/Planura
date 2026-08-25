# ordo_core: pure C++ core library. Must never link or include Qt.
add_library(ordo_core STATIC
    src/version.cpp
    src/dispatcher.cpp
    src/agent.cpp
    src/agent_context.cpp
    src/app_kernel.cpp
)

target_include_directories(ordo_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ordo_core PUBLIC cxx_std_20)
