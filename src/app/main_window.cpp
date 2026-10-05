#include "main_window.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QByteArray>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIODevice>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPoint>
#include <QSaveFile>
#include <QScrollBar>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <geo/vec3.h>

#include "agent/document_store.h"
#include "agent/events.h"
#include "agent/selection_store.h"
#include "constants/design_tokens.h"
#include "infra/io_dispatcher.h"
#include "infra/settings_store.h"
#include "io/obj_reader.h"
#include "io/plr_container.h"
#include "io/plr_reader.h"
#include "tools/tool_controller.h"
#include "ui/activity_rail.h"
#include "ui/context_bar.h"
#include "ui/context_bar_presenter.h"
#include "ui/editor_group.h"
#include "ui/entity_info_presenter.h"
#include "ui/icons.h"
#include "ui/io_presenter.h"
#include "ui/progress_overlay.h"
#include "ui/materials_presenter.h"
#include "ui/shadows_menu_presenter.h"
#include "ui/status_bar_presenter.h"
#include "ui/style_menu_presenter.h"
#include "ui/tab_badge_adapter.h"
#include "ui/side_bar.h"
#include "ui/tags_presenter.h"
#include "ui/text3d_dialog.h"
#include "ui/title_bar_presenter.h"
#include "ui/undo_menu_presenter.h"
#include "ui/viewport_presenter.h"
#include "viewport/viewport_widget.h"

