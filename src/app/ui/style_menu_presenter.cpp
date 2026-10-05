#include "ui/style_menu_presenter.h"

#include <QAction>

#include "agent/style_store.h"

namespace plnr::ui {

StyleMenuPresenter::StyleMenuPresenter(const std::unordered_map<events::FaceStyle, QAction*>& faceStyleActions,
                                        const std::unordered_map<events::EdgeFlag, QAction*>& edgeFlagActions,
                                        QAction* ambientOcclusionAction)
    : Presenter(QStringLiteral("StyleMenuPresenter")),
      faceStyleActions_(faceStyleActions),
      edgeFlagActions_(edgeFlagActions),
      ambientOcclusionAction_(ambientOcclusionAction) {}

void StyleMenuPresenter::onRegister() {
    subscribe<events::StyleChanged>(&StyleMenuPresenter::onStyleChanged);
    refresh();
}

void StyleMenuPresenter::onStyleChanged(const events::StyleChanged& /*event*/) {
    refresh();
}

void StyleMenuPresenter::refresh() {
    auto style = context().agentAs<agent::StyleStore>(agent::kStyleStoreName);
    if (!style) return;  // No StyleStore registered -- leave whatever checked state was already set.

    for (const auto& [faceStyle, action] : faceStyleActions_) {
        if (action) action->setChecked(faceStyle == style->faceStyle());
    }

    if (auto it = edgeFlagActions_.find(events::EdgeFlag::Profiles); it != edgeFlagActions_.end() && it->second) {
        it->second->setChecked(style->profiles());
    }
    if (auto it = edgeFlagActions_.find(events::EdgeFlag::DepthCue); it != edgeFlagActions_.end() && it->second) {
        it->second->setChecked(style->depthCue());
    }
    if (auto it = edgeFlagActions_.find(events::EdgeFlag::BackEdges); it != edgeFlagActions_.end() && it->second) {
        it->second->setChecked(style->backEdges());
    }

    if (ambientOcclusionAction_) {
        ambientOcclusionAction_->setChecked(style->ambientOcclusion());
    }
}

}  // namespace plnr::ui
