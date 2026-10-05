# Test suite. Uses GoogleTest fetched at configure time; must not require Qt.
include(FetchContent)

# Match the runtime library gtest is built with to avoid ABI mismatches on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(ordo_version_test
    ordo/ordo_version_test.cpp
)

target_link_libraries(ordo_version_test
    PRIVATE
        ordo::core
        GTest::gtest_main
)

gtest_discover_tests(ordo_version_test)

add_executable(ordo_dispatcher_test
    ordo/dispatcher_test.cpp
)

target_link_libraries(ordo_dispatcher_test
    PRIVATE
        ordo::core
        GTest::gtest_main
)

gtest_discover_tests(ordo_dispatcher_test)

add_executable(ordo_store_test
    ordo/store_test.cpp
)

target_link_libraries(ordo_store_test
    PRIVATE
        ordo::core
        GTest::gtest_main
)

gtest_discover_tests(ordo_store_test)

add_executable(ordo_command_test
    ordo/command_test.cpp
)

target_link_libraries(ordo_command_test
    PRIVATE
        ordo::core
        GTest::gtest_main
)

gtest_discover_tests(ordo_command_test)

add_executable(ordo_kernel_test
    ordo/kernel_test.cpp
)

target_link_libraries(ordo_kernel_test
    PRIVATE
        ordo::core
        GTest::gtest_main
)

gtest_discover_tests(ordo_kernel_test)

# Unlike the pure-core tests above, this one links ordo::qt (and thus Qt6::Core)
# to exercise the Presenter adapter. No QApplication needed -- plain QObject use.
add_executable(ordo_presenter_test
    ordo/presenter_test.cpp
)

target_link_libraries(ordo_presenter_test
    PRIVATE
        ordo::qt
        GTest::gtest_main
)

# PRE_TEST: the default POST_BUILD discovery mode runs this executable right
# after linking, at build time, to enumerate its tests -- but this exe needs
# Qt DLLs on PATH to even start, and the build step is not guaranteed to have
# that. PRE_TEST defers discovery (running the exe) to ctest invocation time.
gtest_discover_tests(ordo_presenter_test DISCOVERY_MODE PRE_TEST)

set(CMAKE_AUTOMOC ON)  # view_model_test defines a Q_OBJECT class in-file
add_executable(ordo_view_model_test
    ordo/view_model_test.cpp
)
target_link_libraries(ordo_view_model_test
    PRIVATE
        ordo::qt
        GTest::gtest_main
)
gtest_discover_tests(ordo_view_model_test DISCOVERY_MODE PRE_TEST)

# plnr_camera_test links Qt6::Gui directly (only what Camera needs -- no
# ordo::qt/ordo::core) and compiles camera.cpp straight out of src/app/viewport,
# since Camera has no library target of its own yet. Same PRE_TEST rationale
# as ordo_presenter_test above: the binary needs Qt DLLs on PATH to start.
find_package(Qt6 REQUIRED COMPONENTS Gui)

add_executable(plnr_camera_test
    ordo/camera_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/viewport/camera.cpp
)

