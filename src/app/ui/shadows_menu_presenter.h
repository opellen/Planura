#pragma once

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QAction;

namespace plnr::ui {

// Mirrors ShadowStore's showShadows/useSunForShading and FogStore's enabled
// flag onto their View-menu QActions' checked state, on ShadowsChanged/FogChanged.
class ShadowsMenuPresenter : public ordo::qt::Presenter {
public:
    ShadowsMenuPresenter(QAction* shadowsAction, QAction* useSunForShadingAction,
                          QAction* fogAction);

    void onRegister() override;

private:
    void onShadowsChanged(const events::ShadowsChanged& event);
    void onFogChanged(const events::FogChanged& event);
    void refreshShadows();
    void refreshFog();

    // Non-owning; built by MainWindow::buildMenuBar().
    QAction* shadowsAction_;
    QAction* useSunForShadingAction_;
    QAction* fogAction_;
};

}  // namespace plnr::ui
