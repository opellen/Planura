#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <geo/model.h>
#include <ordo/core/kernel.h>
#include <ordo/qt/view_host.h>

namespace plnr::viewport {
class ViewportWidget;
}  // namespace plnr::viewport

namespace plnr::tools {
class ToolController;
}  // namespace plnr::tools

namespace plnr::ui {
class TabBadgeAdapter;
}  // namespace plnr::ui

namespace plnr {

// UI state that can't be rebuilt from kernel events; survives presenter rebind.
struct UiSessionState {
    // Last browsed texture path per material id (MaterialsPresenter).
    std::unordered_map<geo::Id, std::string> materialTexturePaths;
};

// One open document: owns the Ordo kernel and registers its Agents on
// construction. Commands are registered later, after the presenters exist.
class DocumentSession {
public:
    DocumentSession();
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    ordo::core::Kernel& kernel() { return kernel_; }

    // Creates the parentless viewport widget (needs a live QApplication, so not
    // in the ctor). The caller reparents it once, before show().
    void createViewObjects();

    // Tab move: drops the tool host and the stale viewport, builds a fresh one.
    // Caller resets the tool host before the old viewport is destroyed, re-adds
    // the new viewport to a group, reinstalls its event filter, and rebinds the
    // presenters before createToolHost() runs again.
    void recreateViewObjects();

    // Builds the session ViewHost and adds the ToolController. Call after
    // createViewObjects(); the call point fixes ToolController's subscription
    // order relative to the presenters. Idempotent.
    void createToolHost();

    // Destroys the tool host; the next createToolHost() builds a fresh one. A focus
    // switch uses this so rebuilt presenters subscribe to ToolChanged before the controller.
    void resetToolHost();

    UiSessionState& uiState() { return uiState_; }

    // Non-owning; the viewport is owned by the Qt parent chain after reparenting.
    viewport::ViewportWidget* viewport() const { return viewport_; }
    // Non-owning; owned by sessionHost_.
    tools::ToolController* toolController() const { return toolController_; }
    // Non-owning; owned by badgeHost_, so it lives as long as the session.
    ui::TabBadgeAdapter* badge() const { return badge_; }

    // Registers every Intent/Fact Command. Call after the presenters are
    // constructed, before show().
    void registerCommands();

private:
    // Presenters and widgets elsewhere reference this kernel, so the owner
    // must destroy them before the session.
    ordo::core::Kernel kernel_;
    UiSessionState uiState_;
    viewport::ViewportWidget* viewport_ = nullptr;
    // Declared after kernel_ so it is destroyed first.
    std::unique_ptr<ordo::qt::ViewHost> sessionHost_;
    tools::ToolController* toolController_ = nullptr;
    // Separate from sessionHost_: built once in the ctor and untouched by
    // resetToolHost()/recreateViewObjects(), so the tab badge never goes stale
    // across a rebind.
    std::unique_ptr<ordo::qt::ViewHost> badgeHost_;
    ui::TabBadgeAdapter* badge_ = nullptr;
};

}  // namespace plnr