namespace plnr {

namespace {

// Loads a toolbar/menu icon from the compiled :/icons/ resource.
QIcon appIcon(const QString& slug) {
    return ui::icons::icon(slug);
}

// Adds a disabled placeholder QAction (menu scaffolding). Optional iconSlug sets an
// icon without enabling/wiring the action.
void addPlaceholder(QMenu* menu, const QString& text, const QString& iconSlug = QString()) {
    QAction* action = menu->addAction(text);
    action->setEnabled(false);
    if (!iconSlug.isEmpty()) {
        action->setIcon(appIcon(iconSlug));
    }
}

// File-dialog start directory, shared by Open / Save As / Import / Export; falls back
// to the dialog default when no store is active.
constexpr const char* kLastDirKey = "files.lastDir";

QString lastDialogDir() {
    infra::SettingsStore* store = infra::SettingsStore::activeStore();
    return store ? store->get(QString::fromLatin1(kLastDirKey)).toString() : QString();
}

void rememberDialogDir(const QString& pickedFile) {
    infra::SettingsStore* store = infra::SettingsStore::activeStore();
    if (!store || pickedFile.isEmpty()) return;
    store->set(QString::fromLatin1(kLastDirKey), QFileInfo(pickedFile).absolutePath());
}

std::vector<std::unique_ptr<DocumentSession>> makeFirstSession() {
    std::vector<std::unique_ptr<DocumentSession>> sessions;
    sessions.push_back(std::make_unique<DocumentSession>());
    return sessions;
}

// Keeps `session`'s tab text in step with its badge. The group is looked up per signal
// because a tab can move between groups; the link dies with the session.
void connectTabBadge(DocumentSession& session, std::function<ui::EditorGroup*(DocumentSession*)> groupOf) {
    ui::TabBadgeAdapter* badge = session.badge();
    DocumentSession* sessionPtr = &session;
    QObject::connect(badge, &ui::TabBadgeAdapter::stateChanged, badge, [badge, sessionPtr, groupOf] {
        if (ui::EditorGroup* group = groupOf(sessionPtr)) {
            group->setSessionTabTitle(sessionPtr, badge->displayText());
        }
    });
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      sessions_(makeFirstSession()) {
    focused_ = sessions_.front().get();
    // Title text is owned by TitleBarPresenter (registered below); no placeholder needed.
    resize(1440, 900);

    buildMenuBar();
    buildContextBar();
    buildActivityRail();
    buildCentralViewport();
    buildStatusBar();
    wireAppearanceActions();
    applyMode(ui::ShellMode::Edit);
    buildIoOverlay();

    // Type-anywhere: digit/'.'/'-' keypresses redirect to the VCB field (see eventFilter()).
    viewport::ViewportWidget* viewport = activeSession().viewport();
    viewport->installEventFilter(this);

    rebindPresentersToSession(activeSession());
}

DocumentSession& MainWindow::createSessionTab() {
    // The tab text comes from the session's badge; the caller's title is superseded.
    auto owned = std::make_unique<DocumentSession>();
    DocumentSession& session = *owned;
    session.registerCommands();
    session.createViewObjects();
    session.viewport()->installEventFilter(this);
    sessions_.push_back(std::move(owned));
    // The tab becomes current, so currentChanged -> focusSession() rebinds.
    focusedGroup_->addSessionTab(session, session.badge()->displayText());
    connectTabBadge(session, [this](DocumentSession* s) { return groupOf(s); });
    refreshGroupChrome();
    return session;
}

int MainWindow::newSessionTab() {
    createSessionTab();
    return sessionCount() - 1;
}

bool MainWindow::focusSessionByIndex(int index) {
    if (index < 0 || index >= sessionCount()) return false;
    activateSession(&sessionAt(index));
    return true;
}

void MainWindow::activateSession(DocumentSession* session) {
    ui::EditorGroup* group = groupOf(session);
    if (!group) return;
    group->setCurrentSession(session);  // a change fires currentChanged -> focus
    setFocusedGroup(group);
    focusSession(session);
}

void MainWindow::focusSession(DocumentSession* session, bool force) {
    // A null session is a group with no tab: the presenters stay bound to the
    // previous session until a tab lands there.
    if (!session || (!force && session == focused_)) return;
    focused_ = session;

    // Rebinding a session that has a tool host would subscribe ToolController to
    // ToolChanged before the new StatusBarPresenter, so tear the host down (Select first,
    // so the old tool runs onDeactivate) and let the rebind recreate it after the presenters.
    if (session->toolController()) {
        session->kernel().send(events::ToolChanged{events::ToolId::Select});
        session->resetToolHost();
    }

    // A half-typed VCB entry belongs to the session we left.
    if (vcbEntryActive_) setVcbEntryActive(false);

    rebindPresentersToSession(*session);
    if (focusChangedCallback_) focusChangedCallback_();

    // The tool is app-global: arm the checked palette tool on the new session after the
    // rebind, so its activation hint lands after StatusBarPresenter's prompt.
    events::ToolId tool = events::ToolId::Select;
    for (const auto& [id, action] : toolActions_) {
        if (action->isChecked()) {
            tool = id;
            break;
        }
    }
    // The bar's cached segment count must be read before the re-arm: the tool's
    // onActivate reports its default and overwrites the cache.
    const std::optional<int> cachedSegments = contextBar_ ? contextBar_->lastSegments(tool) : std::nullopt;
    session->kernel().send(events::ToolChanged{tool});
    // Per-tool option state is app-global too: re-apply it.
    if (cachedSegments) session->kernel().send(events::ToolSegmentsRequested{tool, *cachedSegments});
}

bool MainWindow::closeSessionTab(DocumentSession* session) {
    // A session with an IO operation in flight stays open.
    if (session && ioBusy_.count(session) != 0) {
        focusedKernel().send(events::StatusHintChanged{"An IO operation is already running."});
        return false;
    }
    // Across all groups: the last session tab never closes.
    if (!session || sessions_.size() <= 1) return false;
    ui::EditorGroup* source = groupOf(session);
    if (!source || !confirmDiscardSession(*session)) return false;

    if (session == focused_) {
        // Focus a neighbor first (next, else previous in its group, else another
        // group's current tab): the rebind drops every presenter bound to the
        // closing session before it is destroyed.
        const int index = source->indexOf(session);
        DocumentSession* neighbor = nullptr;
        for (int i = index + 1; i < source->count() && !neighbor; ++i) neighbor = source->sessionAt(i);
        for (int i = index - 1; i >= 0 && !neighbor; --i) neighbor = source->sessionAt(i);
        for (ui::EditorGroup* other : orderedGroups()) {
            if (neighbor) break;
            if (other != source) neighbor = other->currentSession();
        }
        if (!neighbor) return false;
        activateSession(neighbor);
    }

    // Destroys the page and viewport first, then the session that owned them.
    suppressGroupFocus_ = true;
    source->removeSessionTab(session);
    suppressGroupFocus_ = false;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->get() == session) {
            sessions_.erase(it);
            break;
        }
    }
    if (source->count() == 0) handleGroupEmptied(source);
    refreshGroupChrome();
    return true;
}

SessionOpResult MainWindow::closeSessionByIndex(int index) {
    if (index < 0 || index >= sessionCount()) return SessionOpResult::BadIndex;
    if (sessions_.size() <= 1) return SessionOpResult::LastTab;
    return closeSessionTab(&sessionAt(index)) ? SessionOpResult::Ok : SessionOpResult::Cancelled;
}

SessionOpResult MainWindow::moveSessionToAdjacentGroup(int index, SplitDirection direction) {
    if (index < 0 || index >= sessionCount()) return SessionOpResult::BadIndex;
    DocumentSession* session = &sessionAt(index);
    ui::EditorGroup* target = adjacentGroup(groupOf(session), direction);
    if (!target) return SessionOpResult::NoTarget;
    moveSessionTab(session, target);
    return SessionOpResult::Ok;
}

int MainWindow::splitFocusedGroup(SplitDirection direction) {
    const bool horizontal = direction == SplitDirection::Left || direction == SplitDirection::Right;
    const bool before = direction == SplitDirection::Left || direction == SplitDirection::Up;
    splitGroup(focusedGroup_, horizontal ? Qt::Horizontal : Qt::Vertical, before);
    return groupCount();
}

int MainWindow::groupIndexOf(const DocumentSession& session) const {
    const ui::EditorGroup* group = groupOf(&session);
    const std::vector<ui::EditorGroup*> ordered = orderedGroups();
    const auto it = std::find(ordered.begin(), ordered.end(), group);
    return it == ordered.end() ? -1 : static_cast<int>(it - ordered.begin());
}

// ---- Editor groups --------------------------------------------------------

ui::EditorGroup* MainWindow::createGroup() {
    auto* group = new ui::EditorGroup();
    group->setTabStripVisible(!harnessLayout_);
    // Closable while more than one session tab exists in total.
    group->setCloseGuard([this] { return sessions_.size() > 1; });
    groups_.push_back(group);

    connect(group, &ui::EditorGroup::currentChanged, this, [this, group](DocumentSession* session) {
        if (suppressGroupFocus_) return;
        setFocusedGroup(group);
        focusSession(session);
    });
    connect(group, &ui::EditorGroup::focusRequested, this, [this, group] { activateGroup(group); });
    connect(group, &ui::EditorGroup::tabCloseRequested, this,
            [this](DocumentSession* session) { closeSessionTab(session); });
    connect(group, &ui::EditorGroup::tabContextMenuRequested, this,
            [this, group](int tabIndex, const QPoint& globalPos) { showTabContextMenu(group, tabIndex, globalPos); });
    return group;
}

ui::EditorGroup* MainWindow::splitGroup(ui::EditorGroup* group, Qt::Orientation orientation, bool before) {
    auto* parentSplitter = qobject_cast<QSplitter*>(group->parentWidget());
    if (parentSplitter == nullptr) return nullptr;  // a docked group's parent is always a QSplitter

    ui::EditorGroup* newGroup = createGroup();

    // `before` turns Split Right/Down into Split Left/Up (insert before `group`).
    const int offset = before ? 0 : 1;
    if (parentSplitter->count() == 1 || parentSplitter->orientation() == orientation) {
        // No established orientation yet (one child), or it already matches:
        // the new group becomes a sibling of `group`.
        parentSplitter->setOrientation(orientation);
        const int index = parentSplitter->indexOf(group);
        parentSplitter->insertWidget(index + offset, newGroup);
    } else {
        // The splitter holds several widgets the other way: wrap `group` alone in a
        // nested splitter of the requested orientation (reparents within the same
        // top-level window).
        const int index = parentSplitter->indexOf(group);
        auto* nested = new QSplitter(orientation);
        nested->setChildrenCollapsible(false);
        nested->addWidget(group);
        nested->insertWidget(offset, newGroup);
        parentSplitter->insertWidget(index, nested);
    }

    refreshGroupChrome();
    // The new group is empty; presenters stay on the previous session until a tab lands in it.
    setFocusedGroup(newGroup);
    return newGroup;
}

void MainWindow::handleGroupEmptied(ui::EditorGroup* group) {
    if (groups_.size() <= 1) return;  // the last group stays, showing its empty hint

    auto* parentSplitter = qobject_cast<QSplitter*>(group->parentWidget());
    groups_.erase(std::remove(groups_.begin(), groups_.end(), group), groups_.end());
    if (focusedGroup_ == group) focusedGroup_ = nullptr;
    group->setParent(nullptr);
    // Deferred: the call stack may still be inside the group's own signal handling.
    group->deleteLater();

    // Unwrap nested splitters left with a single child (inverse of splitGroup()'s wrap).
    while (parentSplitter != nullptr && parentSplitter != rootSplitter_ && parentSplitter->count() == 1) {
        auto* grandParent = qobject_cast<QSplitter*>(parentSplitter->parentWidget());
        if (grandParent == nullptr) break;
        const int index = grandParent->indexOf(parentSplitter);
        QWidget* orphan = parentSplitter->widget(0);
        grandParent->insertWidget(index, orphan);
        parentSplitter->setParent(nullptr);
        parentSplitter->deleteLater();
        parentSplitter = grandParent;
    }

    if (focusedGroup_ == nullptr) {
        ui::EditorGroup* next = groupOf(focused_);
        if (!next && !groups_.empty()) next = groups_.front();
        setFocusedGroup(next);
    }
    refreshGroupChrome();
}

void MainWindow::setFocusedGroup(ui::EditorGroup* group) {
    if (focusedGroup_ == group) return;
    if (focusedGroup_ != nullptr) focusedGroup_->setFocused(false);
    focusedGroup_ = group;
    if (focusedGroup_ != nullptr) focusedGroup_->setFocused(true);
}

void MainWindow::activateGroup(ui::EditorGroup* group) {
    setFocusedGroup(group);
    focusSession(group->currentSession());
}

void MainWindow::refreshGroupChrome() {
    const bool shared = groups_.size() > 1;
    for (ui::EditorGroup* group : groups_) {
        group->setRingReserved(shared);
        group->refreshCloseButtons();
    }
}

ui::EditorGroup* MainWindow::groupOf(const DocumentSession* session) const {
    for (ui::EditorGroup* group : groups_) {
        if (group->indexOf(session) >= 0) return group;
    }
    return nullptr;
}

std::vector<ui::EditorGroup*> MainWindow::orderedGroups() const {
    std::vector<ui::EditorGroup*> out;
    const std::function<void(QWidget*)> walk = [&](QWidget* widget) {
        if (auto* splitter = qobject_cast<QSplitter*>(widget)) {
            for (int i = 0; i < splitter->count(); ++i) walk(splitter->widget(i));
        } else if (auto* group = qobject_cast<ui::EditorGroup*>(widget)) {
            out.push_back(group);
        }
    };
    walk(rootSplitter_);
    return out;
}

ui::EditorGroup* MainWindow::adjacentGroup(ui::EditorGroup* group, SplitDirection direction) const {
    if (!group) return nullptr;
    const bool horizontal = direction == SplitDirection::Left || direction == SplitDirection::Right;
    const Qt::Orientation axis = horizontal ? Qt::Horizontal : Qt::Vertical;
    const bool forward = direction == SplitDirection::Right || direction == SplitDirection::Down;

    QWidget* node = group;
    while (auto* parent = qobject_cast<QSplitter*>(node->parentWidget())) {
        if (parent->orientation() == axis) {
            const int sibling = parent->indexOf(node) + (forward ? 1 : -1);
            if (sibling >= 0 && sibling < parent->count()) {
                QWidget* target = parent->widget(sibling);
                // Nearest leaf along the axis; across it, the first child.
                while (auto* splitter = qobject_cast<QSplitter*>(target)) {
                    const bool alongAxis = splitter->orientation() == axis;
                    target = splitter->widget(alongAxis && !forward ? splitter->count() - 1 : 0);
                }
                return qobject_cast<ui::EditorGroup*>(target);
            }
        }
        node = parent;
    }
    return nullptr;
}

void MainWindow::moveSessionTab(DocumentSession* session, ui::EditorGroup* target) {
    ui::EditorGroup* source = groupOf(session);
    if (!session || !target || !source || source == target) return;
    const QString title = source->tabTitle(session);
    const bool wasFocused = session == focused_;

    // Nothing may hold the dying viewport. The tool host's controller
    // references it: deactivate the tool (clears its preview), then drop the host.
    if (session->toolController()) {
        session->kernel().send(events::ToolChanged{events::ToolId::Select});
        session->resetToolHost();
    }
    // The bound presenters reference the focused session's viewport.
    if (wasFocused) viewHost_.reset();

    suppressGroupFocus_ = true;
    source->removeSessionTab(session);  // page and viewport destroyed
    suppressGroupFocus_ = false;

    session->recreateViewObjects();
    session->viewport()->installEventFilter(this);
    // Becomes current in `target`: currentChanged focuses the group and, for a
    // session that was not focused, rebinds the presenters.
    target->addSessionTab(*session, title);
    // A focused session short-circuits focusSession(); force the rebind its
    // presenters lost above (it also re-sends the tool).
    if (wasFocused) focusSession(session, /*force=*/true);

    if (source->count() == 0) handleGroupEmptied(source);
    refreshGroupChrome();
}

void MainWindow::showTabContextMenu(ui::EditorGroup* group, int tabIndex, const QPoint& globalPos) {
    DocumentSession* session = group->sessionAt(tabIndex);
    if (!session) return;
    // Focus the group first so "focused" and "menu target" cannot diverge.
    activateGroup(group);

    const bool closable = sessions_.size() > 1;
    QMenu menu(this);
    QAction* closeAction = menu.addAction(QStringLiteral("Close"));
    closeAction->setEnabled(closable);
    QAction* closeOthersAction = menu.addAction(QStringLiteral("Close Others"));
    closeOthersAction->setEnabled(group->count() > 1);
    menu.addSeparator();

    struct DirectionEntry {
        const char* name;
        SplitDirection direction;
    };
    const DirectionEntry directions[] = {{"Up", SplitDirection::Up},
                                         {"Down", SplitDirection::Down},
                                         {"Left", SplitDirection::Left},
                                         {"Right", SplitDirection::Right}};
    std::vector<std::pair<QAction*, SplitDirection>> splitActions;
    for (const DirectionEntry& entry : directions) {
        splitActions.emplace_back(menu.addAction(QStringLiteral("Split %1").arg(QLatin1String(entry.name))),
                                  entry.direction);
    }
    menu.addSeparator();
    // Enabled only when a group exists that way (splitter-structure adjacency).
    std::vector<std::pair<QAction*, ui::EditorGroup*>> moveActions;
    for (const DirectionEntry& entry : directions) {
        ui::EditorGroup* neighbor = adjacentGroup(group, entry.direction);
        QAction* action = menu.addAction(QStringLiteral("Move to Group %1").arg(QLatin1String(entry.name)));
        action->setEnabled(neighbor != nullptr);
        moveActions.emplace_back(action, neighbor);
    }

    QAction* chosen = menu.exec(globalPos);
    if (chosen == nullptr) return;
    if (chosen == closeAction) {
        closeSessionTab(session);
    } else if (chosen == closeOthersAction) {
        std::vector<DocumentSession*> others;
        for (int i = 0; i < group->count(); ++i) {
            DocumentSession* other = group->sessionAt(i);
            if (other && other != session) others.push_back(other);
        }
        for (DocumentSession* other : others) {
            if (!closeSessionTab(other)) break;  // a cancelled prompt stops the sweep
        }
    } else {
        for (const auto& [action, direction] : splitActions) {
            if (chosen == action) {
                const bool horizontal = direction == SplitDirection::Left || direction == SplitDirection::Right;
                const bool before = direction == SplitDirection::Left || direction == SplitDirection::Up;
                splitGroup(group, horizontal ? Qt::Horizontal : Qt::Vertical, before);
                return;
            }
        }
        for (const auto& [action, neighbor] : moveActions) {
            if (chosen == action) {
                moveSessionTab(session, neighbor);
                return;
            }
        }
    }
}

DocumentSession* MainWindow::sessionOfViewport(const QObject* viewport) const {
    for (const auto& session : sessions_) {
        if (session->viewport() == viewport) return session.get();
    }
    return nullptr;
}

void MainWindow::rebindPresentersToSession(DocumentSession& session) {
    // LIFO-destroys the old presenters (their Qt connections die with them) before the
    // host is recreated on the session's kernel. The first call finds viewHost_ empty.
    viewHost_.reset();
    viewHost_ = std::make_unique<ordo::qt::ViewHost>(session.kernel());

    viewHost_->add<ui::StatusBarPresenter>(hintLabel_, vcbLabel_, vcbEdit_);
    // Before createToolHost() for the same reason as StatusBarPresenter: the bar
    // must see ToolChanged (face, spinner hidden) before the tool reports its
    // segments during activation.
    viewHost_->add<ui::ContextBarPresenter>(contextBar_, [this](events::ToolId tool) { return toolAction(tool); });
    if (auto document = session.kernel().agentAs<agent::DocumentStore>(agent::kDocumentStoreName)) {
        contextBar_->setUnits(QString::fromStdString(document->units()));
    }
    // ViewportPresenter owns checked-state for the projection actions and
    // enabled-state for previous/next.
    viewHost_->add<ui::ViewportPresenter>(session.viewport(), perspectiveAction_, parallelProjectionAction_,
                                               twoPointPerspectiveAction_, previousAction_, nextAction_);
    // Order is load-bearing: StatusBarPresenter must subscribe to ToolChanged before
    // ToolController so a tool's hint overwrites the default prompt.
    // Backs DebugBridge's `modal` command.
    session.createToolHost();
    ui::EntityInfoView* entityInfo = rightTray_->entityInfoView();
    ui::MaterialsView* materials = rightTray_->materialsView();
    ui::TagsView* tags = rightTray_->tagsView();
    viewHost_->add<ui::EntityInfoPresenter>(entityInfo->entityInfoLabel());
    viewHost_->add<ui::TagsPresenter>(tags->tagsList(), tags->addTagButton(), entityInfo->entityTagCombo());
    viewHost_->add<ui::MaterialsPresenter>(materials->materialsList(), materials->addMaterialButton(),
                                                materials->materialNameEdit(), materials->materialColorButton(),
                                                materials->materialOpacitySpin(), materials->materialTextureCheck(),
                                                materials->materialTileWSpin(), materials->materialTileHSpin(),
                                                &session.uiState());
    viewHost_->add<ui::TitleBarPresenter>(this);
    // Owns Undo/Redo QActions' enabled state.
    viewHost_->add<ui::UndoMenuPresenter>(undoAction_, redoAction_);
    // Owns Face Style/Edge Style checked state.
    viewHost_->add<ui::StyleMenuPresenter>(faceStyleActions_, edgeFlagActions_, ambientOcclusionAction_);
    // Owns Shadows/Use Sun for Shading/Fog checked state.
    viewHost_->add<ui::ShadowsMenuPresenter>(shadowsAction_, useSunForShadingAction_, fogAction_);
    // Bound to `session` explicitly: the snapshot is taken synchronously on this kernel, so the
    // bound session is the one the save belongs to.
    DocumentSession* bound = &session;
    viewHost_->add<ui::IoPresenter>(
        this, [this, bound](const events::DocumentSnapshotReady& snapshot) { beginAsyncSave(*bound, snapshot); },
        [this, bound](const events::ObjBytesReady& obj) { beginAsyncExportObj(*bound, obj); });
}

void MainWindow::setIoExecutionPolicy(infra::ExecutionPolicy policy) {
    ioDispatcher_->setExecutionPolicy(policy);
}

bool MainWindow::beginIoOp(DocumentSession& session) {
    if (!ioBusy_.insert(&session).second) {
        focusedKernel().send(events::StatusHintChanged{"An IO operation is already running."});
        return false;
    }
    return true;
}

void MainWindow::buildIoOverlay() {
    ioDispatcher_ = new infra::IoDispatcher(this);
    progressOverlay_ = new ui::ProgressOverlay(this);

    // Connected once, before any submit (ioStarted is emitted synchronously from submitTask).
    // Open/Import are cancelable; Save/Export are not -- the snapshot is already taken and
    // the atomic rename cannot be safely abandoned halfway.
    connect(ioDispatcher_, &infra::IoDispatcher::ioStarted, this, [this](const infra::events::IoStarted& e) {
        using Kind = infra::events::IoOperationKind;
        const bool writes = e.kind == Kind::SaveDocument || e.kind == Kind::ExportObj;
        const bool cancelable = !writes;
        const QString message = e.kind == Kind::SaveDocument ? QStringLiteral("Preparing…")
                                : e.kind == Kind::ExportObj  ? QStringLiteral("Writing…")
                                                             : QStringLiteral("Reading file…");
        progressOverlay_->showOperation(e.opId, e.title, message, cancelable);
    });
    connect(ioDispatcher_, &infra::IoDispatcher::ioProgress, this, [this](const infra::events::IoProgress& e) {
        if (e.opId == progressOverlay_->currentOpId()) progressOverlay_->setProgress(e.percentage, e.statusMessage);
    });
    connect(ioDispatcher_, &infra::IoDispatcher::ioCompleted, this, [this](const infra::events::IoCompleted& e) {
        if (e.opId == progressOverlay_->currentOpId()) progressOverlay_->hideOperation();
    });
    connect(ioDispatcher_, &infra::IoDispatcher::ioFailed, this, [this](const infra::events::IoFailed& e) {
        if (e.opId == progressOverlay_->currentOpId()) progressOverlay_->hideOperation();
    });
    connect(progressOverlay_, &ui::ProgressOverlay::cancelRequested, ioDispatcher_, &infra::IoDispatcher::cancel);
}

bool MainWindow::ownsSession(const DocumentSession* session) const {
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [session](const std::unique_ptr<DocumentSession>& s) { return s.get() == session; });
}