target_include_directories(plnr_camera_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(plnr_camera_test
    PRIVATE
        Qt6::Gui
        GTest::gtest_main
)

gtest_discover_tests(plnr_camera_test DISCOVERY_MODE PRE_TEST)

add_executable(geo_vec3_test
    geo/vec3_test.cpp
)

target_link_libraries(geo_vec3_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_vec3_test)

add_executable(geo_model_test
    geo/model_test.cpp
)

target_link_libraries(geo_model_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_model_test)

add_executable(geo_loop_test
    geo/loop_test.cpp
)

target_link_libraries(geo_loop_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_loop_test)

add_executable(geo_extrude_test
    geo/extrude_test.cpp
)

target_link_libraries(geo_extrude_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_extrude_test)

add_executable(geo_translate_test
    geo/translate_test.cpp
)

target_link_libraries(geo_translate_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_translate_test)

add_executable(geo_transform_test
    geo/transform_test.cpp
)

target_link_libraries(geo_transform_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_transform_test)

add_executable(geo_transform_vertices_test
    geo/transform_vertices_test.cpp
)

target_link_libraries(geo_transform_vertices_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_transform_vertices_test)

add_executable(geo_pick_test
    geo/pick_test.cpp
)

target_link_libraries(geo_pick_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_pick_test)

add_executable(geo_select_test
    geo/select_test.cpp
)

target_link_libraries(geo_select_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_select_test)

add_executable(geo_shapes_test
    geo/shapes_test.cpp
)

target_link_libraries(geo_shapes_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_shapes_test)

add_executable(geo_scene_test
    geo/scene_test.cpp
)

target_link_libraries(geo_scene_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_scene_test)

add_executable(geo_scene_ops_test
    geo/scene_ops_test.cpp
)

target_link_libraries(geo_scene_ops_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_scene_ops_test)

add_executable(geo_scene_pick_test
    geo/scene_pick_test.cpp
)

target_link_libraries(geo_scene_pick_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_scene_pick_test)

add_executable(geo_infer_test
    geo/infer_test.cpp
)

target_link_libraries(geo_infer_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_infer_test)

add_executable(geo_triangulate_test
    geo/triangulate_test.cpp
)

target_link_libraries(geo_triangulate_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_triangulate_test)

add_executable(geo_measure_test
    geo/measure_test.cpp
)

target_link_libraries(geo_measure_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_measure_test)

add_executable(geo_offset_test
    geo/offset_test.cpp
)

target_link_libraries(geo_offset_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_offset_test)

add_executable(geo_follow_test
    geo/follow_test.cpp
)

target_link_libraries(geo_follow_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_follow_test)

add_executable(geo_intersect_test
    geo/intersect_test.cpp
)

target_link_libraries(geo_intersect_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_intersect_test)

add_executable(geo_split_edge_test
    geo/split_edge_test.cpp
)

target_link_libraries(geo_split_edge_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_split_edge_test)

add_executable(geo_restore_test
    geo/restore_test.cpp
)

target_link_libraries(geo_restore_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_restore_test)

add_executable(geo_journal_test
    geo/journal_test.cpp
)

target_link_libraries(geo_journal_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_journal_test)

# agent_geometry_api_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_geometry_api_test
    agent/geometry_api_test.cpp
)

target_link_libraries(agent_geometry_api_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_geometry_api_test)

# agent_selection_store_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_selection_store_test
    agent/selection_store_test.cpp
)

target_link_libraries(agent_selection_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_selection_store_test)

# agent_tag_store_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_tag_store_test
    agent/tag_store_test.cpp
)

target_link_libraries(agent_tag_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_tag_store_test)

# agent_group_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_group_test
    agent/group_test.cpp
)

target_link_libraries(agent_group_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_group_test)

# agent_transform_entities_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::transformEntities/applyArrayTimes/applyArrayDivide (task
# 1.2). No Qt, so plain POST_BUILD discovery (the default) is fine here.
add_executable(agent_transform_entities_test
    agent/transform_entities_test.cpp
)

target_link_libraries(agent_transform_entities_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_transform_entities_test)

# agent_followme_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::followMe. No Qt, so plain POST_BUILD discovery
# (the default) is fine here.
add_executable(agent_followme_test
    agent/followme_test.cpp
)

target_link_libraries(agent_followme_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_followme_test)

# agent_splitting_pass_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::runSplittingPass through addEdge/
# addRectangle/addPolyline plus GeometryApi::divideEdge, and confirms
# followMe deliberately does not run the pass. No Qt, so plain POST_BUILD
# discovery (the default) is fine here.
add_executable(agent_splitting_pass_test
    agent/splitting_pass_test.cpp
)

target_link_libraries(agent_splitting_pass_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_splitting_pass_test)

