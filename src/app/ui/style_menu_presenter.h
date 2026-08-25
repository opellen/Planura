#pragma once

#include <unordered_map>

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QAction;

namespace plnr::ui {

// Mirrors StyleStore's active face style + edge flags onto the View > Face
// Style / Edge Style submenus' checked state, on events::StyleChanged.
class StyleMenuPresenter : public ordo::qt::Presenter {
public:
    // ambientOcclusionAction is StyleStore's own field, so it lives here
    // rather than ShadowsMenuPresenter. All QActions nullptr-safe.
    StyleMenuPresenter(ordo::core::AppKernel& kernel, const std::unordered_map<events::FaceStyle, QAction*>& faceStyleActions,
                        const std::unordered_map<events::EdgeFlag, QAction*>& edgeFlagActions,
                        QAction* ambientOcclusionAction);

    void onRegister() override;

private:
    void onStyleChanged(const events::StyleChanged& event);
    void refresh();

    // Non-owning; built by MainWindow::buildMenuBar().
    std::unordered_map<events::FaceStyle, QAction*> faceStyleActions_;
    std::unordered_map<events::EdgeFlag, QAction*> edgeFlagActions_;
    QAction* ambientOcclusionAction_ = nullptr;
};

}  // namespace plnr::ui
