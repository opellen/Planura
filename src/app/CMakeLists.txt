# Application executable: the Planura GUI entry point.
# Network is for devbridge/debug_bridge.h's QTcpServer -- the in-app TCP JSON
# debug bridge used for agent-driven interaction testing.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets Network Svg)

set(CMAKE_AUTOMOC ON)

# plnr_agent (Agents + Commands) must exist before the executable below can
# link it.
add_subdirectory(agent)

# plnr_io (.plr writer/reader) must exist before
# tests/io/*_test.cpp can link it. Linked into the planura executable
# below for the document commands (agent/command/document_commands.cpp).
add_subdirectory(io)

# main_window.h, ui/activity_rail.h, ui/side_bar.h, ui/status_bar_presenter.h, ui/viewport_presenter.h,
# ui/title_bar_presenter.h, ui/undo_menu_presenter.h, and
# ui/style_menu_presenter.h each sit next to a .cpp of the same name in the
# same directory, so CMake AUTOMOC's basename pairing finds and moc's them
# automatically -- no need to list the headers explicitly. (Contrast
# ordo's own qt/CMakeLists.txt, where presenter.h lives in a public include/
# dir separate from its .cpp and must be listed explicitly for AUTOMOC to
# find it.)

# WIN32 suppresses the console window for this GUI application.
# NOTE: agent/command/undo_commands.cpp is NOT listed here -- unlike
# document_commands.cpp (still genuinely Qt/plnr_io-dependent), the
# undo_commands.cpp was reworked to be Qt-free, so
# it moved into plnr_agent (agent/CMakeLists.txt) instead and is already
# linked in via plnr_agent below.
# Exe-embedded icon. MinGW windres compiles
# .rc natively as a CMake source; guarded because app.rc is a Windows-only
# resource script (IDI_ICON1 -> resources/app.ico).
set(PLNR_APP_WIN32_SOURCES "")
if(WIN32)
    list(APPEND PLNR_APP_WIN32_SOURCES app.rc)
endif()

add_executable(planura WIN32
    main.cpp
    document_session.cpp
    main_window.cpp
    ui/icons.cpp
    ui/io_presenter.cpp
    ${PLNR_APP_WIN32_SOURCES}
    devbridge/debug_bridge.cpp
    harness/probe_stage.cpp
    harness/probe_support.cpp
    harness/probe_scenarios_shell.cpp
    harness/probe_scenarios_tools.cpp
    infra/io_dispatcher.cpp
    infra/settings_registry.cpp
    infra/settings_store.cpp
    agent/command/document_commands.cpp
    agent/command/obj_commands.cpp
    agent/command/material_texture_commands.cpp
    tools/tool_controller.cpp
    tools/arc2point_tool.cpp
    tools/arc3point_tool.cpp
    tools/arc_center_tool.cpp
    tools/axes_tool.cpp
    tools/circle_tool.cpp
    tools/dimension_tool.cpp
    tools/eraser_tool.cpp
    tools/flip_tool.cpp
    tools/followme_tool.cpp
    tools/freehand_tool.cpp
    tools/line_tool.cpp
    tools/move_tool.cpp
    tools/offset_tool.cpp
    tools/paint_bucket_tool.cpp
    tools/pie_tool.cpp
    tools/polygon_tool.cpp
    tools/protractor_tool.cpp
    tools/pushpull_tool.cpp
    tools/rectangle_tool.cpp
    tools/rotate_tool.cpp
    tools/rotated_rectangle_tool.cpp
    tools/scale_tool.cpp
    tools/section_plane_tool.cpp
    tools/select_tool.cpp
    tools/solid_tool.cpp
    tools/tape_measure_tool.cpp
    tools/text_tool.cpp
    tools/vcb_parser.cpp
    ui/context_bar.cpp
    ui/context_bar_presenter.cpp
    ui/context_menu.cpp
    ui/editor_group.cpp
    ui/activity_rail.cpp
    ui/progress_overlay.cpp
    ui/section_header.cpp
    ui/side_bar.cpp
    ui/tab_badge_adapter.cpp
    ui/status_bar_presenter.cpp
    ui/viewport_presenter.cpp
    ui/entity_info_presenter.cpp
    ui/materials_presenter.cpp
    ui/shadows_menu_presenter.cpp
    ui/style_menu_presenter.cpp
    ui/tags_presenter.cpp
    ui/text3d_dialog.cpp
    ui/theme.cpp
    ui/title_bar_presenter.cpp
    ui/tool_flyout.cpp
    ui/undo_menu_presenter.cpp
    viewport/camera.cpp
    viewport/edge_style.cpp
    viewport/material_batch.cpp
    viewport/viewport_widget.cpp
)

target_link_libraries(planura
    PRIVATE
        ordo::qt
        plnr_agent
        plnr_io
        Qt6::OpenGLWidgets
        Qt6::Network
        Qt6::Svg
)

# Icons load at runtime as :/icons/<slug>.svg via
# the Qt SVG icon engine (Qt6::Svg linked above). Explicit file list, no
# file(GLOB) -- see resources/icons/README.md for provenance/update rule.
qt_add_resources(planura "app_icons"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES
        resources/icons/mode_edit.svg
        resources/icons/mode_present.svg
        resources/icons/mode_render.svg
        resources/icons/planura_doc.svg
        resources/icons/planura_logo.svg
        resources/icons/planura_logo_small.svg
        resources/icons/planura_logo_text.svg
        resources/icons/tb_3dtext.svg
        resources/icons/tb_arc.svg
        resources/icons/tb_arc3point.svg
        resources/icons/tb_arc3pointfit.svg
        resources/icons/tb_arc3pointpie.svg
        resources/icons/tb_axes.svg
        resources/icons/tb_backedges.svg
        resources/icons/tb_circle.svg
        resources/icons/tb_dimension.svg
        resources/icons/tb_erase.svg
        resources/icons/tb_flip.svg
        resources/icons/tb_followme.svg
        resources/icons/tb_freehand.svg
        resources/icons/tb_hiddenline.svg
        resources/icons/tb_label.svg
        resources/icons/tb_line.svg
        resources/icons/tb_measure.svg
        resources/icons/tb_monochrome.svg
        resources/icons/tb_move.svg
        resources/icons/tb_new.svg
        resources/icons/tb_offset.svg
        resources/icons/tb_open.svg
        resources/icons/tb_orbit.svg
        resources/icons/tb_paint.svg
        resources/icons/tb_pan.svg
        resources/icons/tb_polygon.svg
        resources/icons/tb_protractor.svg
        resources/icons/tb_pushpull.svg
        resources/icons/tb_rectangle.svg
        resources/icons/tb_rectangle3point.svg
        resources/icons/tb_redo.svg
        resources/icons/tb_rotate.svg
        resources/icons/tb_save.svg
        resources/icons/tb_scale.svg
        resources/icons/tb_section.svg
        resources/icons/tb_select.svg
        resources/icons/tb_shaded.svg
        resources/icons/tb_shadowtoggle.svg
        resources/icons/tb_textures.svg
        resources/icons/tb_undo.svg
        resources/icons/tb_wireframe.svg
        resources/icons/tb_xray.svg
        resources/icons/tb_zoom.svg
        resources/icons/tb_zoomextents.svg
)
