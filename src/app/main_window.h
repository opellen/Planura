#pragma once

#include <unordered_map>

#include <QMainWindow>
#include <QString>

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter_host.h>

#include "agent/events.h"

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;

namespace plnr::viewport {
class ViewportWidget;
}  // namespace plnr::viewport

namespace plnr::ui {
class Tray;
}  // namespace plnr::ui

namespace plnr::tools {
class ToolController;
}  // namespace plnr::tools

namespace plnr {

// industry-standard main window shell: menu bar, tool palette, toolbar,
// central viewport, Default Tray dock, and status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(ordo::core::AppKernel& kernel, QWidget* parent = nullptr);

    // Non-owning; exposed for --screenshot debug capture and tool wiring.
    viewport::ViewportWidget* viewportWidget() const { return viewport_; }

    // Read-only window state for DebugBridge's `status`/`tool` commands. Non-owning.
    QAction* toolAction(events::ToolId tool) const;
    events::ToolId activeTool() const;
    QString statusHintText() const;
    // Backs DebugBridge's `entity_info` query.
    QString entityInfoText() const;
    // Backs DebugBridge's `vcb` query.
    QString vcbLabelText() const;
    QString vcbValueText() const;
    // Backs DebugBridge's `modal` command. Non-owning; constructed into
    // presenterHost_ in the ctor.
    tools::ToolController* toolController() const { return toolController_; }
    // View > Face Style submenu's QAction for one FaceStyle value. Backs
    // DebugBridge's `style` command (action:"set_face_style").
    QAction* faceStyleAction(events::FaceStyle style) const;
    // View > Edge Style submenu's QAction for one EdgeFlag value. Backs
    // DebugBridge's `style` command (action:"set_edge_flag").
    QAction* edgeFlagAction(events::EdgeFlag flag) const;
    // View > Ambient Occlusion QAction. Backs DebugBridge's `style` command
    // (action:"set_ambient_occlusion").
    QAction* ambientOcclusionAction() const { return ambientOcclusionAction_; }
    // View > Shadows QAction. Backs DebugBridge's `shadow` command (action:"set_show_shadows").
    QAction* shadowsAction() const { return shadowsAction_; }
    // View > "Use Sun for Shading" QAction. Backs DebugBridge's `shadow`
    // command (action:"set_use_sun_for_shading").
    QAction* useSunForShadingAction() const { return useSunForShadingAction_; }
    // View > Fog QAction. Backs DebugBridge's `fog` command (action:"set_enabled").
    QAction* fogAction() const { return fogAction_; }
    // Camera menu's exclusive Perspective / Parallel Projection QActions;
    // checked-state owned by ui::ViewportPresenter.
    QAction* perspectiveAction() const { return perspectiveAction_; }
    QAction* parallelProjectionAction() const { return parallelProjectionAction_; }
    // Same exclusive group's third entry, Two-Point Perspective.
    QAction* twoPointPerspectiveAction() const { return twoPointPerspectiveAction_; }
    // Camera menu's Previous/Next QActions (not checkable); enabled-state
    // owned by ui::ViewportPresenter.
    QAction* previousAction() const { return previousAction_; }
    QAction* nextAction() const { return nextAction_; }

protected:
    // Redirects a printable digit/'.'/'-' keypress on the viewport to the
    // VCB field (the reference modeler's type-anywhere behavior). Installed on viewport_
    // in the ctor.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // industry-standard "Do you want to save the changes...?" gate on window
    // close; accepts iff confirmDiscardChanges() allows it.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenuBar();
    void buildTopToolbar();
    void buildToolPalette();
    void buildCentralViewport();
    void buildDefaultTray();
    void buildStatusBar();

    // -- File menu ------------------------------------------------------