void MainWindow::buildMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    // the reference modeler's default shortcuts; Save As uses Ctrl+Shift+S (Windows convention).
    QAction* newAction = fileMenu->addAction(QStringLiteral("New"));
    newAction->setIcon(appIcon(QStringLiteral("tb_new")));
    newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewDocument);

    QAction* openAction = fileMenu->addAction(QStringLiteral("Open..."));
    openAction->setIcon(appIcon(QStringLiteral("tb_open")));
    openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenDocument);

    QAction* importAction = fileMenu->addAction(QStringLiteral("Import..."));
    connect(importAction, &QAction::triggered, this, &MainWindow::onImportObj);

    QAction* saveAction = fileMenu->addAction(QStringLiteral("Save"));
    saveAction->setIcon(appIcon(QStringLiteral("tb_save")));
    saveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveDocument);

    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("Save As..."));
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAction, &QAction::triggered, this, [this]() { onSaveDocumentAs(); });

    // Mirrors File > Export > 3D Model...; OBJ is the only export format.
    QMenu* exportMenu = fileMenu->addMenu(QStringLiteral("Export"));
    QAction* exportObjAction = exportMenu->addAction(QStringLiteral("OBJ..."));
    connect(exportObjAction, &QAction::triggered, this, &MainWindow::onExportObj);

    fileMenu->addSeparator();
    // Routes through QWidget::close() -> closeEvent() -> confirmDiscardChanges(),
    // same gate as the window's own X button/OS close.
    QAction* exitAction = fileMenu->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));

    // Undo/Redo come first in the Edit menu and start disabled (enabled state is owned by
    // UndoMenuPresenter). Label stays plain "Undo"/"Redo".
    undoAction_ = editMenu->addAction(QStringLiteral("Undo"));
    undoAction_->setIcon(appIcon(QStringLiteral("tb_undo")));
    // the reference modeler's Windows shortcut Ctrl+Z, plus its real default Alt+Backspace.
    undoAction_->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Z")), QKeySequence(QStringLiteral("Alt+Backspace"))});
    undoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, [this]() { focusedKernel().send(events::UndoRequested{}); });

    redoAction_ = editMenu->addAction(QStringLiteral("Redo"));
    redoAction_->setIcon(appIcon(QStringLiteral("tb_redo")));
    redoAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
    redoAction_->setEnabled(false);
    connect(redoAction_, &QAction::triggered, this, [this]() { focusedKernel().send(events::RedoRequested{}); });

    editMenu->addSeparator();

    addPlaceholder(editMenu, QStringLiteral("Cut"));
    addPlaceholder(editMenu, QStringLiteral("Copy"));
    addPlaceholder(editMenu, QStringLiteral("Paste"));

    // Sends the empty DeleteSelectionRequested{}; DeleteSelectionCommand reads the live
    // selection. Safe against the VCB type-anywhere redirect (Delete never produces text()).
    QAction* deleteAction = editMenu->addAction(QStringLiteral("Delete"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [this]() { focusedKernel().send(events::DeleteSelectionRequested{}); });

    editMenu->addSeparator();

    // Select All sends SelectAllRequested; Select None is a
    // default-constructed SelectRequested{} (SelectCommand's deselect-all contract).
    QAction* selectAllAction = editMenu->addAction(QStringLiteral("Select All"));
    selectAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+A")));
    connect(selectAllAction, &QAction::triggered, this, [this]() { focusedKernel().send(events::SelectAllRequested{}); });

    QAction* selectNoneAction = editMenu->addAction(QStringLiteral("Select None"));
    selectNoneAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(selectNoneAction, &QAction::triggered, this, [this]() { focusedKernel().send(events::SelectRequested{}); });

    editMenu->addSeparator();

    // Reads the live selection from SelectionStore; empty selection is a no-op.
    QAction* hideAction = editMenu->addAction(QStringLiteral("Hide"));
    connect(hideAction, &QAction::triggered, this, [this]() {
        ordo::core::Kernel& kernel = focusedKernel();
        auto selection = kernel.agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
        if (!selection || selection->items().empty()) return;
        kernel.send(events::SetHiddenRequested{selection->items(), true});
    });

    QAction* unhideAllAction = editMenu->addAction(QStringLiteral("Unhide All"));
    connect(unhideAllAction, &QAction::triggered, this, [this]() { focusedKernel().send(events::UnhideAllRequested{}); });

    editMenu->addSeparator();

    // GroupCreateCommand reads SelectionStore itself. Explode always sends
    // instanceId 0 (no instance-selection UI yet; a documented no-op inside
    // GeometryApi::explode until one exists).
    QAction* makeGroupAction = editMenu->addAction(QStringLiteral("Make Group"));
    connect(makeGroupAction, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::GroupCreateRequested{false, ""}); });

    QAction* makeComponentAction = editMenu->addAction(QStringLiteral("Make Component"));
    connect(makeComponentAction, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::GroupCreateRequested{true, ""}); });

    QAction* explodeAction = editMenu->addAction(QStringLiteral("Explode"));
    connect(explodeAction, &QAction::triggered, this, [this]() { focusedKernel().send(events::ExplodeRequested{0}); });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    // Appearance submenu: shell visibility toggles. Created here, wired to the widgets once
    // they exist (wireAppearanceActions).
    QMenu* appearanceMenu = viewMenu->addMenu(QStringLiteral("Appearance"));
    auto addAppearanceAction = [appearanceMenu](const QString& label) {
        QAction* action = appearanceMenu->addAction(label);
        action->setCheckable(true);
        return action;
    };
    trayAction_ = addAppearanceAction(QStringLiteral("Tray"));
    panelAction_ = addAppearanceAction(QStringLiteral("Panel"));
    statusBarAction_ = addAppearanceAction(QStringLiteral("Status Bar"));
    viewMenu->addSeparator();

    addPlaceholder(viewMenu, QStringLiteral("Toolbars"));
    addPlaceholder(viewMenu, QStringLiteral("Scene Tabs"));
    viewMenu->addSeparator();

    // Face Style submenu: exclusive QActionGroup, one entry per FaceStyle in enum order.
    // ShadedWithTextures starts checked, matching StyleStore's default.
    QMenu* faceStyleMenu = viewMenu->addMenu(QStringLiteral("Face Style"));
    auto* faceStyleGroup = new QActionGroup(this);
    faceStyleGroup->setExclusive(true);

    struct FaceStyleEntry {
        QString label;
        events::FaceStyle style;
        QString iconSlug;
    };
    const FaceStyleEntry faceStyleEntries[] = {
        {QStringLiteral("Wireframe"), events::FaceStyle::Wireframe, QStringLiteral("tb_wireframe")},
        {QStringLiteral("Hidden Line"), events::FaceStyle::HiddenLine, QStringLiteral("tb_hiddenline")},
        {QStringLiteral("Shaded"), events::FaceStyle::Shaded, QStringLiteral("tb_shaded")},
        {QStringLiteral("Shaded with Textures"), events::FaceStyle::ShadedWithTextures,
         QStringLiteral("tb_textures")},
        {QStringLiteral("Monochrome"), events::FaceStyle::Monochrome, QStringLiteral("tb_monochrome")},
        {QStringLiteral("X-Ray"), events::FaceStyle::XRay, QStringLiteral("tb_xray")},
    };
    for (const FaceStyleEntry& entry : faceStyleEntries) {
        QAction* action = faceStyleMenu->addAction(entry.label);
        action->setIcon(appIcon(entry.iconSlug));
        action->setCheckable(true);
        faceStyleGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, style = entry.style]() { focusedKernel().send(events::SetFaceStyleRequested{style}); });
        if (entry.style == events::FaceStyle::ShadedWithTextures) {
            action->setChecked(true);
        }
        faceStyleActions_[entry.style] = action;
    }

    // Edge Style submenu: three independent checkable toggles. setChecked(false) is a
    // pre-presenter placeholder, overwritten by StyleMenuPresenter's refresh(). Toggling
    // persists/dirties but doesn't yet change rendering.
    QMenu* edgeStyleMenu = viewMenu->addMenu(QStringLiteral("Edge Style"));

    struct EdgeFlagEntry {
        QString label;
        events::EdgeFlag flag;
        // Empty = stays text-only (no produced icon for this entry).
        QString iconSlug;
    };
    const EdgeFlagEntry edgeFlagEntries[] = {
        {QStringLiteral("Profiles"), events::EdgeFlag::Profiles, QString()},
        {QStringLiteral("Depth Cue"), events::EdgeFlag::DepthCue, QString()},
        {QStringLiteral("Back Edges"), events::EdgeFlag::BackEdges, QStringLiteral("tb_backedges")},
    };
    for (const EdgeFlagEntry& entry : edgeFlagEntries) {
        QAction* action = edgeStyleMenu->addAction(entry.label);
        if (!entry.iconSlug.isEmpty()) {
            action->setIcon(appIcon(entry.iconSlug));
        }
        action->setCheckable(true);
        action->setChecked(false);
        connect(action, &QAction::triggered, this, [this, flag = entry.flag](bool checked) {
            focusedKernel().send(events::SetEdgeStyleFlagRequested{flag, checked});
        });
        edgeFlagActions_[entry.flag] = action;
    }

    // Drives StyleStore::ambientOcclusion(); checked-state owned by StyleMenuPresenter.
    // Default unchecked, matching StyleStore.
    QAction* ambientOcclusionQAction = viewMenu->addAction(QStringLiteral("Ambient Occlusion"));
    ambientOcclusionQAction->setCheckable(true);
    ambientOcclusionQAction->setChecked(false);
    connect(ambientOcclusionQAction, &QAction::triggered, this,
            [this](bool checked) { focusedKernel().send(events::SetAmbientOcclusionRequested{checked}); });
    ambientOcclusionAction_ = ambientOcclusionQAction;

    // Drives showShadows() (ground-plane shadow casting). No Shadows tray panel yet, so this
    // and the `shadow` bridge command are the only ways to reach it. Default unchecked.
    viewMenu->addSeparator();
    QAction* shadowsQAction = viewMenu->addAction(QStringLiteral("Shadows"));
    shadowsQAction->setIcon(appIcon(QStringLiteral("tb_shadowtoggle")));
    shadowsQAction->setCheckable(true);
    shadowsQAction->setChecked(false);
    connect(shadowsQAction, &QAction::triggered, this,
            [this](bool checked) { focusedKernel().send(events::SetShowShadowsRequested{checked}); });
    shadowsAction_ = shadowsQAction;

    // Drives N.L face shading. setChecked(false) is a pre-presenter placeholder,
    // overwritten by ShadowsMenuPresenter's refresh.
    QAction* useSunForShadingQAction = viewMenu->addAction(QStringLiteral("Use Sun for Shading"));
    useSunForShadingQAction->setCheckable(true);
    useSunForShadingQAction->setChecked(false);
    connect(useSunForShadingQAction, &QAction::triggered, this,
            [this](bool checked) { focusedKernel().send(events::SetUseSunForShadingRequested{checked}); });
    useSunForShadingAction_ = useSunForShadingQAction;

    // Default unchecked, matching FogStore.
    QAction* fogQAction = viewMenu->addAction(QStringLiteral("Fog"));
    fogQAction->setCheckable(true);
    fogQAction->setChecked(false);
    connect(fogQAction, &QAction::triggered, this,
            [this](bool checked) { focusedKernel().send(events::SetFogEnabledRequested{checked}); });
    fogAction_ = fogQAction;

    QMenu* cameraMenu = menuBar()->addMenu(QStringLiteral("&Camera"));

    // Order: Previous/Next, Standard Views, Parallel/Perspective group, Orbit/Pan/Zoom.

    // Session-only camera view history (CameraStore's two-stack). Not checkable; enabled-state
    // owned by ViewportPresenter (CameraChanged). Both start enabled; pullCamera() overwrites.
    previousAction_ = cameraMenu->addAction(QStringLiteral("Previous"));
    connect(previousAction_, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::CameraPreviousRequested{}); });

    nextAction_ = cameraMenu->addAction(QStringLiteral("Next"));
    connect(nextAction_, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::CameraNextRequested{}); });

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
                [this, view = entry.view]() { focusedKernel().send(events::SetStandardViewRequested{view}); });
    }

    cameraMenu->addSeparator();

    // Exclusive QActionGroup like faceStyleGroup. Perspective starts checked;
    // ViewportPresenter::pullCamera() overwrites it immediately.
    auto* projectionGroup = new QActionGroup(this);
    projectionGroup->setExclusive(true);

    parallelProjectionAction_ = cameraMenu->addAction(QStringLiteral("Parallel Projection"));
    parallelProjectionAction_->setCheckable(true);
    projectionGroup->addAction(parallelProjectionAction_);
    connect(parallelProjectionAction_, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::SetProjectionRequested{events::Projection::Parallel}); });

    perspectiveAction_ = cameraMenu->addAction(QStringLiteral("Perspective"));
    perspectiveAction_->setCheckable(true);
    perspectiveAction_->setChecked(true);
    projectionGroup->addAction(perspectiveAction_);
    connect(perspectiveAction_, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::SetProjectionRequested{events::Projection::Perspective}); });

    // Removes the third (vertical) vanishing point so world-vertical edges render vertical.
    // Orbiting or jumping to a Standard View exits back to Perspective; zoom/pan keep the mode.
    twoPointPerspectiveAction_ = cameraMenu->addAction(QStringLiteral("Two-Point Perspective"));
    twoPointPerspectiveAction_->setCheckable(true);
    projectionGroup->addAction(twoPointPerspectiveAction_);
    connect(twoPointPerspectiveAction_, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::SetProjectionRequested{events::Projection::TwoPoint}); });

    cameraMenu->addSeparator();
    // Orbit/Pan/Zoom are (disabled) placeholder QActions with icons only.
    // No "Zoom Extents" action exists yet.
    addPlaceholder(cameraMenu, QStringLiteral("Orbit"), QStringLiteral("tb_orbit"));
    addPlaceholder(cameraMenu, QStringLiteral("Pan"), QStringLiteral("tb_pan"));
    addPlaceholder(cameraMenu, QStringLiteral("Zoom"), QStringLiteral("tb_zoom"));

    QMenu* drawMenu = menuBar()->addMenu(QStringLiteral("&Draw"));
    addPlaceholder(drawMenu, QStringLiteral("Line"));
    addPlaceholder(drawMenu, QStringLiteral("Rectangle"));
    addPlaceholder(drawMenu, QStringLiteral("Circle"));

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    addPlaceholder(toolsMenu, QStringLiteral("Select"));
    addPlaceholder(toolsMenu, QStringLiteral("Move"));

    // -- Solid Tools -- real tool activations (ToolChanged), no toolbar/checkable presence.
    // Registered in toolActions_ so DebugBridge's `tool` command reaches all six
    // (`menu_action` only reaches Outer Shell, one menu level deep).
    QAction* outerShellAction = toolsMenu->addAction(QStringLiteral("Outer Shell"));
    connect(outerShellAction, &QAction::triggered, this,
            [this]() { focusedKernel().send(events::ToolChanged{events::ToolId::OuterShell}); });
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
                [this, tool = entry.tool]() { focusedKernel().send(events::ToolChanged{tool}); });
        toolActions_[entry.tool] = action;
    }

    QMenu* windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    // Tray section toggles, wired to the section widgets once they exist
    // (wireAppearanceActions).
    auto addSectionAction = [windowMenu](const QString& label) {
        QAction* action = windowMenu->addAction(label);
        action->setCheckable(true);
        return action;
    };
    entityInfoSectionAction_ = addSectionAction(QStringLiteral("Entity Info"));
    materialsSectionAction_ = addSectionAction(QStringLiteral("Materials"));
    tagsSectionAction_ = addSectionAction(QStringLiteral("Tags"));
    stylesSectionAction_ = addSectionAction(QStringLiteral("Styles"));

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    addPlaceholder(helpMenu, QStringLiteral("About Planura"));
}