# agent_edit_context_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_edit_context_test
    agent/edit_context_test.cpp
)

target_link_libraries(agent_edit_context_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_edit_context_test)

# agent_text3d_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::add3dText with synthetic outlines only -- Text3dDialog's Qt
# font polygonization is not under test here (that half stays Qt-free-suite-
# untestable by design; see tests/agent/text3d_test.cpp's own comment). No
# Qt, so plain POST_BUILD discovery (the default) is fine here.
add_executable(agent_text3d_test
    agent/text3d_test.cpp
)

target_link_libraries(agent_text3d_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_text3d_test)

# agent_vcb_parser_test: Qt-free -- vcb_parser.{h,cpp} (src/app/tools) is
# deliberately std-only (see its header comment) and has no library target
# of its own, so this compiles vcb_parser.cpp straight into the test binary,
# same "compile the .cpp directly" pattern as plnr_camera_test above but with
# zero dependencies (not even Qt) instead of Qt6::Gui. Plain POST_BUILD
# discovery is fine -- no Qt DLLs to find at start-up.
add_executable(agent_vcb_parser_test
    agent/vcb_parser_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/tools/vcb_parser.cpp
)

target_include_directories(agent_vcb_parser_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(agent_vcb_parser_test
    PRIVATE
        GTest::gtest_main
)

gtest_discover_tests(agent_vcb_parser_test)

# agent_guide_store_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises GuideStore
# plus its AddGuideLine/AddGuidePoint/EraseGuide/DeleteAllGuides/
# SetGuideHidden commands end to end. No Qt, so plain POST_BUILD discovery
# (the default) is fine here.
add_executable(agent_guide_store_test
    agent/guide_store_test.cpp
)

target_link_libraries(agent_guide_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_guide_store_test)

# agent_axes_store_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises AxesStore
# plus its SetAxes/ResetAxes commands end to end, including the
# deterministic re-orthonormalization construction rule. No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_axes_store_test
    agent/axes_store_test.cpp
)

target_link_libraries(agent_axes_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_axes_store_test)

# agent_annotation_store_test: Qt-free, links plnr_agent directly (ordo::core
# + plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# AnnotationStore plus its Add/SetText/Remove/RemoveAll commands
# end to end, including GeometryChangedCommand's chained
# PruneSelectionCommand-replay + refreshAssociations reaction driven through
# a real GeometryApi (add/remove/move an edge's vertices). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(agent_annotation_store_test
    agent/annotation_store_test.cpp
)

target_link_libraries(agent_annotation_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_annotation_store_test)

# agent_section_store_test: Qt-free, links plnr_agent directly (ordo::core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises SectionStore
# plus its Add/Remove/SetActive/Reverse/SetHidden commands end to
# end, including the single-active-cut invariant. No Qt, so plain POST_BUILD
# discovery (the default) is fine here.
add_executable(agent_section_store_test
    agent/section_store_test.cpp
)

target_link_libraries(agent_section_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_section_store_test)

# plnr_context_menu_test: buildContextMenu() (src/app/ui/
# context_menu.cpp) is a pure function over tools::ToolContext's read
# accessors, but tool.h's PointerEvent carries a QPointF -- same "app .cpp
# with no library target of its own yet, compile it straight into the test
# binary" precedent as plnr_camera_test above, just linking plnr_agent (Qt-
# free: ordo::core + plnr_geo + the src/app include root) for the agent/geo
# types instead of nothing. Qt6::Core only (QPointF needs no widgets/GL).
# Same PRE_TEST rationale as ordo_presenter_test/plnr_camera_test: the binary
# needs Qt DLLs on PATH to even start.
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(plnr_context_menu_test
    agent/context_menu_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ui/context_menu.cpp
)

