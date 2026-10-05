# plnr_agent: application domain layer (Agents + Commands) for the
# planura shell. Links ordo::core + plnr_geo only -- must never link or
# include Qt (Qt-free by policy, so it can be unit-tested without a QApplication).
#
# document_commands.cpp is DELIBERATELY NOT listed here:
# OpenDocumentCommand/SaveDocumentCommand need plnr_io (io::readDocument/
# writeDocument) + Qt (QFile/QSaveFile/QDateTime/QJsonDocument), and plnr_io
# already links plnr_agent -- adding it here would be circular besides
# violating the Qt-free rule. document_commands.cpp is compiled straight into
# the planura executable instead (src/app/CMakeLists.txt), same
# pattern the source layout documents for tools/vcb_parser.cpp/ui/context_menu.cpp.
# camera_store.cpp/document_store.cpp themselves are Qt-free (plain
# geo::Vec3/double/std::string/bool state) and belong here like every other
# Agent.
#
# undo_commands.cpp (and its new transaction.cpp sibling) moved IN
# here after its rework: the halted
# snapshot-era implementation needed plnr_io/Qt (JSON snapshot serialization),
# so it used to sit next to document_commands.cpp in the executable's own
# source list instead -- see src/app/CMakeLists.txt's own history/comment.
# The delta-journaling rework applies TransactionDelta directly via the geo
# restore/rollback primitives and each Agent's own restore API, with no
# JSON/Qt involved at all, so both files are genuinely Qt-free like every
# other Agent/Command pair here.
add_library(plnr_agent STATIC
    geometry_api.cpp
    command/geometry_commands.cpp
    selection_store.cpp
    command/selection_commands.cpp
    tag_store.cpp
    command/tag_commands.cpp
    command/group_commands.cpp
    edit_context_store.cpp
    command/edit_context_commands.cpp
    guide_store.cpp
    command/guide_commands.cpp
    axes_store.cpp
    command/axes_commands.cpp
    annotation_store.cpp
    command/annotation_commands.cpp
    section_store.cpp
    command/section_commands.cpp
    material_repository.cpp
    command/material_commands.cpp
    # The document's rendering-style state (face style + edge flags + default
    # front/back colors). Qt-free like every other Agent/Command pair here.
    style_store.cpp
    command/style_commands.cpp
    # Sun-position math
    # (Qt-free, agent::-free -- see sun_position.h's own file comment) + the
    # document's shadow/sun-shading + ground-shadow settings, mirroring
    # style_store.cpp/style_commands.cpp's own shape just above.
    sun_position.cpp
    shadow_store.cpp
    command/shadow_commands.cpp
    # The document's fog settings -- its own separate Agent (see FogStore's
    # own class comment for why it isn't folded into ShadowStore), same
    # shape as shadow_store.cpp/shadow_commands.cpp just above.
    fog_store.cpp
    command/fog_commands.cpp
    # Content-addressed
    # texture asset storage -- both Qt-free (sha256.cpp is a from-spec
    # SHA-256 implementation used only by asset_repository.cpp's own content-hash
    # naming; material_texture_commands.cpp, which NEEDS Qt for QFile, is
    # deliberately NOT listed here, same document_commands.cpp/
    # obj_commands.cpp exclusion this file's own top comment documents).
    asset_repository.cpp
    sha256.cpp
    camera_store.cpp
    document_store.cpp
    undo_store.cpp
    transaction.cpp
    command/undo_commands.cpp

    # --- Solid Tools ---

    # Solid Tools
    # (Union/Outer Shell/Subtract/Trim/Intersect/Split). The heavy CSG
    # validation/execution/materialization lives in
    # GeometryApi::applySolidOp (geometry_api.cpp, already listed above)
    # -- solid_commands.cpp is just the thin orchestrating Command, same
    # "Qt-free, links plnr_geo's geo::csg directly" story as every other
    # Agent/Command pair here.
    command/solid_commands.cpp
)

# PUBLIC and rooted at src/app (not src/app/agent) so every includer --
# inside this lib, the app executable, and tests alike -- writes
# #include "agent/events.h", #include "agent/geometry_api.h", etc.
target_include_directories(plnr_agent PUBLIC
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(plnr_agent PUBLIC
    ordo::core
    plnr_geo
)

target_compile_features(plnr_agent PUBLIC cxx_std_20)
