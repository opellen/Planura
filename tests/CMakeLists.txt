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
        ordo_core
        GTest::gtest_main
)

gtest_discover_tests(ordo_version_test)

add_executable(ordo_dispatcher_test
    ordo/dispatcher_test.cpp
)

target_link_libraries(ordo_dispatcher_test
    PRIVATE
        ordo_core
        GTest::gtest_main
)

gtest_discover_tests(ordo_dispatcher_test)

add_executable(ordo_store_test
    ordo/store_test.cpp
)

target_link_libraries(ordo_store_test
    PRIVATE
        ordo_core
        GTest::gtest_main
)

gtest_discover_tests(ordo_store_test)

add_executable(ordo_command_test
    ordo/command_test.cpp
)

target_link_libraries(ordo_command_test
    PRIVATE
        ordo_core
        GTest::gtest_main
)

gtest_discover_tests(ordo_command_test)

add_executable(ordo_app_kernel_test
    ordo/app_kernel_test.cpp
)

target_link_libraries(ordo_app_kernel_test
    PRIVATE
        ordo_core
        GTest::gtest_main
)

gtest_discover_tests(ordo_app_kernel_test)

# Unlike the pure-core tests above, this one links ordo_qt (and thus Qt6::Core)
# to exercise the Presenter adapter. No QApplication needed -- plain QObject use.
add_executable(ordo_presenter_test
    ordo/presenter_test.cpp
)

target_link_libraries(ordo_presenter_test
    PRIVATE
        ordo_qt
        GTest::gtest_main
)

# PRE_TEST: the default POST_BUILD discovery mode runs this executable right
# after linking, at build time, to enumerate its tests -- but this exe needs
# Qt DLLs on PATH to even start, and the build step is not guaranteed to have
# that. PRE_TEST defers discovery (running the exe) to ctest invocation time.
gtest_discover_tests(ordo_presenter_test DISCOVERY_MODE PRE_TEST)

# plnr_camera_test links Qt6::Gui directly (only what Camera needs -- no
# ordo_qt/ordo_core) and compiles camera.cpp straight out of src/app/viewport,
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

# domain_geometry_api_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_geometry_api_test
    agent/geometry_api_test.cpp
)

target_link_libraries(domain_geometry_api_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_geometry_api_test)

# domain_selection_store_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_selection_store_test
    agent/selection_store_test.cpp
)

target_link_libraries(domain_selection_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_selection_store_test)

# domain_tag_store_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_tag_store_test
    agent/tag_store_test.cpp
)

target_link_libraries(domain_tag_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_tag_store_test)

# domain_group_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_group_test
    agent/group_test.cpp
)

target_link_libraries(domain_group_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_group_test)

# domain_transform_entities_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::transformEntities/applyArrayTimes/applyArrayDivide (task
# 1.2). No Qt, so plain POST_BUILD discovery (the default) is fine here.
add_executable(domain_transform_entities_test
    agent/transform_entities_test.cpp
)

target_link_libraries(domain_transform_entities_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_transform_entities_test)

# domain_followme_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::followMe (m8-edit-toolset.task 7.2). No Qt, so plain POST_BUILD discovery
# (the default) is fine here.
add_executable(domain_followme_test
    agent/followme_test.cpp
)

target_link_libraries(domain_followme_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_followme_test)

# domain_splitting_pass_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::runSplittingPass (m9-inference-construction.task 2.2) through addEdge/
# addRectangle/addPolyline plus GeometryApi::divideEdge, and confirms
# followMe deliberately does not run the pass. No Qt, so plain POST_BUILD
# discovery (the default) is fine here.
add_executable(domain_splitting_pass_test
    agent/splitting_pass_test.cpp
)

target_link_libraries(domain_splitting_pass_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_splitting_pass_test)

# domain_edit_context_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_edit_context_test
    agent/edit_context_test.cpp
)

target_link_libraries(domain_edit_context_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_edit_context_test)

# domain_text3d_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# GeometryApi::add3dText with synthetic outlines only -- Text3dDialog's Qt
# font polygonization is not under test here (that half stays Qt-free-suite-
# untestable by design; see tests/agent/text3d_test.cpp's own comment). No
# Qt, so plain POST_BUILD discovery (the default) is fine here.
add_executable(domain_text3d_test
    agent/text3d_test.cpp
)

