# Application executable: the Planura GUI entry point.
# Network is for devbridge/debug_bridge.h's QTcpServer -- the in-app TCP JSON
# debug bridge used for agent-driven interaction testing.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets Network)

set(CMAKE_AUTOMOC ON)

# plnr_agent (Agents + Commands) must exist before the executable below can
# link it.
add_subdirectory(agent)

# plnr_io (.plr writer/reader, m10-undo-file-io.task 1) must exist before
# tests/io/*_test.cpp can link it. Linked into the planura executable
# below as of task 2's document commands (agent/command/document_commands.cpp).
add_subdirectory(io)

# main_window.h, ui/tray.h, ui/status_bar_presenter.h, ui/viewport_presenter.h,
# ui/title_bar_presenter.h, ui/undo_menu_presenter.h, and
# ui/style_menu_presenter.h each sit next to a .cpp of the same name in the
# same directory, so CMake AUTOMOC's basename pairing finds and moc's them
# automatically -- no need to list the headers explicitly. (Contrast
# src/framework/ordo/qt/CMakeLists.txt, where presenter.h lives in a public
# include/ dir separate from its .cpp and must be listed explicitly for
# AUTOMOC to find it.)

# WIN32 suppresses the console window for this GUI application.
# NOTE: agent/command/undo_commands.cpp is NOT listed here -- unlike
# document_commands.cpp (still genuinely Qt/plnr_io-dependent), the
# m10-undo-file-io.task 3 step 2/3 rework made undo_commands.cpp Qt-free, so
# it moved into plnr_agent (agent/CMakeLists.txt) instead and is already
# linked in via plnr_agent below.
add_executable(planura WIN32
    main.cpp
    main_window.cpp
    devbridge/debug_bridge.cpp
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
    ui/context_menu.cpp
    ui/tray.cpp
    ui/status_bar_presenter.cpp
    ui/viewport_presenter.cpp
    ui/entity_info_presenter.cpp
    ui/materials_presenter.cpp
    ui/shadows_menu_presenter.cpp
    ui/style_menu_presenter.cpp
    ui/tags_presenter.cpp
    ui/text3d_dialog.cpp
    ui/title_bar_presenter.cpp
    ui/undo_menu_presenter.cpp
    viewport/camera.cpp
    viewport/edge_style.cpp
    viewport/material_batch.cpp
    viewport/viewport_widget.cpp
)

target_link_libraries(planura
    PRIVATE
        ordo_qt
        plnr_agent
        plnr_io
        Qt6::OpenGLWidgets
        Qt6::Network
)
