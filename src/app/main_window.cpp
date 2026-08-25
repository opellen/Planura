#include "main_window.h"

#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <geo/vec3.h>

#include "agent/document_store.h"
#include "agent/events.h"
#include "agent/selection_store.h"
#include "tools/tool_controller.h"
#include "ui/entity_info_presenter.h"
#include "ui/materials_presenter.h"
#include "ui/shadows_menu_presenter.h"
#include "ui/status_bar_presenter.h"
#include "ui/style_menu_presenter.h"
#include "ui/tags_presenter.h"
#include "ui/text3d_dialog.h"
#include "ui/title_bar_presenter.h"
#include "ui/tray.h"
#include "ui/undo_menu_presenter.h"
#include "ui/viewport_presenter.h"
#include "viewport/viewport_widget.h"

namespace plnr {

namespace {

// Adds a disabled placeholder QAction -- menu scaffolding the real commands
// will replace later.
void addPlaceholder(QMenu* menu, const QString& text) {
    menu->addAction(text)->setEnabled(false);
}

}  // namespace

MainWindow::MainWindow(ordo::core::AppKernel& kernel, QWidget* parent) : QMainWindow(parent), kernel_(kernel) {
    // Title text is owned by TitleBarPresenter (registered below); no placeholder needed.
    resize(1440, 900);

    buildMenuBar();
    buildTopToolbar();
    buildToolPalette();
    buildCentralViewport();
    buildDefaultTray();
    buildStatusBar();

    // Type-anywhere: digit/'.'/'-' keypresses redirect to the VCB field (see eventFilter()).
    viewport_->installEventFilter(this);

    presenterHost_.add<ui::StatusBarPresenter>(kernel_, hintLabel_, vcbLabel_, vcbEdit_);
    // ViewportPresenter owns checked-state for the projection actions and
    // enabled-state for previous/next.
    presenterHost_.add<ui::ViewportPresenter>(kernel_, viewport_, perspectiveAction_, parallelProjectionAction_,
                                               twoPointPerspectiveAction_, previousAction_, nextAction_);
    // Captured for toolController(); backs DebugBridge's `modal` command.
    toolController_ = presenterHost_.add<tools::ToolController>(kernel_, viewport_);
    presenterHost_.add<ui::EntityInfoPresenter>(kernel_, tray_->entityInfoLabel());
    presenterHost_.add<ui::TagsPresenter>(kernel_, tray_->tagsList(), tray_->addTagButton(), tray_->entityTagCombo());
    presenterHost_.add<ui::MaterialsPresenter>(kernel_, tray_->materialsList(), tray_->addMaterialButton(),
                                                tray_->materialNameEdit(), tray_->materialColorButton(),
                                                tray_->materialOpacitySpin(), tray_->materialTextureCheck(),
                                                tray_->materialTileWSpin(), tray_->materialTileHSpin());
    presenterHost_.add<ui::TitleBarPresenter>(kernel_, this);
    // Owns Undo/Redo QActions' enabled state.
    presenterHost_.add<ui::UndoMenuPresenter>(kernel_, undoAction_, redoAction_);
    // Owns Face Style/Edge Style checked state.
    presenterHost_.add<ui::StyleMenuPresenter>(kernel_, faceStyleActions_, edgeFlagActions_, ambientOcclusionAction_);
    // Owns Shadows/Use Sun for Shading/Fog checked state.
    presenterHost_.add<ui::ShadowsMenuPresenter>(kernel_, shadowsAction_, useSunForShadingAction_, fogAction_);
}

void MainWindow::buildMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    // the reference modeler's own default shortcuts; Save As uses the Windows convention
    // Ctrl+Shift+S (no the reference modeler precedent to crib).
    QAction* newAction = fileMenu->addAction(QStringLiteral("New"));
    newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewDocument);

    QAction* openAction = fileMenu->addAction(QStringLiteral("Open..."));
    openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenDocument);

    QAction* importAction = fileMenu->addAction(QStringLiteral("Import..."));
    connect(importAction, &QAction::triggered, this, &MainWindow::onImportObj);

    QAction* saveAction = fileMenu->addAction(QStringLiteral("Save"));
    saveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveDocument);

    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("Save As..."));
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAction, &QAction::triggered, this, [this]() { onSaveDocumentAs(); });

    // Mirrors File > Export > 3D Model...; OBJ is the only export format (v1).
    QMenu* exportMenu = fileMenu->addMenu(QStringLiteral("Export"));
    QAction* exportObjAction = exportMenu->addAction(QStringLiteral("OBJ..."));
    connect(exportObjAction, &QAction::triggered, this, &MainWindow::onExportObj);

    fileMenu->addSeparator();
    // Routes through QWidget::close() -> closeEvent() -> confirmDiscardChanges(),
    // same gate as the window's own X button/OS close.
    QAction* exitAction = fileMenu->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));

    // the reference modeler puts Undo/Redo first in the Edit menu; both start disabled.
    // Enabled state afterward is owned by UndoMenuPresenter (UndoStateChanged
    // subscription). Label stays plain "Undo"/"Redo" (no per-command name yet).
    undoAction_ = editMenu->addAction(QStringLiteral("Undo"));
    // the reference modeler's Windows shortcut Ctrl+Z, plus its real default Alt+Backspace.
    undoAction_->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Z")), QKeySequence(QStringLiteral("Alt+Backspace"))});
    undoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, [this]() { kernel_.send(events::UndoRequested{}); });

    redoAction_ = editMenu->addAction(QStringLiteral("Redo"));
    redoAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
    redoAction_->setEnabled(false);
    connect(redoAction_, &QAction::triggered, this, [this]() { kernel_.send(events::RedoRequested{}); });

    editMenu->addSeparator();

    addPlaceholder(editMenu, QStringLiteral("Cut"));
    addPlaceholder(editMenu, QStringLiteral("Copy"));
    addPlaceholder(editMenu, QStringLiteral("Paste"));

    // Sends the empty DeleteSelectionRequested{} fact; DeleteSelectionCommand
    // reads the live selection itself. Safe against the VCB type-anywhere
    // redirect (Delete never produces text()).
    QAction* deleteAction = editMenu->addAction(QStringLiteral("Delete"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [this]() { kernel_.send(events::DeleteSelectionRequested{}); });

    editMenu->addSeparator();

    // Select All sends SelectAllRequested; Select None is a
    // default-constructed SelectRequested{} (SelectCommand's deselect-all contract).
    QAction* selectAllAction = editMenu->addAction(QStringLiteral("Select All"));
    selectAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));
    connect(selectAllAction, &QAction::triggered, this, [this]() { kernel_.send(events::SelectAllRequested{}); });

    QAction* selectNoneAction = editMenu->addAction(QStringLiteral("Select None"));
    selectNoneAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(selectNoneAction, &QAction::triggered, this, [this]() { kernel_.send(events::SelectRequested{}); });

    editMenu->addSeparator();

    // Reads the live selection from SelectionStore; empty selection is a no-op.
    QAction* hideAction = editMenu->addAction(QStringLiteral("Hide"));
    connect(hideAction, &QAction::triggered, this, [this]() {
        auto selection = kernel_.agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
        if (!selection || selection->items().empty()) return;
        kernel_.send(events::SetHiddenRequested{selection->items(), true});
    });

    QAction* unhideAllAction = editMenu->addAction(QStringLiteral("Unhide All"));
    connect(unhideAllAction, &QAction::triggered, this, [this]() { kernel_.send(events::UnhideAllRequested{}); });

    editMenu->addSeparator();

    // GroupCreateCommand reads SelectionStore itself. Explode always sends
    // instanceId 0 (no instance-selection UI yet; a documented no-op inside
    // GeometryApi::explode until one exists).
    QAction* makeGroupAction = editMenu->addAction(QStringLiteral("Make Group"));
    connect(makeGroupAction, &QAction::triggered, this,
            [this]() { kernel_.send(events::GroupCreateRequested{false, ""}); });

    QAction* makeComponentAction = editMenu->addAction(QStringLiteral("Make Component"));
    connect(makeComponentAction, &QAction::triggered, this,
            [this]() { kernel_.send(events::GroupCreateRequested{true, ""}); });

    QAction* explodeAction = editMenu->addAction(QStringLiteral("Explode"));
    connect(explodeAction, &QAction::triggered, this, [this]() { kernel_.send(events::ExplodeRequested{0}); });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    addPlaceholder(viewMenu, QStringLiteral("Toolbars"));
    addPlaceholder(viewMenu, QStringLiteral("Scene Tabs"));
    viewMenu->addSeparator();

    // Face Style submenu (X-Ray included, per the reference modeler mirror): exclusive
    // QActionGroup, one entry per FaceStyle, in that enum's own order.
    // ShadedWithTextures starts checked, matching StyleStore's default.
    QMenu* faceStyleMenu = viewMenu->addMenu(QStringLiteral("Face Style"));
    auto* faceStyleGroup = new QActionGroup(this);
    faceStyleGroup->setExclusive(true);

    struct FaceStyleEntry {
        QString label;
        events::FaceStyle style;
    };
    const FaceStyleEntry faceStyleEntries[] = {
        {QStringLiteral("Wireframe"), events::FaceStyle::Wireframe},
        {QStringLiteral("Hidden Line"), events::FaceStyle::HiddenLine},
        {QStringLiteral("Shaded"), events::FaceStyle::Shaded},
        {QStringLiteral("Shaded with Textures"), events::FaceStyle::ShadedWithTextures},
        {QStringLiteral("Monochrome"), events::FaceStyle::Monochrome},
        {QStringLiteral("X-Ray"), events::FaceStyle::XRay},
    };
    for (const FaceStyleEntry& entry : faceStyleEntries) {
        QAction* action = faceStyleMenu->addAction(entry.label);
        action->setCheckable(true);
        faceStyleGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, style = entry.style]() { kernel_.send(events::SetFaceStyleRequested{style}); });
        if (entry.style == events::FaceStyle::ShadedWithTextures) {
            action->setChecked(true);
        }
        faceStyleActions_[entry.style] = action;
    }

    // Edge Style submenu: this app's own placement choice (no confirmed
    // View-menu location in the mirror). Three independent checkable
    // toggles; setChecked(false) is a pre-presenter placeholder, overwritten
    // by StyleMenuPresenter's refresh(). Toggling persists/dirties but
    // doesn't yet change rendering (events::EdgeFlag).
    QMenu* edgeStyleMenu = viewMenu->addMenu(QStringLiteral("Edge Style"));

    struct EdgeFlagEntry {
        QString label;
        events::EdgeFlag flag;
    };
    const EdgeFlagEntry edgeFlagEntries[] = {
        {QStringLiteral("Profiles"), events::EdgeFlag::Profiles},
        {QStringLiteral("Depth Cue"), events::EdgeFlag::DepthCue},
        {QStringLiteral("Back Edges"), events::EdgeFlag::BackEdges},
    };
    for (const EdgeFlagEntry& entry : edgeFlagEntries) {
        QAction* action = edgeStyleMenu->addAction(entry.label);
        action->setCheckable(true);
        action->setChecked(false);
        connect(action, &QAction::triggered, this, [this, flag = entry.flag](bool checked) {
            kernel_.send(events::SetEdgeStyleFlagRequested{flag, checked});
        });
        edgeFlagActions_[entry.flag] = action;
    }

    // DEVIATION from the mirror (the reference modeler puts AO in engine display
    // settings, not the View menu). Drives StyleStore::ambientOcclusion();
    // checked-state owned by StyleMenuPresenter despite living next to
    // Shadows/Fog. Default unchecked, matching StyleStore's default.
    QAction* ambientOcclusionQAction = viewMenu->addAction(QStringLiteral("Ambient Occlusion"));
    ambientOcclusionQAction->setCheckable(true);
    ambientOcclusionQAction->setChecked(false);
    connect(ambientOcclusionQAction, &QAction::triggered, this,
            [this](bool checked) { kernel_.send(events::SetAmbientOcclusionRequested{checked}); });
    ambientOcclusionAction_ = ambientOcclusionQAction;

    // Mirrors the reference modeler's View > Shadows item; drives showShadows()
    // (ground-plane shadow casting). No Shadows tray panel yet, so this and
    // the `shadow` bridge command are the only ways to reach it. Default unchecked.
    viewMenu->addSeparator();
    QAction* shadowsQAction = viewMenu->addAction(QStringLiteral("Shadows"));
    shadowsQAction->setCheckable(true);
    shadowsQAction->setChecked(false);
    connect(shadowsQAction, &QAction::triggered, this,
            [this](bool checked) { kernel_.send(events::SetShowShadowsRequested{checked}); });
    shadowsAction_ = shadowsQAction;

    // the reference modeler's own Shadows-panel checkbox name; drives N.L face shading.
    // DEVIATION: mirror has no View-menu equivalent (no tray panel yet).
    // setChecked(false) is a pre-presenter placeholder, overwritten by
    // ShadowsMenuPresenter's refresh.
    QAction* useSunForShadingQAction = viewMenu->addAction(QStringLiteral("Use Sun for Shading"));
    useSunForShadingQAction->setCheckable(true);
    useSunForShadingQAction->setChecked(false);
    connect(useSunForShadingQAction, &QAction::triggered, this,
            [this](bool checked) { kernel_.send(events::SetUseSunForShadingRequested{checked}); });
    useSunForShadingAction_ = useSunForShadingQAction;

    // DEVIATION from the mirror (the reference modeler uses its own Fog tray dialog).
    // Default unchecked, matching FogStore's default.
    QAction* fogQAction = viewMenu->addAction(QStringLiteral("Fog"));
    fogQAction->setCheckable(true);
    fogQAction->setChecked(false);
    connect(fogQAction, &QAction::triggered, this,
            [this](bool checked) { kernel_.send(events::SetFogEnabledRequested{checked}); });
    fogAction_ = fogQAction;

    QMenu* cameraMenu = menuBar()->addMenu(QStringLiteral("&Camera"));

    // Order: Previous/Next, Standard Views, Perspective/Parallel group
    // (Parallel listed above Perspective, the reference modeler's order), Orbit/Pan/Zoom.

    // Session-only camera view history (CameraStore's two-stack). Not
    // checkable; enabled-state owned by ViewportPresenter, refreshed on
    // CameraChanged. Both start enabled; pullCamera() overwrites with the real value.
    previousAction_ = cameraMenu->addAction(QStringLiteral("Previous"));
    connect(previousAction_, &QAction::triggered, this,
            [this]() { kernel_.send(events::CameraPreviousRequested{}); });

    nextAction_ = cameraMenu->addAction(QStringLiteral("Next"));
    connect(nextAction_, &QAction::triggered, this,
            [this]() { kernel_.send(events::CameraNextRequested{}); });

    // Seven fixed orientation presets, in StandardView's own enumerator order.
    QMenu* standardViewsMenu = cameraMenu->addMenu(QStringLiteral("Standard Views"));
    struct StandardViewEntry {
        QString label;
        events::StandardView view;
    };
    const StandardViewEntry standardViewEntries[] = {
        {QStringLiteral("Top"), events::StandardView::Top},
        {QStringLiteral("Bottom"), events::StandardView::Bottom},
        {QStringLiteral("Front"), events::StandardView::Front},
        {QStringLiteral("Back"), events::StandardView::Back},
        {QStringLiteral("Left"), events::StandardView::Left},
        {QStringLiteral("Right"), events::StandardView::Right},
        {QStringLiteral("Iso"), events::StandardView::Iso},
    };
    for (const StandardViewEntry& entry : standardViewEntries) {
        QAction* action = standardViewsMenu->addAction(entry.label);
        connect(action, &QAction::triggered, this,
                [this, view = entry.view]() { kernel_.send(events::SetStandardViewRequested{view}); });
    }

    cameraMenu->addSeparator();

    // Exclusive QActionGroup, same shape as faceStyleGroup. Perspective
    // starts checked, matching Camera's default; ViewportPresenter::pullCamera()
    // overwrites it immediately. Construction order below (Parallel,
    // Perspective, Two-Point) matches the reference modeler's own group order.
    auto* projectionGroup = new QActionGroup(this);
    projectionGroup->setExclusive(true);

    parallelProjectionAction_ = cameraMenu->addAction(QStringLiteral("Parallel Projection"));
    parallelProjectionAction_->setCheckable(true);
    projectionGroup->addAction(parallelProjectionAction_);
    connect(parallelProjectionAction_, &QAction::triggered, this,
            [this]() { kernel_.send(events::SetProjectionRequested{events::Projection::Parallel}); });

    perspectiveAction_ = cameraMenu->addAction(QStringLiteral("Perspective"));
    perspectiveAction_->setCheckable(true);
    perspectiveAction_->setChecked(true);
    projectionGroup->addAction(perspectiveAction_);
    connect(perspectiveAction_, &QAction::triggered, this,
            [this]() { kernel_.send(events::SetProjectionRequested{events::Projection::Perspective}); });

    // Removes the third (vertical) vanishing point so world-vertical edges
    // render vertical. Orbiting or jumping to a Standard View exits back to
    // ordinary Perspective (CameraSyncCommand/CameraStore::setStandardView);
    // zoom/pan keep the mode.
    twoPointPerspectiveAction_ = cameraMenu->addAction(QStringLiteral("Two-Point Perspective"));
    twoPointPerspectiveAction_->setCheckable(true);
    projectionGroup->addAction(twoPointPerspectiveAction_);
    connect(twoPointPerspectiveAction_, &QAction::triggered, this,
            [this]() { kernel_.send(events::SetProjectionRequested{events::Projection::TwoPoint}); });

    cameraMenu->addSeparator();
    addPlaceholder(cameraMenu, QStringLiteral("Orbit"));
    addPlaceholder(cameraMenu, QStringLiteral("Pan"));
    addPlaceholder(cameraMenu, QStringLiteral("Zoom"));

    QMenu* drawMenu = menuBar()->addMenu(QStringLiteral("&Draw"));
    addPlaceholder(drawMenu, QStringLiteral("Line"));
    addPlaceholder(drawMenu, QStringLiteral("Rectangle"));
    addPlaceholder(drawMenu, QStringLiteral("Circle"));

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    addPlaceholder(toolsMenu, QStringLiteral("Select"));
    addPlaceholder(toolsMenu, QStringLiteral("Move"));

    // -- Solid Tools -- Real tool activations (ToolChanged), unlike the
    // placeholders above; no toolbar/checkable presence. Registered in
    // toolActions_ so DebugBridge's `tool` command reaches all six --
    // `menu_action` only reaches Outer Shell (one menu level deep).
    // Submenu order matches the reference modeler's own listing.
    QAction* outerShellAction = toolsMenu->addAction(QStringLiteral("Outer Shell"));
    connect(outerShellAction, &QAction::triggered, this,
            [this]() { kernel_.send(events::ToolChanged{events::ToolId::OuterShell}); });
    toolActions_[events::ToolId::OuterShell] = outerShellAction;

    QMenu* solidToolsMenu = toolsMenu->addMenu(QStringLiteral("Solid Tools"));
    struct SolidToolEntry {
        QString label;
        events::ToolId tool;
    };
    const SolidToolEntry solidToolEntries[] = {
        {QStringLiteral("Union"), events::ToolId::SolidUnion},
        {QStringLiteral("Intersect"), events::ToolId::SolidIntersect},
        {QStringLiteral("Subtract"), events::ToolId::SolidSubtract},
        {QStringLiteral("Trim"), events::ToolId::SolidTrim},
        {QStringLiteral("Split"), events::ToolId::SolidSplit},
    };
    for (const SolidToolEntry& entry : solidToolEntries) {
        QAction* action = solidToolsMenu->addAction(entry.label);
        connect(action, &QAction::triggered, this,
                [this, tool = entry.tool]() { kernel_.send(events::ToolChanged{tool}); });
        toolActions_[entry.tool] = action;
    }

    QMenu* windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    addPlaceholder(windowMenu, QStringLiteral("Default Tray"));

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    addPlaceholder(helpMenu, QStringLiteral("About Planura"));
}