target_link_libraries(domain_text3d_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_text3d_test)

# domain_vcb_parser_test: Qt-free -- vcb_parser.{h,cpp} (src/app/tools) is
# deliberately std-only (see its header comment) and has no library target
# of its own, so this compiles vcb_parser.cpp straight into the test binary,
# same "compile the .cpp directly" pattern as plnr_camera_test above but with
# zero dependencies (not even Qt) instead of Qt6::Gui. Plain POST_BUILD
# discovery is fine -- no Qt DLLs to find at start-up.
add_executable(domain_vcb_parser_test
    agent/vcb_parser_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/tools/vcb_parser.cpp
)

target_include_directories(domain_vcb_parser_test PRIVATE ${CMAKE_SOURCE_DIR}/src/app)

target_link_libraries(domain_vcb_parser_test
    PRIVATE
        GTest::gtest_main
)

gtest_discover_tests(domain_vcb_parser_test)

# domain_guide_store_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises GuideStore
# (m9-inference-construction.task 4.1) plus its AddGuideLine/AddGuidePoint/EraseGuide/DeleteAllGuides/
# SetGuideHidden commands end to end. No Qt, so plain POST_BUILD discovery
# (the default) is fine here.
add_executable(domain_guide_store_test
    agent/guide_store_test.cpp
)

target_link_libraries(domain_guide_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_guide_store_test)

# domain_axes_store_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises AxesStore
# (m9-inference-construction.task 6) plus its SetAxes/ResetAxes commands end to end, including the
# deterministic re-orthonormalization construction rule. No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_axes_store_test
    agent/axes_store_test.cpp
)

target_link_libraries(domain_axes_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_axes_store_test)

# domain_annotation_store_test: Qt-free, links plnr_agent directly (ordo_core
# + plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises
# AnnotationStore (m9-inference-construction.task 7) plus its Add/SetText/Remove/RemoveAll commands
# end to end, including GeometryChangedCommand's chained
# PruneSelectionCommand-replay + refreshAssociations reaction driven through
# a real GeometryApi (add/remove/move an edge's vertices). No Qt, so plain
# POST_BUILD discovery (the default) is fine here.
add_executable(domain_annotation_store_test
    agent/annotation_store_test.cpp
)

target_link_libraries(domain_annotation_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_annotation_store_test)

# domain_section_store_test: Qt-free, links plnr_agent directly (ordo_core +
# plnr_geo transitively via plnr_agent's PUBLIC deps). Exercises SectionStore
# (m9-inference-construction.task 8) plus its Add/Remove/SetActive/Reverse/SetHidden commands end to
# end, including the single-active-cut invariant. No Qt, so plain POST_BUILD
# discovery (the default) is fine here.
add_executable(domain_section_store_test
    agent/section_store_test.cpp
)

target_link_libraries(domain_section_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_section_store_test)

# plnr_context_menu_test: m9-inference-construction.task 3. buildContextMenu() (src/app/ui/
# context_menu.cpp) is a pure function over tools::ToolContext's read
# accessors, but tool.h's PointerEvent carries a QPointF -- same "app .cpp
# with no library target of its own yet, compile it straight into the test
# binary" precedent as plnr_camera_test above, just linking plnr_agent (Qt-
# free: ordo_core + plnr_geo + the src/app include root) for the agent/geo
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

# plnr_plr_writer_test: m10-undo-file-io.task 1 step 2. Links plnr_io (which
# pulls plnr_agent -- ordo_core + plnr_geo -- transitively, plus Qt6::Core for
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

# plnr_plr_reader_test: m10-undo-file-io.task 1 step 3. Same plnr_io/Qt-DLLs-
# on-PATH rationale as plnr_plr_writer_test above (PRE_TEST discovery).
# plnr_miniz joins (v1.M11, DESIGN.md decision 2) PRIVATE --
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

# plnr_plr_container_test: v1.M11 (DESIGN.md decision 2).
# Exercises io::assembleContainer/io::openContainer directly (raw-vs-zip
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

# domain_document_test: m10-undo-file-io.task 2. Exercises DocumentStore/
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
add_executable(domain_document_test
    agent/document_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
)