void MainWindow::buildContextBar() {
    contextBar_ = new ui::ContextBar(this);
    contextBar_->setProjectionActions(perspectiveAction_, parallelProjectionAction_);
    addToolBar(contextBar_);
}

void MainWindow::addToolActions(ui::ActivityRail* rail) {
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    struct ToolEntry {
        QString label;
        events::ToolId tool;
        QKeySequence shortcut;
        QString iconSlug;
    };
    // the reference modeler default shortcuts; iconSlug per the inventory mapping.
    const ToolEntry entries[] = {
        {QStringLiteral("Select"), events::ToolId::Select, QKeySequence(Qt::Key_Space),
         QStringLiteral("tb_select")},
        {QStringLiteral("Line"), events::ToolId::Line, QKeySequence(Qt::Key_L), QStringLiteral("tb_line")},
        {QStringLiteral("Eraser"), events::ToolId::Eraser, QKeySequence(Qt::Key_E), QStringLiteral("tb_erase")},
        {QStringLiteral("Move"), events::ToolId::Move, QKeySequence(Qt::Key_M), QStringLiteral("tb_move")},
        {QStringLiteral("Rectangle"), events::ToolId::Rectangle, QKeySequence(Qt::Key_R),
         QStringLiteral("tb_rectangle")},
        {QStringLiteral("Rotated Rectangle"), events::ToolId::RotatedRectangle, QKeySequence(),
         QStringLiteral("tb_rectangle3point")},
        {QStringLiteral("Circle"), events::ToolId::Circle, QKeySequence(Qt::Key_C), QStringLiteral("tb_circle")},
        {QStringLiteral("Polygon"), events::ToolId::Polygon, QKeySequence(), QStringLiteral("tb_polygon")},
        {QStringLiteral("Push/Pull"), events::ToolId::PushPull, QKeySequence(Qt::Key_P),
         QStringLiteral("tb_pushpull")},
        // Note: tb_arc3point is the 2-Point Arc icon; tb_arc is the center Arc icon (crossed slugs).
        {QStringLiteral("2-Point Arc"), events::ToolId::Arc2Point, QKeySequence(Qt::Key_A),
         QStringLiteral("tb_arc3point")},
        {QStringLiteral("3-Point Arc"), events::ToolId::Arc3Point, QKeySequence(),
         QStringLiteral("tb_arc3pointfit")},
        {QStringLiteral("Arc"), events::ToolId::ArcCenter, QKeySequence(), QStringLiteral("tb_arc")},
        {QStringLiteral("Pie"), events::ToolId::Pie, QKeySequence(), QStringLiteral("tb_arc3pointpie")},
        {QStringLiteral("Freehand"), events::ToolId::Freehand, QKeySequence(), QStringLiteral("tb_freehand")},
        {QStringLiteral("Rotate"), events::ToolId::Rotate, QKeySequence(Qt::Key_Q), QStringLiteral("tb_rotate")},
        {QStringLiteral("Scale"), events::ToolId::Scale, QKeySequence(Qt::Key_S), QStringLiteral("tb_scale")},
        {QStringLiteral("Offset"), events::ToolId::Offset, QKeySequence(Qt::Key_F), QStringLiteral("tb_offset")},
        // No shortcut -- the reference modeler's Flip tool has none either.
        {QStringLiteral("Flip"), events::ToolId::Flip, QKeySequence(), QStringLiteral("tb_flip")},
        // No shortcut for Follow Me.
        {QStringLiteral("Follow Me"), events::ToolId::FollowMe, QKeySequence(), QStringLiteral("tb_followme")},
        // the reference modeler's own default Tape Measure shortcut.
        {QStringLiteral("Tape Measure"), events::ToolId::TapeMeasure, QKeySequence(Qt::Key_T),
         QStringLiteral("tb_measure")},
        // No shortcut -- the reference modeler's Protractor has none either.
        {QStringLiteral("Protractor"), events::ToolId::Protractor, QKeySequence(),
         QStringLiteral("tb_protractor")},
        // No shortcut (toolbar entry only).
        {QStringLiteral("Axes"), events::ToolId::Axes, QKeySequence(), QStringLiteral("tb_axes")},
        // No shortcut (toolbar entries only).
        {QStringLiteral("Dimension"), events::ToolId::Dimension, QKeySequence(), QStringLiteral("tb_dimension")},
        {QStringLiteral("Text"), events::ToolId::Text, QKeySequence(), QStringLiteral("tb_label")},
        // No shortcut (toolbar entry only).
        {QStringLiteral("Section Plane"), events::ToolId::SectionPlane, QKeySequence(),
         QStringLiteral("tb_section")},
        // the reference modeler's default Paint Bucket shortcut, B for Bucket.
        {QStringLiteral("Paint Bucket"), events::ToolId::PaintBucket, QKeySequence(Qt::Key_B),
         QStringLiteral("tb_paint")},
    };

    for (const ToolEntry& entry : entries) {
        auto* action = new QAction(entry.label, this);
        action->setIcon(appIcon(entry.iconSlug));
        action->setCheckable(true);
        action->setShortcut(entry.shortcut);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool = entry.tool]() { focusedKernel().send(events::ToolChanged{tool}); });
        if (entry.tool == events::ToolId::Select) {
            action->setChecked(true);
        }
        toolActions_[entry.tool] = action;
    }

    // Opens a dialog instead of switching the active tool, so it's not
    // checkable/grouped. Still registered in toolActions_ under Text3D so
    // the debug bridge's `tool` command can trigger it.
    auto* text3dAction = new QAction(QStringLiteral("3D Text"), this);
    text3dAction->setIcon(appIcon(QStringLiteral("tb_3dtext")));
    connect(text3dAction, &QAction::triggered, this, [this]() {
        // open() (non-blocking), not exec() -- exec() would hang the debug bridge.
        auto* dialog = new ui::Text3dDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QDialog::accepted, this, [this, dialog]() {
            // Fixed clear-ground origin; click-to-place needs ray/inference plumbing not yet built.
            focusedKernel().send(events::Add3dTextRequested{dialog->text().toStdString(), dialog->outlines(),
                                                      dialog->extrusion(), geo::Vec3{0.0, -6.0, 0.0}});
        });
        dialog->open();
    });
    toolActions_[events::ToolId::Text3D] = text3dAction;

    // Flyout-hidden variants have no rail button of their own, so every action
    // is window-owned to keep shortcuts global (e.g. A = 2-Point Arc while the
    // Arc slot shows Pie).
    for (const auto& [tool, action] : toolActions_) {
        addAction(action);
    }

    // Slot order: one id is a single-tool slot, several are a flyout family whose first id
    // is the initial face.
    using events::ToolId;
    auto addSlot = [this, rail](std::initializer_list<ToolId> tools) {
        QList<QAction*> actions;
        for (ToolId tool : tools) {
            actions.append(toolActions_.at(tool));
        }
        if (actions.size() == 1) {
            rail->addToolSlot(ui::ShellMode::Edit, actions.first());
        } else {
            rail->addToolFlyout(ui::ShellMode::Edit, actions);
        }
    };
    addSlot({ToolId::Select});
    addSlot({ToolId::Eraser});
    rail->addSeparator();
    addSlot({ToolId::Line, ToolId::Freehand});
    addSlot({ToolId::Arc2Point, ToolId::Arc3Point, ToolId::ArcCenter, ToolId::Pie});
    addSlot({ToolId::Rectangle, ToolId::RotatedRectangle});
    addSlot({ToolId::Circle, ToolId::Polygon});
    rail->addSeparator();
    addSlot({ToolId::PushPull});
    addSlot({ToolId::Move});
    addSlot({ToolId::Rotate});
    addSlot({ToolId::Scale, ToolId::Flip});
    addSlot({ToolId::Offset, ToolId::FollowMe});
    rail->addSeparator();
    addSlot({ToolId::TapeMeasure, ToolId::Protractor, ToolId::Axes});
    addSlot({ToolId::Dimension, ToolId::Text, ToolId::Text3D});
    rail->addSeparator();
    addSlot({ToolId::SectionPlane});
    addSlot({ToolId::PaintBucket});
}