target_include_directories(plnr_context_menu_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(plnr_context_menu_test
    PRIVATE
        plnr_agent
        Qt6::Core
        GTest::gtest_main
)

gtest_discover_tests(plnr_context_menu_test DISCOVERY_MODE PRE_TEST)

# plnr_plr_writer_test: Links plnr_io (which
# pulls plnr_agent -- ordo::core + plnr_geo -- transitively, plus Qt6::Core for
# QJsonDocument). Same PRE_TEST rationale as plnr_camera_test/
# plnr_context_menu_test above: the binary needs Qt DLLs on PATH just to
# start.
add_executable(plnr_plr_writer_test
    io/plr_writer_test.cpp
)

target_link_libraries(plnr_plr_writer_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(plnr_plr_writer_test DISCOVERY_MODE PRE_TEST)

# plnr_plr_reader_test: Same plnr_io/Qt-DLLs-
# on-PATH rationale as plnr_plr_writer_test above (PRE_TEST discovery).
# plnr_miniz joins PRIVATE --
# this test's own AssetContentHashMismatchRejectsAllOrNothing case hand-forges
# a minimal ZIP archive directly via the same miniz writer API
# io::assembleContainer itself uses (io/plr_container.cpp), since a
# content-hash-mismatched asset entry can never be produced through the
# normal write path (assembleContainer's own AssetRepository::get() gate).
add_executable(plnr_plr_reader_test
    io/plr_reader_test.cpp
)

target_link_libraries(plnr_plr_reader_test
    PRIVATE
        plnr_io
        plnr_miniz
        GTest::gtest_main
)

gtest_discover_tests(plnr_plr_reader_test DISCOVERY_MODE PRE_TEST)

# plnr_plr_container_test: Exercises io::assembleContainer/io::openContainer directly (raw-vs-zip
# decision, entry order/determinism, round-trip, corrupt/malformed-archive
# rejection) -- no MaterialRepository/full-document round trip here, that
# integration lives in plr_reader_test.cpp's own PlrReaderZipTest/
# PlrReaderZipErrorTest. Links plnr_io + plnr_miniz PRIVATE (the entry-order
# and missing-model.json tests use the miniz reader/writer API directly,
# independent of assembleContainer/openContainer's own internals). Same
# PRE_TEST discovery rationale as every other plnr_io-linked test here: Qt
# DLLs need to be on PATH just to start.
add_executable(plnr_plr_container_test
    io/plr_container_test.cpp
)

target_link_libraries(plnr_plr_container_test
    PRIVATE
        plnr_io
        plnr_miniz
        GTest::gtest_main
)

gtest_discover_tests(plnr_plr_container_test DISCOVERY_MODE PRE_TEST)

# agent_document_test: Exercises DocumentStore/
# CameraStore and New/Open/SaveDocumentCommand end to end (QTemporaryDir
# round-trip, dirty-flag transitions, open/save failure paths). Links plnr_io
# (which pulls plnr_agent + Qt6::Core transitively) for
# io::readDocument/writeDocument. document_commands.cpp is compiled straight
# into this binary rather than linked from a library target -- it
# deliberately isn't part of plnr_agent (see agent/CMakeLists.txt's own
# comment: it needs plnr_io + Qt, which plnr_agent must never link) -- same
# "app .cpp with no library target of its own" precedent as
# plnr_camera_test/plnr_context_menu_test above. Qt6::Core (via plnr_io) means
# this binary needs Qt DLLs on PATH just to start, hence PRE_TEST discovery.
add_executable(agent_document_test
    agent/document_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
)

target_link_libraries(agent_document_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(agent_document_test DISCOVERY_MODE PRE_TEST)

# agent_undo_test: Exercises
# Transaction/UndoStore/UndoCommand/RedoCommand end to end through
# real events (draw/tag/guide/axes/section/dimension/hide/group/explode/
# move-instance gestures, undo/redo, abort() at the unit level). Unlike
# agent_document_test, undo_commands.cpp/transaction.cpp are NOT
# compiled directly into this binary -- the task-3 rework made them Qt-free,
# so they now live in plnr_agent proper (agent/CMakeLists.txt) and arrive
# here transitively via plnr_io -> plnr_agent. This binary still links plnr_io
# (rather than plnr_agent directly) and still compiles
# document_commands.cpp directly in, for two things ONLY plnr_io/Qt provide:
# io::writeDocument byte-level comparison (undo/redo round-trip bytes) and
# the New/Open-clears-stacks test case. Same PRE_TEST discovery rationale as
# agent_document_test: Qt DLLs need to be on PATH just to start.
add_executable(agent_undo_test
    agent/undo_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
)

target_link_libraries(agent_undo_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(agent_undo_test DISCOVERY_MODE PRE_TEST)

# plnr_obj_test: Writer
# golden/visibility tests, reader grammar/error tests, an export->readObj->
# GeometryApi::importMesh structural round-trip, and command-level
# ImportObjRequested/ExportObjRequested coverage (undo/redo, dirty
# tracking) driven through a real (minimal) kernel. Links plnr_io (pulls
# plnr_agent + Qt6::Core transitively) and compiles agent/command/obj_commands.cpp
# directly in -- same "app .cpp with no library target of its own" precedent
# as document_commands.cpp/plnr_plr_writer_test above (ExportObjCommand/
# ImportObjCommand need plnr_io + Qt, which plnr_agent must never link). Same
# PRE_TEST discovery rationale: the binary needs Qt DLLs on PATH just to
# start.
add_executable(plnr_obj_test
    io/obj_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/obj_commands.cpp
)

target_link_libraries(plnr_obj_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(plnr_obj_test DISCOVERY_MODE PRE_TEST)

# agent_material_test: includes AssetRepository + MaterialSetTextureCommand
# coverage. Exercises MaterialRepository directly (create/edit/paint/setActive/
# setTexture/clearTexture + restore-API rules), then end to end through real
# events (undo/redo of create/edit/paint/setTexture via UndoCaptureCommand,
# dirty tracking via MarkDirtyCommand<MaterialsChanged>, aux-diff capture via
# Transaction, SetActiveMaterialRequested's deliberate non-undo/
# non-dirty exclusion, and .plr round-trip via New/Open/SaveDocumentCommand
# plus direct io::writeDocument/readDocument calls for the schema-level
# contract tests). Same "needs plnr_io + document_commands.cpp compiled
# straight in" precedent as agent_document_test/agent_undo_test above
# (io::writeDocument/readDocument + Qt6::Core via plnr_io means this binary
# needs Qt DLLs on PATH just to start -- PRE_TEST discovery).
# material_texture_commands.cpp joins for the same reason document_commands.cpp
# is here -- MaterialSetTextureCommand needs Qt (QFile) but no library target
# of its own (agent/command/material_texture_commands.h's own placement comment).
add_executable(agent_material_test
    agent/material_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/material_texture_commands.cpp
)

target_link_libraries(agent_material_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(agent_material_test DISCOVERY_MODE PRE_TEST)

# agent_asset_repository_test: Qt-free, links plnr_agent directly (ordo::core transitively) -- content-hash
# stability/dedupe/restore-verification-mismatch rules, no kernel
# registration needed (AssetRepository never dispatches, see its own class
# comment). No Qt, so plain POST_BUILD discovery is fine here.
add_executable(agent_asset_repository_test
    agent/asset_repository_test.cpp
)

target_link_libraries(agent_asset_repository_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_asset_repository_test)

# agent_sun_position_test: Qt-free AND kernel-free (sun_position.h/.cpp is pure
# value-in/value-out math, no Agent/Command involved) -- links plnr_agent
# directly (for geo::Vec3 transitively via plnr_geo), same POST_BUILD
# discovery as agent_asset_repository_test above.
add_executable(agent_sun_position_test
    agent/sun_position_test.cpp
)

target_link_libraries(agent_sun_position_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_sun_position_test)

# agent_shadow_store_test: Qt-free, links plnr_agent directly (ordo::core
# transitively) -- exercises ShadowStore directly (setEnabled/setPosition/
# setDateTime, restore-API rules, isAllDefault()) plus the event/command
# chain via a fresh Kernel, same shape as agent_style_store_test just
# above (ShadowStore mirrors StyleStore's own contract field-for-field). The
# deliberate "dirty yes, undo no" composition is proven at the integration
# level instead -- tests/agent/undo_test.cpp's own
# ShadowChangeIsDirtyButNeverCreatesItsOwnUndoStep, same per-agent-file-vs-
# undo_test.cpp split agent_style_store_test already follows. No Qt, so
# plain POST_BUILD discovery is fine here.
add_executable(agent_shadow_store_test
    agent/shadow_store_test.cpp
)

target_link_libraries(agent_shadow_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_shadow_store_test)

# agent_fog_store_test: Qt-free, links plnr_agent directly (ordo::core
# transitively) -- exercises FogStore directly (setEnabled/setRange/
# setUseBackgroundColor, restore-API rules, isAllDefault()) plus the event/
# command chain via a fresh Kernel, same shape as
# agent_shadow_store_test just above (FogStore mirrors ShadowStore/
# StyleStore's own contract shape). The deliberate "dirty yes, undo no"
# composition is proven at the integration level instead --
# tests/agent/undo_test.cpp's own
# FogChangeIsDirtyButNeverCreatesItsOwnUndoStep, same per-agent-file-vs-
# undo_test.cpp split agent_shadow_store_test already follows. No Qt, so
# plain POST_BUILD discovery is fine here.
add_executable(agent_fog_store_test
    agent/fog_store_test.cpp
)

target_link_libraries(agent_fog_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_fog_store_test)

# agent_style_store_test: Qt-free, links plnr_agent directly (ordo::core transitively
# via plnr_agent's PUBLIC deps). Exercises StyleStore directly (setFaceStyle/
# setEdgeFlag no-op rules, isAllDefault, Restore API) plus its two commands
# end to end through real events (StyleChanged firing/dedup). The deliberate
# "dirty yes, undo no" composition is proven separately, at the integration
# level, by tests/agent/undo_test.cpp's own
# StyleChangeIsDirtyButNeverCreatesItsOwnUndoStep -- same split every other
# per-agent test file here already follows toward undo_test.cpp. No Qt, so
# plain POST_BUILD discovery (the default) is fine here.
add_executable(agent_style_store_test
    agent/style_store_test.cpp
)

target_link_libraries(agent_style_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_style_store_test)

# plnr_material_batch_test: Exercises the presenter/widget's shared pure material-batching helper
# (src/app/viewport/material_batch.h/.cpp) directly: face-override-vs-
# instance-inheritance resolution (including nested instance chains), range
# bucketing/contiguity, opaque-vs-transparent partition (including the
# frontMaterialId==0 sentinel), and back-to-front centroid ordering. Same
# "app .cpp with no library target of its own, compile it straight into the
# test binary" precedent as plnr_camera_test above, but this one needs no Qt
# at all (material_batch.h only touches geo:: value types, deliberately no
# agent::MaterialRepository/MaterialAssignment -- see its own file comment) --
# links plnr_geo only, so plain POST_BUILD discovery is fine.
add_executable(plnr_material_batch_test
    viewport/material_batch_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/viewport/material_batch.cpp
)

target_include_directories(plnr_material_batch_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(plnr_material_batch_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(plnr_material_batch_test)

# plnr_edge_style_test: Exercises the pure, Qt/domain-free silhouette/profile
# edge classification + depth-banding helper (src/app/viewport/
# edge_style.h/.cpp) directly: classifyEdge's cube-corner cases (front-
# facing pair vs silhouette pair vs boundary vs wire vs degenerate-normal
# safety), depthBandIndex's banding math, and classifyAndBucketEdges' end-
# to-end bucketing. Same "app .cpp with no library target of its own,
# compile it straight into the test binary" precedent as
# plnr_material_batch_test above -- links plnr_geo only, so plain POST_BUILD
# discovery is fine.
add_executable(plnr_edge_style_test
    viewport/edge_style_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/viewport/edge_style.cpp
)

target_include_directories(plnr_edge_style_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(plnr_edge_style_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(plnr_edge_style_test)

# --- Solid Tools ---

# geo_solid_test: Qt-free,
# links plnr_geo directly. Exercises isSolid's three independent checks (wire
# half-edges, non-manifold/bowtie vertices, non-positive divergence-theorem
# volume) plus solidVolume and isSolidDefinition, all built from raw
# Model::addEdge/addFaceOnLoop box geometry -- no Qt, so plain POST_BUILD
# discovery (the default) is fine here.
add_executable(geo_solid_test
    geo/solid_test.cpp
)

target_link_libraries(geo_solid_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_solid_test)

# geo_csg_test: Qt-free, links
# plnr_geo directly. Exercises the pure CSG kernel core (csg::apply's BSP
# clipping pipeline + csg::meshVolume): analytic volumes for overlapping /
# disjoint / contained / through-hole / coplanar-shared-face box pairs across
# Union/Subtract/Intersect, operand toWorld transforms (translation plus an
# orientation-reversing mirror), per-face provenance + winding contract, and
# op-order symmetry. Volumes only -- face counts are deliberately NOT asserted,
# since this step emits an unmerged fragment soup (coplanar re-merge is step
# 2/2). No Qt, so plain POST_BUILD discovery (the default) is fine here.
add_executable(geo_csg_test
    geo/csg_test.cpp
)

target_link_libraries(geo_csg_test
    PRIVATE
        plnr_geo
        GTest::gtest_main
)

gtest_discover_tests(geo_csg_test)

# agent_solid_commands_test: Qt-free, links plnr_agent directly (ordo::core + plnr_geo transitively via
# plnr_agent's PUBLIC deps). Exercises SolidOpCommand/GeometryApi::
# applySolidOp end to end through real events -- Union/Subtract/Trim/
# Intersect/Split volumes+topology (boxes placed via GeometryApi::
# importMesh, explicit-winding, no auto-face-detection risk), arity/
# non-solid-operand validation, the Subtract/Trim/Split/Intersect overlap
# gate (disjoint operands), undo/redo (including Split's multi-Definition
# creation in one transaction), and best-effort front-material carry-over via
# MaterialRepository. No Qt, so plain POST_BUILD discovery (the default) is fine
# here.
add_executable(agent_solid_commands_test
    agent/solid_commands_test.cpp
)

target_link_libraries(agent_solid_commands_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(agent_solid_commands_test)

# infra_dispatcher_test / infra_settings_test: The infra
# sources have no library target of their own yet, so they compile straight into
# each test binary (same precedent as plnr_context_menu_test above). Qt6::Core
# only; each test owns a QCoreApplication. PRE_TEST discovery: the binaries need
# the Qt DLLs on PATH just to start.
add_executable(infra_dispatcher_test
    infra/dispatcher_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/infra/io_dispatcher.cpp
)

target_include_directories(infra_dispatcher_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(infra_dispatcher_test
    PRIVATE
        Qt6::Core
        GTest::gtest_main
)

gtest_discover_tests(infra_dispatcher_test DISCOVERY_MODE PRE_TEST)

add_executable(infra_settings_test
    infra/settings_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/infra/settings_store.cpp
    ${CMAKE_SOURCE_DIR}/src/app/infra/settings_registry.cpp
)

target_include_directories(infra_settings_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(infra_settings_test
    PRIVATE
        Qt6::Core
        GTest::gtest_main
)

gtest_discover_tests(infra_settings_test DISCOVERY_MODE PRE_TEST)