void MainWindow::buildTopToolbar() {
    QToolBar* toolbar = addToolBar(QStringLiteral("Standard"));
    toolbar->setObjectName(QStringLiteral("standardToolbar"));
    toolbar->setMovable(false);

    toolbar->addAction(QStringLiteral("New"))->setEnabled(false);
    toolbar->addAction(QStringLiteral("Open"))->setEnabled(false);
    toolbar->addAction(QStringLiteral("Save"))->setEnabled(false);
    toolbar->addSeparator();
}

void MainWindow::buildToolPalette() {
    QToolBar* palette = new QToolBar(QStringLiteral("Tools"), this);
    palette->setObjectName(QStringLiteral("toolPalette"));
    palette->setMovable(false);
    palette->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, palette);

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    struct ToolEntry {
        QString label;
        events::ToolId tool;
        QKeySequence shortcut;
    };
    // the reference modeler default shortcuts (feature spec §2.1/2.2).
    const ToolEntry entries[] = {
        {QStringLiteral("Select"), events::ToolId::Select, QKeySequence(Qt::Key_Space)},
        {QStringLiteral("Line"), events::ToolId::Line, QKeySequence(Qt::Key_L)},
        {QStringLiteral("Eraser"), events::ToolId::Eraser, QKeySequence(Qt::Key_E)},
        {QStringLiteral("Move"), events::ToolId::Move, QKeySequence(Qt::Key_M)},
        {QStringLiteral("Rectangle"), events::ToolId::Rectangle, QKeySequence(Qt::Key_R)},
        {QStringLiteral("Rotated Rectangle"), events::ToolId::RotatedRectangle, QKeySequence()},
        {QStringLiteral("Circle"), events::ToolId::Circle, QKeySequence(Qt::Key_C)},
        {QStringLiteral("Polygon"), events::ToolId::Polygon, QKeySequence()},
        {QStringLiteral("Push/Pull"), events::ToolId::PushPull, QKeySequence(Qt::Key_P)},
        {QStringLiteral("2-Point Arc"), events::ToolId::Arc2Point, QKeySequence(Qt::Key_A)},
        {QStringLiteral("3-Point Arc"), events::ToolId::Arc3Point, QKeySequence()},
        {QStringLiteral("Arc"), events::ToolId::ArcCenter, QKeySequence()},
        {QStringLiteral("Pie"), events::ToolId::Pie, QKeySequence()},
        {QStringLiteral("Freehand"), events::ToolId::Freehand, QKeySequence()},
        {QStringLiteral("Rotate"), events::ToolId::Rotate, QKeySequence(Qt::Key_Q)},
        {QStringLiteral("Scale"), events::ToolId::Scale, QKeySequence(Qt::Key_S)},
        {QStringLiteral("Offset"), events::ToolId::Offset, QKeySequence(Qt::Key_F)},
        // No shortcut -- the reference modeler's Flip tool has none either.
        {QStringLiteral("Flip"), events::ToolId::Flip, QKeySequence()},
        // No shortcut for Follow Me (deliberate scope exclusion).
        {QStringLiteral("Follow Me"), events::ToolId::FollowMe, QKeySequence()},
        // the reference modeler's own default Tape Measure shortcut.
        {QStringLiteral("Tape Measure"), events::ToolId::TapeMeasure, QKeySequence(Qt::Key_T)},
        {QStringLiteral("Protractor"), events::ToolId::Protractor, QKeySequence()},
        // No shortcut (toolbar entry only).
        {QStringLiteral("Axes"), events::ToolId::Axes, QKeySequence()},
        // No shortcut (toolbar entries only).
        {QStringLiteral("Dimension"), events::ToolId::Dimension, QKeySequence()},
        {QStringLiteral("Text"), events::ToolId::Text, QKeySequence()},
        // No shortcut (toolbar entry only).
        {QStringLiteral("Section Plane"), events::ToolId::SectionPlane, QKeySequence()},
        {QStringLiteral("Paint Bucket"), events::ToolId::PaintBucket, QKeySequence(Qt::Key_B)},
    };

    for (const ToolEntry& entry : entries) {
        QAction* action = palette->addAction(entry.label);
        action->setCheckable(true);
        action->setShortcut(entry.shortcut);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool = entry.tool]() { kernel_.send(events::ToolChanged{tool}); });
        if (entry.tool == events::ToolId::Select) {
            action->setChecked(true);
        }
        toolActions_[entry.tool] = action;
    }

    // Opens a dialog instead of switching the active tool, so it's not
    // checkable/grouped. Still registered in toolActions_ under Text3D so
    // the debug bridge's `tool` command can trigger it.
    QAction* text3dAction = palette->addAction(QStringLiteral("3D Text"));
    connect(text3dAction, &QAction::triggered, this, [this]() {
        // open() (non-blocking), not exec() -- exec() would hang the debug bridge.
        auto* dialog = new ui::Text3dDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QDialog::accepted, this, [this, dialog]() {
            // Fixed clear-ground origin (MVP; click-to-place needs ray/inference plumbing not yet built).
            kernel_.send(events::Add3dTextRequested{dialog->text().toStdString(), dialog->outlines(),
                                                      dialog->extrusion(), geo::Vec3{0.0, -6.0, 0.0}});
        });
        dialog->open();
    });
    toolActions_[events::ToolId::Text3D] = text3dAction;
}

