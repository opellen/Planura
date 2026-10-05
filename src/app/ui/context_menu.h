#pragma once

// Minimal right-click context menu. buildContextMenu() is a pure function (no Qt
// construction) shared by the QMenu path and DebugBridge's `context_menu` command
// (QMenu::exec() would block the event loop). Every item sends one *Requested event,
// except Divide/Position Texture, which arm ToolController's own interposed mode.

#include <functional>
#include <string>
#include <vector>

#include <geo/entity.h>

#include <ordo/core/presenter_context.h>

#include "tools/tool.h"

namespace plnr::ui {

// One built menu entry. checkable/checked is only used by the section
// plane's "Active Cut" item; action is never null.
struct ContextMenuItem {
    std::string label;
    bool checkable = false;
    bool checked = false;
    std::function<void()> action;
};

// Hit-tests in priority order, first match wins: multi-instance selection >
// edge > guide > section plane > axes > textured face interior > empty.
std::vector<ContextMenuItem> buildContextMenu(tools::ToolContext& ctx, ordo::core::PresenterContext& context,
                                               const tools::PointerEvent& e,
                                               const std::function<void(geo::Id)>& startDivide,
                                               const std::function<void(geo::Id)>& startPositionTexture);

}  // namespace plnr::ui