void MainWindow::buildCentralViewport() {
    activeSession().createViewObjects();
    rootSplitter_ = new QSplitter(Qt::Horizontal, this);
    rootSplitter_->setObjectName(QStringLiteral("editorRoot"));
    rootSplitter_->setChildrenCollapsible(false);
    ui::EditorGroup* group = createGroup();
    rootSplitter_->addWidget(group);
    // The viewport is reparented into the tab's page here, once, before show().
    group->addSessionTab(activeSession(), activeSession().badge()->displayText());
    connectTabBadge(activeSession(), [this](DocumentSession* s) { return groupOf(s); });
    setFocusedGroup(group);
    refreshGroupChrome();

    // Assembled before show(): rootSplitter_ moves into topSplitter_ here, never afterwards.
    rightTray_ = new ui::RightTray();
    bottomPanel_ = new QWidget();
    bottomPanel_->setObjectName(QStringLiteral("bottomPanel"));
    // Placeholder content until a panel view exists, same empty-state look as
    // StylesView; the minimum height makes the toggled strip visibly real.
    constexpr int kPanelMinHeight = 120;  // this screen's layout constant, not a token
    bottomPanel_->setMinimumHeight(kPanelMinHeight);
    auto* panelLayout = new QVBoxLayout(bottomPanel_);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    auto* panelHint = new QLabel(QStringLiteral("No panel content yet"), bottomPanel_);
    panelHint->setAlignment(Qt::AlignCenter);
    panelHint->setStyleSheet(design::resolveRoles(
        QStringLiteral("color: {text-disabled}; font-size: %1px;").arg(design::kTypeSizePx)));
    panelLayout->addWidget(panelHint);

    constexpr int kTrayMinWidth = 180;  // clamps child-minimum leaks
    rightTray_->setMinimumWidth(kTrayMinWidth);
    // Default tray width follows the widest section row (Edit Material's Tile W/H spins)
    // plus the scroll bar, so no field clips; the user can still drag narrower, down to
    // kTrayMinWidth.
    const int trayWidth = rightTray_->widget()->minimumSizeHint().width() +
                          rightTray_->verticalScrollBar()->sizeHint().width();

    topSplitter_ = new QSplitter(Qt::Horizontal);
    topSplitter_->setObjectName(QStringLiteral("topSplitter"));
    topSplitter_->setChildrenCollapsible(false);
    topSplitter_->addWidget(rootSplitter_);
    topSplitter_->addWidget(rightTray_);
    topSplitter_->setStretchFactor(0, 1);
    topSplitter_->setStretchFactor(1, 0);

    innerSplitter_ = new QSplitter(Qt::Vertical);
    innerSplitter_->setObjectName(QStringLiteral("innerSplitter"));
    innerSplitter_->setChildrenCollapsible(false);
    innerSplitter_->addWidget(topSplitter_);
    innerSplitter_->addWidget(bottomPanel_);
    innerSplitter_->setStretchFactor(0, 1);
    innerSplitter_->setStretchFactor(1, 0);
    bottomPanel_->hide();

    // Editor takes the rest of the default 1440 px window.
    topSplitter_->setSizes({width() - trayWidth, trayWidth});
    setCentralWidget(innerSplitter_);
}