void MainWindow::buildCentralViewport() {
    viewport_ = new viewport::ViewportWidget(this);
    viewport_->setObjectName(QStringLiteral("viewport"));
    setCentralWidget(viewport_);
}

void MainWindow::buildDefaultTray() {
    tray_ = new ui::Tray(this);
    addDockWidget(Qt::RightDockWidgetArea, tray_);
}

void MainWindow::buildStatusBar() {
    hintLabel_ = new QLabel(QStringLiteral("Select entities."), this);

    // Default label "Length" (the reference modeler's Line-tool default) until a tool
    // overrides it. returnPressed commits + selects all (the reference modeler keeps the text selected).
    vcbLabel_ = new QLabel(QStringLiteral("Length"), this);
    vcbEdit_ = new QLineEdit(this);
    vcbEdit_->setFixedWidth(120);
    vcbEdit_->setAlignment(Qt::AlignRight);
    connect(vcbEdit_, &QLineEdit::returnPressed, this, [this]() { commitVcbEntry(); });

    statusBar()->addWidget(hintLabel_, 1);
    statusBar()->addPermanentWidget(vcbLabel_);
    statusBar()->addPermanentWidget(vcbEdit_);
}

QAction* MainWindow::toolAction(events::ToolId tool) const {
    auto it = toolActions_.find(tool);
    return it != toolActions_.end() ? it->second : nullptr;
}

