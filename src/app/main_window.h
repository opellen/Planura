#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QMainWindow>
#include <QString>

#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

#include "agent/events.h"
#include "document_session.h"
#include "ui/activity_rail.h"

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QObject;
class QPoint;
class QSplitter;
class QToolBar;

namespace plnr::viewport {
class ViewportWidget;
}  // namespace plnr::viewport

namespace plnr::ui {
class ActivityRail;
class ContextBar;
class EditorGroup;
class ProgressOverlay;
class RightTray;
}  // namespace plnr::ui

namespace plnr::infra {
class IoDispatcher;
enum class ExecutionPolicy;
}  // namespace plnr::infra

namespace plnr::tools {
class ToolController;
}  // namespace plnr::tools

namespace plnr {

// Which way a split or a tab move points, in screen terms.
enum class SplitDirection { Up, Down, Left, Right };

// Outcome of a bridge-driven tab close or move.
enum class SessionOpResult {
    Ok,
    BadIndex,   // no session at that index
    LastTab,    // close refused: the last session tab across all groups
    Cancelled,  // the dirty prompt was cancelled
    NoTarget,   // move refused: no group in that direction
};

// Main window shell: menus, tool palette, central viewport, activity rail,
// right tray, and status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // The focused document; same session as focusedSession().
    DocumentSession& activeSession() { return *focused_; }
    const DocumentSession& activeSession() const { return *focused_; }

    // The session commands resolve against: the current tab of the focused group.
    // A focused group with no session keeps the previous one bound, so this can
    // name a session that lives in another group. Backs DebugBridge's per-command
    // kernel resolution.
    DocumentSession& focusedSession() { return *focused_; }
    const DocumentSession& focusedSession() const { return *focused_; }

    // Open sessions, in creation order. Backs DebugBridge's `session_list`.
    int sessionCount() const { return static_cast<int>(sessions_.size()); }
    DocumentSession& sessionAt(int index) const { return *sessions_.at(static_cast<std::size_t>(index)); }

    // Backs DebugBridge's `session_focus`: makes that session's tab current in
    // its group and focuses the group. False when `index` is out of range.
    bool focusSessionByIndex(int index);

    // Backs DebugBridge's `session_new_tab`: opens an empty "Untitled" session
    // tab and returns its index.
    int newSessionTab();

    // Number of editor groups (splitter leaves).
    int groupCount() const { return static_cast<int>(groups_.size()); }
    // Visual (depth-first splitter) index of the group holding `session`'s tab, or -1.
    int groupIndexOf(const DocumentSession& session) const;

    // Backs DebugBridge's `session_split`: splits the focused group and returns
    // the group count. The new group is empty and takes focus.
    int splitFocusedGroup(SplitDirection direction);

    // Backs DebugBridge's `session_close`: the tab-close path (dirty prompt
    // included), keyed by session index.
    SessionOpResult closeSessionByIndex(int index);

    // Backs DebugBridge's `session_move`: moves that session's tab to the group
    // adjacent to its own in `direction`.
    SessionOpResult moveSessionToAdjacentGroup(int index, SplitDirection direction);

    // Called after every focus switch; DebugBridge registers it to re-point its
    // trace observer. The bridge holds the window, not the reverse.
    void setFocusChangedCallback(std::function<void()> callback) { focusChangedCallback_ = std::move(callback); }

    // Harness layout for agent-driven runs: hides the tab strip (all groups, incl. later
    // splits), activity rail, right tray and bottom panel so the viewport keeps its
    // full-window size. Call before show().
    void setHarnessLayout(bool on);

    // Forwards to the IO dispatcher; agent-driven runs pick DirectSynchronous so
    // scenarios do not depend on worker timing.
    void setIoExecutionPolicy(infra::ExecutionPolicy policy);

    // Non-owning; exposed for --screenshot debug capture and tool wiring.
    viewport::ViewportWidget* viewportWidget() const { return activeSession().viewport(); }

    // Read-only window state for DebugBridge's `status`/`tool` commands. Non-owning.
    QAction* toolAction(events::ToolId tool) const;
    events::ToolId activeTool() const;
    QString statusHintText() const;
    // Backs DebugBridge's `entity_info` query.
    QString entityInfoText() const;
    // Backs DebugBridge's `vcb` query.
    QString vcbLabelText() const;
    QString vcbValueText() const;
    // Backs DebugBridge's `modal` command. Non-owning; owned by the active session.
    tools::ToolController* toolController() const { return activeSession().toolController(); }
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
    // Redirects a printable digit/'.'/'-' keypress on the viewport to the VCB field.
    // Installed on every session's viewport at creation; acts on the focused one.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Unsaved-changes gate on window close; accepts iff confirmDiscardChanges() allows it.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenuBar();
    // Adds the ContextBar as the only top toolbar.
    void buildContextBar();
    void addToolActions(ui::ActivityRail* rail);
    void buildCentralViewport();
    void buildActivityRail();
    // Single consumer of the shell mode: swaps the rail's tool section, the context
    // bar's tool cluster and the tray panel set.
    void applyMode(ui::ShellMode mode);
    void buildStatusBar();
    // View > Appearance toggles.
    void wireAppearanceActions();
    void syncAppearanceAction(QObject* widget);