void MainWindow::wireAppearanceActions() {
    // Action -> widget: toggling shows or hides the matching shell part.
    // Widget -> action: eventFilter() re-reads visibility on every explicit show/hide.
    // Checked states persist through SettingsStore: restored here, before applyMode()
    // re-applies section visibility; written only from the toggled signal (the widget ->
    // action sync blocks signals).
    const struct {
        QAction* action;
        QWidget* widget;
        const char* key;
    } pairs[] = {
        {trayAction_, rightTray_, "view.tray"},
        {panelAction_, bottomPanel_, "view.panel"},
        {statusBarAction_, statusBar(), "view.statusBar"},
        {entityInfoSectionAction_, rightTray_->entityInfoView(), "view.tray.entityInfo"},
        {materialsSectionAction_, rightTray_->materialsView(), "view.tray.materials"},
        {tagsSectionAction_, rightTray_->tagsView(), "view.tray.tags"},
        {stylesSectionAction_, rightTray_->stylesView(), "view.tray.styles"},
    };
    for (const auto& pair : pairs) {
        QWidget* widget = pair.widget;
        const QString key = QString::fromLatin1(pair.key);
        pair.action->setChecked(!widget->isHidden());
        connect(pair.action, &QAction::toggled, widget, &QWidget::setVisible);
        if (infra::SettingsStore* store = infra::SettingsStore::activeStore()) {
            const QJsonValue saved = store->get(key);
            if (saved.isBool()) pair.action->setChecked(saved.toBool());
        }
        connect(pair.action, &QAction::toggled, this, [key](bool checked) {
            if (infra::SettingsStore* store = infra::SettingsStore::activeStore()) store->set(key, checked);
        });
        widget->installEventFilter(this);
    }
}