target_link_libraries(domain_document_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(domain_document_test DISCOVERY_MODE PRE_TEST)

# domain_undo_test: m10-undo-file-io.task 3 step 2/3. Exercises
# TransactionManager/UndoStore/UndoCommand/RedoCommand end to end through
# real events (draw/tag/guide/axes/section/dimension/hide/group/explode/
# move-instance gestures, undo/redo, abort() at the unit level). Unlike
# domain_document_test, undo_commands.cpp/transaction_manager.cpp are NOT
# compiled directly into this binary -- the task-3 rework made them Qt-free,
# so they now live in plnr_agent proper (agent/CMakeLists.txt) and arrive
# here transitively via plnr_io -> plnr_agent. This binary still links plnr_io
# (rather than plnr_agent directly) and still compiles
# document_commands.cpp directly in, for two things ONLY plnr_io/Qt provide:
# io::writeDocument byte-level comparison (undo/redo round-trip bytes) and
# the New/Open-clears-stacks test case. Same PRE_TEST discovery rationale as
# domain_document_test: Qt DLLs need to be on PATH just to start.
add_executable(domain_undo_test
    agent/undo_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
)

target_link_libraries(domain_undo_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(domain_undo_test DISCOVERY_MODE PRE_TEST)

# plnr_obj_test: m10-undo-file-io.task 4 (DESIGN.md decision 6). Writer
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

# domain_material_test: v1.M11, extended in task 2 step 1/3
# (DESIGN.md decision 2) with AssetRepository + MaterialSetTextureCommand
# coverage. Exercises MaterialRepository directly (create/edit/paint/setActive/
# setTexture/clearTexture + restore-API rules), then end to end through real
# events (undo/redo of create/edit/paint/setTexture via UndoCaptureCommand,
# dirty tracking via MarkDirtyCommand<MaterialsChanged>, aux-diff capture via
# TransactionManager, SetActiveMaterialRequested's deliberate non-undo/
# non-dirty exclusion, and .plr round-trip via New/Open/SaveDocumentCommand
# plus direct io::writeDocument/readDocument calls for the schema-level
# contract tests). Same "needs plnr_io + document_commands.cpp compiled
# straight in" precedent as domain_document_test/domain_undo_test above
# (io::writeDocument/readDocument + Qt6::Core via plnr_io means this binary
# needs Qt DLLs on PATH just to start -- PRE_TEST discovery).
# material_texture_commands.cpp joins for the same reason document_commands.cpp
# is here -- MaterialSetTextureCommand needs Qt (QFile) but no library target
# of its own (agent/command/material_texture_commands.h's own placement comment).
add_executable(domain_material_test
    agent/material_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/document_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/app/agent/command/material_texture_commands.cpp
)

target_link_libraries(domain_material_test
    PRIVATE
        plnr_io
        GTest::gtest_main
)

gtest_discover_tests(domain_material_test DISCOVERY_MODE PRE_TEST)

# domain_asset_repository_test: v1.M11 (DESIGN.md decision 2).
# Qt-free, links plnr_agent directly (ordo_core transitively) -- content-hash
# stability/dedupe/restore-verification-mismatch rules, no kernel
# registration needed (AssetRepository never dispatches, see its own class
# comment). No Qt, so plain POST_BUILD discovery is fine here.
add_executable(domain_asset_repository_test
    agent/asset_repository_test.cpp
)

target_link_libraries(domain_asset_repository_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_asset_repository_test)

# domain_sun_position_test: v1.M11 (DESIGN.md decision 7,
# PLAN.md Task 4). Qt-free AND kernel-free (sun_position.h/.cpp is pure
# value-in/value-out math, no Agent/Command involved) -- links plnr_agent
# directly (for geo::Vec3 transitively via plnr_geo), same POST_BUILD
# discovery as domain_asset_repository_test above.
add_executable(domain_sun_position_test
    agent/sun_position_test.cpp
)

target_link_libraries(domain_sun_position_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_sun_position_test)

# domain_shadow_store_test: v1.M11 (DESIGN.md decision 7,
# PLAN.md Task 4). Qt-free, links plnr_agent directly (ordo_core
# transitively) -- exercises ShadowStore directly (setEnabled/setPosition/
# setDateTime, restore-API rules, isAllDefault()) plus the event/command
# chain via a fresh AppKernel, same shape as domain_style_store_test just
# above (ShadowStore mirrors StyleStore's own contract field-for-field). The
# deliberate "dirty yes, undo no" composition is proven at the integration
# level instead -- tests/agent/undo_test.cpp's own
# ShadowChangeIsDirtyButNeverCreatesItsOwnUndoStep, same per-agent-file-vs-
# undo_test.cpp split domain_style_store_test already follows. No Qt, so
# plain POST_BUILD discovery is fine here.
add_executable(domain_shadow_store_test
    agent/shadow_store_test.cpp
)

target_link_libraries(domain_shadow_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_shadow_store_test)

# domain_fog_store_test: v1.M11 (PLAN.md Task 4 Step 2/3,
# DESIGN.md decision 6). Qt-free, links plnr_agent directly (ordo_core
# transitively) -- exercises FogStore directly (setEnabled/setRange/
# setUseBackgroundColor, restore-API rules, isAllDefault()) plus the event/
# command chain via a fresh AppKernel, same shape as
# domain_shadow_store_test just above (FogStore mirrors ShadowStore/
# StyleStore's own contract shape). The deliberate "dirty yes, undo no"
# composition is proven at the integration level instead --
# tests/agent/undo_test.cpp's own
# FogChangeIsDirtyButNeverCreatesItsOwnUndoStep, same per-agent-file-vs-
# undo_test.cpp split domain_shadow_store_test already follows. No Qt, so
# plain POST_BUILD discovery is fine here.
add_executable(domain_fog_store_test
    agent/fog_store_test.cpp
)

target_link_libraries(domain_fog_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_fog_store_test)

# domain_style_store_test: v1.M11 (DESIGN.md decision 6,
# PLAN.md Task 3). Qt-free, links plnr_agent directly (ordo_core transitively
# via plnr_agent's PUBLIC deps). Exercises StyleStore directly (setFaceStyle/
# setEdgeFlag no-op rules, isAllDefault, Restore API) plus its two commands
# end to end through real events (StyleChanged firing/dedup). The deliberate
# "dirty yes, undo no" composition is proven separately, at the integration
# level, by tests/agent/undo_test.cpp's own
# StyleChangeIsDirtyButNeverCreatesItsOwnUndoStep -- same split every other
# per-agent test file here already follows toward undo_test.cpp. No Qt, so
# plain POST_BUILD discovery (the default) is fine here.
add_executable(domain_style_store_test
    agent/style_store_test.cpp
)

target_link_libraries(domain_style_store_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_style_store_test)

# plnr_material_batch_test: v1.M11 (DESIGN.md decision 2).
# Exercises the presenter/widget's shared pure material-batching helper
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

# plnr_edge_style_test: v1.M11 (PLAN.md Task 3 pinned
# approximations). Exercises the pure, Qt/domain-free silhouette/profile
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

# --- v1.M12 ---

# geo_solid_test: v1.M12 (DESIGN.md decision 1). Qt-free,
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

# geo_csg_test: v1.M12 (DESIGN.md decision 2). Qt-free, links
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

# domain_solid_commands_test: v1.M12 (DESIGN.md decision 3).
# Qt-free, links plnr_agent directly (ordo_core + plnr_geo transitively via
# plnr_agent's PUBLIC deps). Exercises SolidOpCommand/GeometryApi::
# applySolidOp end to end through real events -- Union/Subtract/Trim/
# Intersect/Split volumes+topology (boxes placed via GeometryApi::
# importMesh, explicit-winding, no auto-face-detection risk), arity/
# non-solid-operand validation, the Subtract/Trim/Split/Intersect overlap
# gate (disjoint operands), undo/redo (including Split's multi-Definition
# creation in one transaction), and best-effort front-material carry-over via
# MaterialRepository. No Qt, so plain POST_BUILD discovery (the default) is fine
# here.
add_executable(domain_solid_commands_test
    agent/solid_commands_test.cpp
)

target_link_libraries(domain_solid_commands_test
    PRIVATE
        plnr_agent
        GTest::gtest_main
)

gtest_discover_tests(domain_solid_commands_test)