QAction* MainWindow::faceStyleAction(events::FaceStyle style) const {
    auto it = faceStyleActions_.find(style);
    return it != faceStyleActions_.end() ? it->second : nullptr;
}

QAction* MainWindow::edgeFlagAction(events::EdgeFlag flag) const {
    auto it = edgeFlagActions_.find(flag);
    return it != edgeFlagActions_.end() ? it->second : nullptr;
}

events::ToolId MainWindow::activeTool() const {
    // ToolController::activeToolId() is the source of truth (mirrors the
    // last handled ToolChanged), not a scan for a checked QAction -- breaks
    // for the non-checkable Solid Tools menu entries.
    return toolController_ ? toolController_->activeToolId() : events::ToolId::Select;
}

QString MainWindow::statusHintText() const {
    return hintLabel_ ? hintLabel_->text() : QString();
}

QString MainWindow::entityInfoText() const {
    return tray_ ? tray_->entityInfoLabel()->text() : QString();
}

QString MainWindow::vcbLabelText() const {
    return vcbLabel_ ? vcbLabel_->text() : QString();
}

QString MainWindow::vcbValueText() const {
    return vcbEdit_ ? vcbEdit_->text() : QString();
}

void MainWindow::commitVcbEntry() {
    kernel_.send(events::VcbCommitted{vcbEdit_->text().toStdString()});
    vcbEdit_->selectAll();
    setVcbEntryActive(false);
}

