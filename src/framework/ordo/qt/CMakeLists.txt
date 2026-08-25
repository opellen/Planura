# ordo_qt: Qt bridge layer built on top of ordo_core.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)

# presenter.h is listed explicitly (not just included by presenter.cpp) because
# CMake AUTOMOC's basename-pairing only looks for a matching header in the same
# directory as its .cpp; this target splits public include/ from src/, so
# without this the Q_OBJECT class in presenter.h is silently never moc'd.
add_library(ordo_qt STATIC
    src/qt_bridge.cpp
    src/presenter.cpp
    src/presenter_host.cpp
    include/ordo/qt/presenter.h
)

target_include_directories(ordo_qt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ordo_qt
    PUBLIC
        ordo_core
        Qt6::Core
        Qt6::Widgets
)