    // Destroys and recreates viewHost_ on session.kernel(), then re-adds the app-level
    // presenters in their load-bearing order. Does not re-send transient state
    // (tool/VCB); focusSession() does that.
    void rebindPresentersToSession(DocumentSession& session);

    // Builds a session (agents, commands, viewport), adds its tab to the focused
    // group, and so focuses it.
    DocumentSession& createSessionTab();

    // Focus switch (group currentChanged or click into a group): rebinds the presenters,
    // notifies the bridge, re-arms the app-global tool on the new session. No-op for null
    // or the already-focused session unless `force` (a tab move destroys the moving
    // session's presenters).
    void focusSession(DocumentSession* session, bool force = false);

    // Makes `session` current in its group, focuses the group, and focuses the session.
    void activateSession(DocumentSession* session);

    // Tab close request: false when refused (last session tab) or cancelled at the
    // dirty prompt. Otherwise focuses a neighbor if needed, destroys tab, viewport and
    // session, and collapses the group if it emptied.
    bool closeSessionTab(DocumentSession* session);

    // -- Editor groups ----------------------------------------------------

    // Builds a group wired to this window (focus, close, context menu) and
    // registers it in groups_; the caller places it in the splitter tree.
    ui::EditorGroup* createGroup();
    // Inserts a new empty group beside `group` (reuses the parent splitter if its
    // orientation matches or it has one child, else nests) and focuses it. `before`
    // puts it left/above. Returns the new group.
    ui::EditorGroup* splitGroup(ui::EditorGroup* group, Qt::Orientation orientation, bool before);
    // Erases an emptied group and unwraps single-child nested splitters. The
    // last group is never removed.
    void handleGroupEmptied(ui::EditorGroup* group);
    // Moves the focus ring to `group`. Presenters stay bound to the previous
    // session when the group has none.
    void setFocusedGroup(ui::EditorGroup* group);
    // setFocusedGroup(group) plus focusSession() on the group's current tab.
    void activateGroup(ui::EditorGroup* group);
    // Re-evaluates what depends on the group count: the focus ring's reserved
    // margin and the close buttons' last-tab rule.
    void refreshGroupChrome();
    ui::EditorGroup* groupOf(const DocumentSession* session) const;
    // Leaves of the splitter tree in depth-first (visual) order.
    std::vector<ui::EditorGroup*> orderedGroups() const;
    // The group next to `group` in `direction`, by splitter structure: climb to
    // the nearest ancestor splitter on that axis with a sibling that way, then
    // descend into the sibling (nearest leaf; first child across the axis).
    ui::EditorGroup* adjacentGroup(ui::EditorGroup* group, SplitDirection direction) const;
    // Tab move by view recreation: never reparents a shown viewport.
    void moveSessionTab(DocumentSession* session, ui::EditorGroup* target);
    void showTabContextMenu(ui::EditorGroup* group, int tabIndex, const QPoint& globalPos);
    DocumentSession* sessionOfViewport(const QObject* viewport) const;

    // -- File menu ------------------------------------------------------

    // New: opens an empty "Untitled" session tab.
    void onNewDocument();
    // Open...: file dialog (a cancel is a silent no-op), then a new session
    // tab that receives OpenDocumentRequested.
    void onOpenDocument();
    // Save: with a path, takes the snapshot synchronously (SaveSnapshotRequested) and writes it on
    // a worker via beginAsyncSave(); otherwise falls through to Save As (which stays synchronous).
    void onSaveDocument();
    // Worker half of the async save, reached through ui::IoPresenter. Re-validates `session`
    // against sessions_ before SaveCommitted/failure events are sent.
    void beginAsyncSave(DocumentSession& session, const events::DocumentSnapshotReady& snapshot);
    // True while `session` is still owned by sessions_ (a closed tab drops its completions).
    bool ownsSession(const DocumentSession* session) const;
    // Wires the dispatcher's signals to the overlay, once.
    void buildIoOverlay();
    // Single-op guard: one IO operation per session at a time.
    // beginIoOp() marks `session` busy, or returns false after a status hint when it already is;
    // every completion and failure callback calls endIoOp().
    bool beginIoOp(DocumentSession& session);
    void endIoOp(const DocumentSession* session) { ioBusy_.erase(session); }
    // Save As...: default ".plr" suffix. Returns false if cancelled or the
    // save failed; true otherwise. Shared with confirmDiscardChanges().
    bool onSaveDocumentAs();
    // Save As for a specific session (confirmDiscardChanges() iterates sessions).
    bool saveSessionAs(DocumentSession& session);

    // -- Import/Export ----------------------------------------------------