void MainWindow::onNewDocument() {
    if (!confirmDiscardChanges()) return;
    kernel_.send(events::NewDocumentRequested{});
}

void MainWindow::onOpenDocument() {
    if (!confirmDiscardChanges()) return;

    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open"), QString(),
                                                        QStringLiteral("Planura Model (*.plr)"));
    if (path.isEmpty()) return;  // dialog cancelled
    kernel_.send(events::OpenDocumentRequested{path.toStdString()});
}

void MainWindow::onSaveDocument() {
    auto document = kernel_.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document) return;

    if (!document->filePath().empty()) {
        kernel_.send(events::SaveDocumentRequested{document->filePath()});
        return;
    }
    // Untitled document: Save routes through Save As.
    onSaveDocumentAs();
}

bool MainWindow::onSaveDocumentAs() {
    // Plain QFileDialog (not getSaveFileName()) so setDefaultSuffix can force ".plr".
    QFileDialog dialog(this, QStringLiteral("Save As"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("Planura Model (*.plr)"));
    dialog.setDefaultSuffix(QStringLiteral("plr"));
    if (dialog.exec() != QDialog::Accepted) return false;  // dialog cancelled

    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return false;

    kernel_.send(events::SaveDocumentRequested{selected.constFirst().toStdString()});

    // Runs synchronously: dirty() clears on success, stays set (with
    // DocumentIoFailed dispatched) on failure -- this is the success signal.
    auto document = kernel_.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    return document && !document->dirty();
}

void MainWindow::onImportObj() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Import"), QString(), QStringLiteral("OBJ (*.obj)"));
    if (path.isEmpty()) return;  // dialog cancelled
    kernel_.send(events::ImportObjRequested{path.toStdString()});
}

