#include "ui/shadows_menu_presenter.h"

#include <QAction>

#include "agent/fog_store.h"
#include "agent/shadow_store.h"

namespace plnr::ui {

ShadowsMenuPresenter::ShadowsMenuPresenter(QAction* shadowsAction,
                                            QAction* useSunForShadingAction, QAction* fogAction)
    : Presenter(QStringLiteral("ShadowsMenuPresenter")),
      shadowsAction_(shadowsAction),
      useSunForShadingAction_(useSunForShadingAction),
      fogAction_(fogAction) {}

void ShadowsMenuPresenter::onRegister() {
    subscribe<events::ShadowsChanged>(&ShadowsMenuPresenter::onShadowsChanged);
    subscribe<events::FogChanged>(&ShadowsMenuPresenter::onFogChanged);
    refreshShadows();
    refreshFog();
}

void ShadowsMenuPresenter::onShadowsChanged(const events::ShadowsChanged& /*event*/) {
    refreshShadows();
}

void ShadowsMenuPresenter::onFogChanged(const events::FogChanged& /*event*/) {
    refreshFog();
}

void ShadowsMenuPresenter::refreshShadows() {
    auto shadow = context().agentAs<agent::ShadowStore>(agent::kShadowStoreName);
    if (!shadow) return;  // No ShadowStore registered -- leave whatever checked state was already set.

    if (shadowsAction_) shadowsAction_->setChecked(shadow->showShadows());
    if (useSunForShadingAction_) useSunForShadingAction_->setChecked(shadow->useSunForShading());
}

void ShadowsMenuPresenter::refreshFog() {
    auto fog = context().agentAs<agent::FogStore>(agent::kFogStoreName);
    if (!fog) return;  // No FogStore registered -- leave whatever checked state was already set.

    if (fogAction_) fogAction_->setChecked(fog->enabled());
}

}  // namespace plnr::ui
