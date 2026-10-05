#pragma once

#include <functional>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QAction;

namespace plnr::ui {

class ContextBar;

// Mirrors ToolChanged (tool face, spinner hidden until the tool reports) and
// ToolSegmentsChanged (spinner value + the bar's per-tool cache) onto the
// ContextBar, and turns spinner edits into ToolSegmentsRequested. Given only
// the bar and a ToolId -> QAction resolver for the face.
class ContextBarPresenter : public ordo::qt::Presenter {
public:
    using ActionResolver = std::function<QAction*(events::ToolId)>;

    ContextBarPresenter(ContextBar* bar, ActionResolver resolveAction);

    void onRegister() override;

private:
    void onToolChanged(const events::ToolChanged& event);
    void onToolSegmentsChanged(const events::ToolSegmentsChanged& event);

    ContextBar* bar_;
    ActionResolver resolveAction_;
    events::ToolId activeTool_ = events::ToolId::Select;
};

}  // namespace plnr::ui