void MainWindow::onExportObj() {
    QFileDialog dialog(this, QStringLiteral("Export"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("OBJ (*.obj)"));
    dialog.setDefaultSuffix(QStringLiteral("obj"));
    if (dialog.exec() != QDialog::Accepted) return;  // dialog cancelled

    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return;

    kernel_.send(events::ExportObjRequested{selected.constFirst().toStdString()});
}

bool MainWindow::confirmDiscardChanges() {
    auto document = kernel_.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document || !document->dirty()) return true;  // nothing to save

    const QString name = document->filePath().empty()
                              ? QStringLiteral("Untitled")
                              : QFileInfo(QString::fromStdString(document->filePath())).fileName();

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Planura"));
    box.setText(QStringLiteral("Do you want to save the changes you made to %1?").arg(name));
    box.setInformativeText(QStringLiteral("Your changes will be lost if you don't save them."));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);

    switch (box.exec()) {
        case QMessageBox::Discard:
            return true;
        case QMessageBox::Save:
            if (!document->filePath().empty()) {
                kernel_.send(events::SaveDocumentRequested{document->filePath()});
                return !document->dirty();  // false if the save actually failed
            }
            // Untitled: a cancelled/failed Save As aborts the whole calling action.
            return onSaveDocumentAs();
        default:  // Cancel, or the dialog closed via [x]
            return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::setVcbEntryActive(bool active) {
    vcbEntryActive_ = active;
    vcbEdit_->setProperty("vcbEntryActive", active);

    // VCB-grammar suffix letters (s/r/c) collide with tool shortcuts
    // (Scale/Rectangle/Circle); disabling every tool QAction removes them
    // from Qt's shortcut matching during an entry.
    for (auto& [tool, action] : toolActions_) {
        action->setEnabled(!active);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // A viewport press abandons a half-typed VCB entry.
    if (watched == viewport_ && event->type() == QEvent::MouseButtonPress) {
        setVcbEntryActive(false);
        return QMainWindow::eventFilter(watched, event);
    }

    if (watched == viewport_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        // Escape cancels the entry; not consumed, so the tool still sees it.
        if (keyEvent->key() == Qt::Key_Escape) {
            setVcbEntryActive(false);
            return QMainWindow::eventFilter(watched, event);
        }

        // Keyed on vcbEntryActive_ (with hasFocus() as fallback), not focus
        // alone -- see vcbEntryActive_'s member comment.
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            (vcbEntryActive_ || vcbEdit_->hasFocus())) {
            commitVcbEntry();
            return true;
        }

        // Gated on vcbEntryActive_ only (Backspace isn't in the
        // printable-ASCII range the branch below tests); no tool binds Key_Backspace.
        if (keyEvent->key() == Qt::Key_Backspace && vcbEntryActive_) {
            vcbEdit_->backspace();
            if (vcbEdit_->text().isEmpty()) setVcbEntryActive(false);
            return true;
        }

        const QString text = keyEvent->text();
        // Type-anywhere: any printable keystroke with the viewport focused
        // redirects to the VCB field, covering the full vcb_parser alphabet
        // (digits, '.'/'-', letters, ','/';', '+'). Escape/arrows/Delete
        // never produce text() so they're excluded for free; Space is
        // excluded explicitly (Select tool shortcut). Non-Shift modifiers
        // block the redirect (tool meaning instead); Qt::KeypadModifier is excluded so numpad digits still redirect.
        const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
        const bool modifierOk = mods == Qt::NoModifier || mods == Qt::ShiftModifier;

        // text() for a real keypress; falls back to the raw key code only
        // when text() is empty (the debug bridge's synthetic `key` presses
        // carry no text) -- Key's printable-ASCII values equal their own ASCII code.
        std::optional<QChar> ch;
        if (text.size() == 1) {
            ch = text.at(0);
        } else if (text.isEmpty() && keyEvent->key() >= 0x20 && keyEvent->key() <= 0x7e) {
            ch = QChar(static_cast<char16_t>(keyEvent->key()));
        }

        if (modifierOk && ch && ch->isPrint() && !ch->isSpace()) {
            if (!vcbEntryActive_) {
                // First character: replaces the field's live readout,
                // industry-standard. Tracked via vcbEntryActive_, never focus
                // (setFocus() is inert while the window is inactive under bridge-driven runs).
                vcbEdit_->setFocus();
                vcbEdit_->setText(QString(*ch));
            } else {
                // insert() drops any selection first, so typing right after
                // a commit's selectAll() replaces the old value.
                vcbEdit_->insert(QString(*ch));
            }
            setVcbEntryActive(true);
            return true;  // consumed -- the active tool never sees this keystroke
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

}  // namespace plnr