    // New: gated by confirmDiscardChanges(), then events::NewDocumentRequested.
    void onNewDocument();
    // Open...: gated by confirmDiscardChanges(); a cancelled dialog is a silent no-op.
    void onOpenDocument();
    // Save: sends SaveDocumentRequested if the document has a path;
    // otherwise falls through to Save As.
    void onSaveDocument();
    // Save As...: default ".plr" suffix. Returns false if cancelled or the
    // save failed; true otherwise. Shared with confirmDiscardChanges().
    bool onSaveDocumentAs();

    // -- Import/Export ----------------------------------------------------

    // Import...: OBJ pick feeding ImportObjRequested (adds to the live
    // document, so not gated by confirmDiscardChanges()). Cancel is a silent no-op.
    void onImportObj();
    // Export > OBJ...: read-only, no confirmDiscardChanges() gate; failure
    // is surfaced via the status bar hint.
    void onExportObj();

    // industry-standard unsaved-changes gate, shared by New/Open/window-close.
    // True immediately if not dirty; otherwise shows a Save/Discard/Cancel
    // prompt: Discard -> true, Cancel -> false, Save -> true iff the save
    // (Save or Save As) actually succeeded.
    bool confirmDiscardChanges();

    // Commits the VCB field (VcbCommitted + selectAll) and clears
    // vcbEntryActive_. Shared by vcbEdit_'s returnPressed and eventFilter().
    void commitVcbEntry();
    // Mirrors active onto vcbEdit_'s "vcbEntryActive" dynamic property, which
    // StatusBarPresenter reads to pause live VCB updates while typing.
    void setVcbEntryActive(bool active);

    ordo::core::AppKernel& kernel_;
    QLabel* hintLabel_ = nullptr;
    // VCB (Measurements Box) label + field. Default label "Length" (the reference modeler's
    // Line-tool default); text otherwise driven by StatusBarPresenter.
    QLabel* vcbLabel_ = nullptr;
    QLineEdit* vcbEdit_ = nullptr;
    // Tracked explicitly rather than via vcbEdit_->hasFocus(): debug-bridge
    // input runs with the window inactive, where focus-based checks fail.
    bool vcbEntryActive_ = false;
    viewport::ViewportWidget* viewport_ = nullptr;
    // Populated by buildDefaultTray(); backs entityInfoText().
    ui::Tray* tray_ = nullptr;
    // Populated by buildToolPalette(); backs toolAction()/activeTool().
    std::unordered_map<events::ToolId, QAction*> toolActions_;
    // Edit menu's Undo/Redo QActions; enabled state owned by UndoMenuPresenter.
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    // View > Face Style / Edge Style QActions; checked-state owned by ui::StyleMenuPresenter.
    std::unordered_map<events::FaceStyle, QAction*> faceStyleActions_;
    std::unordered_map<events::EdgeFlag, QAction*> edgeFlagActions_;
    // View > Ambient Occlusion QAction; checked-state owned by ui::StyleMenuPresenter.
    QAction* ambientOcclusionAction_ = nullptr;
    // View > Shadows / Use Sun for Shading / Fog QActions; checked-state
    // owned by ui::ShadowsMenuPresenter.
    QAction* shadowsAction_ = nullptr;
    QAction* useSunForShadingAction_ = nullptr;
    QAction* fogAction_ = nullptr;
    // Camera menu's projection QActions; checked-state owned by ui::ViewportPresenter.
    QAction* perspectiveAction_ = nullptr;
    QAction* parallelProjectionAction_ = nullptr;
    QAction* twoPointPerspectiveAction_ = nullptr;
    // Camera menu's Previous/Next QActions; enabled-state owned by ui::ViewportPresenter.
    QAction* previousAction_ = nullptr;
    QAction* nextAction_ = nullptr;

    // Declared after the widgets it references so presenters are destroyed
    // (and onRemove'd) before those widgets die.
    ordo::qt::PresenterHost presenterHost_;

    // Non-owning; returned by PresenterHost::add<tools::ToolController>() in
    // the ctor, owned by presenterHost_.
    tools::ToolController* toolController_ = nullptr;
};

}  // namespace plnr