    // Import...: OBJ pick; the file is read + parsed on a worker, then ImportObjDataReady applies it
    // on the focused session (adds to the live document, so not gated by confirmDiscardChanges()).
    // Cancel is a silent no-op.
    void onImportObj();
    // Export > OBJ...: read-only, no confirmDiscardChanges() gate. Takes the bytes synchronously
    // (ExportObjSnapshotRequested) and writes them on a worker via beginAsyncExportObj(); failure
    // is surfaced via the status bar hint.
    void onExportObj();
    // Worker half of the async export, reached through ui::IoPresenter; same session
    // re-validation as beginAsyncSave(). Never touches document state.
    void beginAsyncExportObj(DocumentSession& session, const events::ObjBytesReady& obj);

    // Unsaved-changes gate for window close: runs confirmDiscardSession() over every
    // session; the first false aborts.
    bool confirmDiscardChanges();
    // One session's gate, shared with tab close. True immediately if not
    // dirty; otherwise shows a Save/Discard/Cancel prompt: Discard -> true,
    // Cancel -> false, Save -> true iff the save (Save or Save As) succeeded.
    bool confirmDiscardSession(DocumentSession& session);

    // Commits the VCB field (VcbCommitted + selectAll) and clears
    // vcbEntryActive_. Shared by vcbEdit_'s returnPressed and eventFilter().
    void commitVcbEntry();
    // Mirrors active onto vcbEdit_'s "vcbEntryActive" dynamic property, which
    // StatusBarPresenter reads to pause live VCB updates while typing.
    void setVcbEntryActive(bool active);

    ordo::core::Kernel& focusedKernel() { return focusedSession().kernel(); }

    // Declared before every widget/presenter member: the session's kernel
    // must outlive them (members destroy in reverse order).
    std::vector<std::unique_ptr<DocumentSession>> sessions_;
    // Non-owning; the session commands and presenters currently target.
    DocumentSession* focused_ = nullptr;
    std::function<void()> focusChangedCallback_;
    QLabel* hintLabel_ = nullptr;
    // VCB (Measurements Box) label + field. Default label "Length" (the reference modeler's
    // Line-tool default); text otherwise driven by StatusBarPresenter.
    QLabel* vcbLabel_ = nullptr;
    QLineEdit* vcbEdit_ = nullptr;
    // Tracked explicitly rather than via vcbEdit_->hasFocus(): debug-bridge
    // input runs with the window inactive, where focus-based checks fail.
    bool vcbEntryActive_ = false;
    // Activity rail and the right tray; both owned by the Qt tree (rail by the
    // main window, tray by topSplitter_).
    ui::ActivityRail* activityRail_ = nullptr;
    // Top options bar; owned by the main window's toolbar area.
    ui::ContextBar* contextBar_ = nullptr;
    ui::RightTray* rightTray_ = nullptr;
    QWidget* bottomPanel_ = nullptr;  // empty placeholder, hidden by default
    // Central widget: innerSplitter_ V { topSplitter_ H { rootSplitter_, rightTray_ },
    // bottomPanel_ }. Built by buildCentralViewport().
    QSplitter* innerSplitter_ = nullptr;
    QSplitter* topSplitter_ = nullptr;
    // Editor area (objectName "editorRoot"): a recursive QSplitter tree whose
    // leaves are the editor groups.
    QSplitter* rootSplitter_ = nullptr;
    // Every group, creation order; non-owning (the splitter tree owns them).
    std::vector<ui::EditorGroup*> groups_;
    // The group holding the focus ring; its current tab is the focused session.
    ui::EditorGroup* focusedGroup_ = nullptr;
    bool harnessLayout_ = false;
    // View > Appearance QActions; checked state mirrors the widget's visibility.
    QAction* trayAction_ = nullptr;
    QAction* panelAction_ = nullptr;
    QAction* statusBarAction_ = nullptr;
    // Window > tray section toggles; checked state mirrors the section's visibility.
    QAction* entityInfoSectionAction_ = nullptr;
    QAction* materialsSectionAction_ = nullptr;
    QAction* tagsSectionAction_ = nullptr;
    QAction* stylesSectionAction_ = nullptr;
    // Set while a tab is removed for a close or move, so the source group's
    // currentChanged does not steal focus.
    bool suppressGroupFocus_ = false;
    // Populated by addToolActions(); backs toolAction()/activeTool().
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

    // Async .plr Open/Save orchestration. The dispatcher's four signals drive the overlay
    // directly (shell chrome, no kernel hop); overlayCancel -> dispatcher.cancel.
    // Declared before viewHost_ so presenters die first.
    infra::IoDispatcher* ioDispatcher_ = nullptr;
    ui::ProgressOverlay* progressOverlay_ = nullptr;
    // Sessions with an IO operation in flight; see beginIoOp().
    std::unordered_set<const DocumentSession*> ioBusy_;

    // Declared after the widgets it references so presenters are destroyed
    // (and onRemove'd) before those widgets die.
    std::unique_ptr<ordo::qt::ViewHost> viewHost_;
};

}  // namespace plnr