void MainWindow::syncAppearanceAction(QObject* widget) {
    QAction* action = widget == rightTray_                     ? trayAction_
                      : widget == bottomPanel_                 ? panelAction_
                      : widget == statusBar()                  ? statusBarAction_
                      : widget == rightTray_->entityInfoView() ? entityInfoSectionAction_
                      : widget == rightTray_->materialsView()  ? materialsSectionAction_
                      : widget == rightTray_->tagsView()       ? tagsSectionAction_
                      : widget == rightTray_->stylesView()     ? stylesSectionAction_
                                                               : nullptr;
    if (!action) return;
    if ((widget == bottomPanel_ || widget == rightTray_) && harnessLayout_) return;
    const QSignalBlocker blocker(action);
    action->setChecked(!static_cast<QWidget*>(widget)->isHidden());
}

void MainWindow::setHarnessLayout(bool on) {
    // Harness layout: chrome that would shrink the viewport (also the rail, right tray and
    // bottom panel) is hidden so pinned scenario coordinates stay valid. Groups created
    // later pick the flag up in createGroup().
    harnessLayout_ = on;
    for (ui::EditorGroup* group : groups_) {
        group->setTabStripVisible(!on);
    }
    activityRail_->setVisible(!on);
    // Restores to the Tray action's state, like the bottom panel below.
    rightTray_->setVisible(!on && trayAction_->isChecked());
    // Restores to the Panel action's state; syncAppearanceAction() ignores the
    // harness hide for this widget so that state survives it.
    bottomPanel_->setVisible(!on && panelAction_->isChecked());
}

void MainWindow::buildActivityRail() {
    activityRail_ = new ui::ActivityRail(this);
    addToolBar(Qt::LeftToolBarArea, activityRail_);
    addToolActions(activityRail_);
    connect(activityRail_, &ui::ActivityRail::modeSelected, this, &MainWindow::applyMode);
}

void MainWindow::applyMode(ui::ShellMode mode) {
    activityRail_->setMode(mode);
    contextBar_->setMode(mode);
    // Tray sections follow the Window-menu toggles; each mode defines its own panel set.
    switch (mode) {
        case ui::ShellMode::Edit:
        case ui::ShellMode::Present:
        case ui::ShellMode::Render:
            break;
    }
    const struct {
        QAction* action;
        QWidget* section;
    } sections[] = {
        {entityInfoSectionAction_, rightTray_->entityInfoView()},
        {materialsSectionAction_, rightTray_->materialsView()},
        {tagsSectionAction_, rightTray_->tagsView()},
        {stylesSectionAction_, rightTray_->stylesView()},
    };
    for (const auto& entry : sections) {
        entry.section->setVisible(entry.action->isChecked());
    }
}

void MainWindow::buildStatusBar() {
    hintLabel_ = new QLabel(QStringLiteral("Select entities."), this);

    // Default label "Length" until a tool overrides it. returnPressed commits + selects all.
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
    tools::ToolController* controller = toolController();
    return controller ? controller->activeToolId() : events::ToolId::Select;
}

QString MainWindow::statusHintText() const {
    return hintLabel_ ? hintLabel_->text() : QString();
}

QString MainWindow::entityInfoText() const {
    return rightTray_ ? rightTray_->entityInfoView()->entityInfoLabel()->text() : QString();
}

QString MainWindow::vcbLabelText() const {
    return vcbLabel_ ? vcbLabel_->text() : QString();
}

QString MainWindow::vcbValueText() const {
    return vcbEdit_ ? vcbEdit_->text() : QString();
}

void MainWindow::commitVcbEntry() {
    focusedKernel().send(events::VcbCommitted{vcbEdit_->text().toStdString()});
    vcbEdit_->selectAll();
    setVcbEntryActive(false);
}

void MainWindow::onNewDocument() {
    // A fresh session is already an empty document; no NewDocumentRequested.
    createSessionTab();
}

void MainWindow::onOpenDocument() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open"), lastDialogDir(),
                                                        QStringLiteral("Planura Model (*.plr)"));
    if (path.isEmpty()) return;  // dialog cancelled
    rememberDialogDir(path);
    DocumentSession& session = createSessionTab();
    DocumentSession* target = &session;
    if (!beginIoOp(session)) return;

    // Worker captures values only (the path); every kernel send happens in the main-thread
    // callbacks, after re-validating the target session (a tab closed mid-read drops the result).
    auto reportFailure = [this, target](const QString& error) {
        endIoOp(target);
        if (!ownsSession(target)) return;
        const std::string message = error.toStdString();
        target->kernel().send(events::DocumentIoFailed{message});
        target->kernel().send(events::StatusHintChanged{message});
    };
    ioDispatcher_->submitTask<std::shared_ptr<io::OpenPayload>>(
        infra::events::IoOperationKind::OpenDocument, path, QStringLiteral("Opening %1").arg(QFileInfo(path).fileName()),
        true,
        [path](const std::atomic<bool>& cancel, infra::IoDispatcher::ProgressCallback progress,
               QString* error) -> std::optional<std::shared_ptr<io::OpenPayload>> {
            progress(-1, QStringLiteral("Reading file…"));
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                *error = QStringLiteral("Could not open file: ") + path;
                return std::nullopt;
            }
            const QByteArray bytes = file.readAll();
            file.close();
            if (cancel.load()) return std::nullopt;

            progress(-1, QStringLiteral("Preparing…"));
            io::OpenResult prepared = io::prepareDocument(bytes);
            if (!prepared.ok) {
                *error = prepared.error;
                return std::nullopt;
            }
            return std::make_shared<io::OpenPayload>(std::move(prepared.payload));
        },
        [this, target, path](std::shared_ptr<io::OpenPayload> payload) {
            endIoOp(target);
            if (!ownsSession(target)) return;
            target->kernel().send(events::OpenDocumentDataReady{path.toStdString(), std::move(payload)});
        },
        reportFailure);
}

void MainWindow::onSaveDocument() {
    ordo::core::Kernel& kernel = focusedKernel();
    auto document = kernel.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document) return;

    if (!document->filePath().empty()) {
        // The snapshot is a synchronous value copy; IoPresenter hands it to beginAsyncSave().
        kernel.send(events::SaveSnapshotRequested{document->filePath()});
        return;
    }
    // Untitled document: Save routes through Save As.
    onSaveDocumentAs();
}

