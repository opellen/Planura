# plnr_io: .plr native file-format serialization (m10-undo-file-io.task 1).
# Unlike plnr_agent, this DOES link Qt (QJsonDocument) -- it walks plnr_agent's
# Qt-free agent state and turns it into a QJsonDocument, which is inherently
# a Qt type. Kept as its own library, separate from plnr_agent, so
# plnr_agent's own Qt-free-by-policy guarantee (unit-testable without a
# QApplication) stays intact.
#
# plr_container.cpp (v1.M11, DESIGN.md decision 2) is the
# .plr ZIP container seam -- it links plnr_miniz (third_party/miniz/) PRIVATE:
# no plnr_io consumer needs a miniz type (plr_container.h's own public API is
# QByteArray/QJsonDocument/agent::AssetRepository only), so miniz's own include
# path does not leak past this library.
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(plnr_io STATIC
    plr_writer.cpp
    plr_reader.cpp
    plr_container.cpp
    obj_writer.cpp
    obj_reader.cpp
)

# PUBLIC and rooted at src/app (not src/app/io), same convention as
# plnr_agent's own target_include_directories -- every includer writes
# #include "io/plr_writer.h", #include "io/plr_format.h".
target_include_directories(plnr_io PUBLIC
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(plnr_io
    PUBLIC
        plnr_agent
        Qt6::Core
    PRIVATE
        plnr_miniz
)

target_compile_features(plnr_io PUBLIC cxx_std_20)