void MainWindow::beginAsyncSave(DocumentSession& session, const events::DocumentSnapshotReady& snapshot) {
    if (!snapshot.snapshot) return;
    DocumentSession* target = &session;
    const QString path = QString::fromStdString(snapshot.path);
    const std::shared_ptr<const io::SaveSnapshot> data = snapshot.snapshot;
    const std::uint64_t revision = snapshot.revision;
    if (!beginIoOp(session)) return;

    ioDispatcher_->submitAction(
        infra::events::IoOperationKind::SaveDocument, path, QStringLiteral("Saving %1").arg(QFileInfo(path).fileName()),
        true,
        [path, data](const std::atomic<bool>& /*cancel*/, infra::IoDispatcher::ProgressCallback progress,
                     QString* error) {
            // Not cancelable: QSaveFile's temp-file + atomic rename is the only abandon point and
            // it is cheap; the snapshot value is immutable, so the write is race-free.
            const QByteArray bytes = io::assembleContainer(data->doc, data->blobs);
            progress(-1, QStringLiteral("Writing…"));
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                *error = QStringLiteral("Could not open file for writing: ") + path;
                return false;
            }
            file.write(bytes);
            if (!file.commit()) {
                *error = QStringLiteral("Could not save file: ") + path;
                return false;
            }
            return true;
        },
        [this, target, path, revision]() {
            endIoOp(target);
            if (!ownsSession(target)) return;
            target->kernel().send(events::SaveCommitted{path.toStdString(), revision});
        },
        [this, target](const QString& error) {
            endIoOp(target);
            if (!ownsSession(target)) return;
            const std::string message = error.toStdString();
            target->kernel().send(events::DocumentIoFailed{message});
            target->kernel().send(events::StatusHintChanged{message});
        });
}

bool MainWindow::onSaveDocumentAs() {
    return saveSessionAs(focusedSession());
}

bool MainWindow::saveSessionAs(DocumentSession& session) {
    // Plain QFileDialog (not getSaveFileName()) so setDefaultSuffix can force ".plr".
    QFileDialog dialog(this, QStringLiteral("Save As"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("Planura Model (*.plr)"));
    dialog.setDefaultSuffix(QStringLiteral("plr"));
    if (const QString dir = lastDialogDir(); !dir.isEmpty()) dialog.setDirectory(dir);
    if (dialog.exec() != QDialog::Accepted) return false;  // dialog cancelled

    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return false;
    rememberDialogDir(selected.constFirst());

    ordo::core::Kernel& kernel = session.kernel();
    kernel.send(events::SaveDocumentRequested{selected.constFirst().toStdString()});

    // Runs synchronously: dirty() clears on success, stays set (with
    // DocumentIoFailed dispatched) on failure -- this is the success signal.
    auto document = kernel.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    return document && !document->dirty();
}

void MainWindow::onImportObj() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Import"), lastDialogDir(), QStringLiteral("OBJ (*.obj)"));
    if (path.isEmpty()) return;  // dialog cancelled
    rememberDialogDir(path);

    // Same thread boundary as onOpenDocument: the worker captures values only; sends happen in
    // main-thread callbacks after ownsSession() revalidation.
    DocumentSession* target = &focusedSession();
    if (!beginIoOp(*target)) return;
    const std::string name = QFileInfo(path).completeBaseName().toStdString();
    ioDispatcher_->submitTask<std::shared_ptr<const io::ReadObjResult>>(
        infra::events::IoOperationKind::ImportObj, path, QStringLiteral("Importing %1").arg(QFileInfo(path).fileName()),
        true,
        [path](const std::atomic<bool>& cancel, infra::IoDispatcher::ProgressCallback progress,
               QString* error) -> std::optional<std::shared_ptr<const io::ReadObjResult>> {
            progress(-1, QStringLiteral("Reading file…"));
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                *error = QStringLiteral("Could not open file: ") + path;
                return std::nullopt;
            }
            const QByteArray bytes = file.readAll();
            file.close();
            if (cancel.load()) return std::nullopt;

            progress(-1, QStringLiteral("Parsing…"));
            auto parsed = std::make_shared<io::ReadObjResult>(io::readObj(bytes));
            if (!parsed->ok) {
                *error = parsed->error;
                return std::nullopt;
            }
            return std::shared_ptr<const io::ReadObjResult>(std::move(parsed));
        },
        [this, target, path, name](std::shared_ptr<const io::ReadObjResult> parsed) {
            endIoOp(target);
            if (!ownsSession(target)) return;
            target->kernel().send(events::ImportObjDataReady{path.toStdString(), name, std::move(parsed)});
        },
        [this, target](const QString& error) {
            endIoOp(target);
            if (!ownsSession(target)) return;
            const std::string message = error.toStdString();
            target->kernel().send(events::DocumentIoFailed{message});
            target->kernel().send(events::StatusHintChanged{message});
        });
}

void MainWindow::onExportObj() {
    QFileDialog dialog(this, QStringLiteral("Export"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("OBJ (*.obj)"));
    dialog.setDefaultSuffix(QStringLiteral("obj"));
    if (const QString dir = lastDialogDir(); !dir.isEmpty()) dialog.setDirectory(dir);
    if (dialog.exec() != QDialog::Accepted) return;  // dialog cancelled

    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) return;
    rememberDialogDir(selected.constFirst());

    // The bytes are a synchronous value copy; IoPresenter hands them to beginAsyncExportObj().
    focusedKernel().send(events::ExportObjSnapshotRequested{selected.constFirst().toStdString()});
}

void MainWindow::beginAsyncExportObj(DocumentSession& session, const events::ObjBytesReady& obj) {
    if (!obj.bytes) return;
    DocumentSession* target = &session;
    const QString path = QString::fromStdString(obj.path);
    const std::shared_ptr<const QByteArray> bytes = obj.bytes;
    if (!beginIoOp(session)) return;

    ioDispatcher_->submitAction(
        infra::events::IoOperationKind::ExportObj, path, QStringLiteral("Exporting %1").arg(QFileInfo(path).fileName()),
        true,
        [path, bytes](const std::atomic<bool>& /*cancel*/, infra::IoDispatcher::ProgressCallback progress,
                      QString* error) {
            // Not cancelable, same reasoning as the .plr save write.
            progress(-1, QStringLiteral("Writing…"));
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                *error = QStringLiteral("Could not open file for writing: ") + path;
                return false;
            }
            file.write(*bytes);
            if (!file.commit()) {
                *error = QStringLiteral("Could not save file: ") + path;
                return false;
            }
            return true;
        },
        [this, target]() { endIoOp(target); },  // Export never touches document state.
        [this, target](const QString& error) {
            endIoOp(target);
            if (!ownsSession(target)) return;
            const std::string message = error.toStdString();
            target->kernel().send(events::DocumentIoFailed{message});
            target->kernel().send(events::StatusHintChanged{message});
        });
}

bool MainWindow::confirmDiscardChanges() {
    // One prompt per dirty session; Cancel (or a failed save) aborts the whole loop.
    for (const auto& session : sessions_) {
        if (!confirmDiscardSession(*session)) return false;
    }
    return true;
}

bool MainWindow::confirmDiscardSession(DocumentSession& session) {
    ordo::core::Kernel& kernel = session.kernel();
    auto document = kernel.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document || !document->dirty()) return true;  // nothing to save

    const QString name = ui::documentDisplayName(*document);

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
                kernel.send(events::SaveDocumentRequested{document->filePath()});
                return !document->dirty();  // false: the save actually failed
            }
            // Untitled: a cancelled/failed Save As aborts the whole calling action.
            return saveSessionAs(session);
        default:  // Cancel, or the dialog closed via [x]
            return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardChanges()) {
        if (infra::SettingsStore* store = infra::SettingsStore::activeStore()) store->flushSync();
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
    // Appearance toggles follow explicit show/hide of their widget.
    if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) {
        syncAppearanceAction(watched);
    }

    // A press into any group's viewport focuses that group first.
    if (event->type() == QEvent::MouseButtonPress) {
        if (DocumentSession* pressed = sessionOfViewport(watched)) {
            if (ui::EditorGroup* group = groupOf(pressed)) activateGroup(group);
        }
    }

    // A viewport press abandons a half-typed VCB entry.
    const QObject* viewport = activeSession().viewport();
    if (watched == viewport && event->type() == QEvent::MouseButtonPress) {
        setVcbEntryActive(false);
        return QMainWindow::eventFilter(watched, event);
    }

    if (watched == viewport && event->type() == QEvent::KeyPress) {
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
        // Type-anywhere: any printable keystroke with the viewport focused redirects to the
        // VCB field. Escape/arrows/Delete never produce text(); Space is excluded explicitly
        // (Select shortcut). Non-Shift modifiers block the redirect (tool meaning instead);
        // KeypadModifier doesn't, so numpad digits still redirect.
        const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
        const bool modifierOk = mods == Qt::NoModifier || mods == Qt::ShiftModifier;

        // text() for a real keypress; the raw key code only when text() is empty (the bridge's
        // synthetic `key` presses carry no text).
        std::optional<QChar> ch;
        if (text.size() == 1) {
            ch = text.at(0);
        } else if (text.isEmpty() && keyEvent->key() >= 0x20 && keyEvent->key() <= 0x7e) {
            ch = QChar(static_cast<char16_t>(keyEvent->key()));
        }

        if (modifierOk && ch && ch->isPrint() && !ch->isSpace()) {
            if (!vcbEntryActive_) {
                // First character: replaces the field's live readout. Tracked via vcbEntryActive_,
                // never focus (setFocus() is inert while the window is inactive under bridge runs).
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
