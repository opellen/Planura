#include "ui/activity_rail.h"

#include <QAction>
#include <QActionGroup>
#include <QSize>

#include "ui/icons.h"
#include "ui/tool_flyout.h"

namespace plnr::ui {

namespace {
// Multi-color rail icons carry internal margins, so 24 px reads small next to a
// monochrome rail; 32 is the next grammar size.
constexpr int kRailIconPx = 32;
}

ActivityRail::ActivityRail(QWidget* parent) : QToolBar(QStringLiteral("Activity Rail"), parent) {
    setObjectName(QStringLiteral("activityRail"));
    setOrientation(Qt::Vertical);
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(kRailIconPx, kRailIconPx));
    setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Present and Render are disabled placeholders and own no tool slots; only enabled
    // modes can fire modeSelected.
    modeGroup_ = new QActionGroup(this);
    modeGroup_->setExclusive(true);
    struct ModeEntry {
        const char* slug;
        const char* label;
        bool live;
    };
    const ModeEntry modes[] = {
        {"mode_edit", "Edit", true},
        {"mode_present", "Present", false},
        {"mode_render", "Render", false},
    };
    int index = 0;
    for (const ModeEntry& mode : modes) {
        QAction* action = addAction(QString::fromLatin1(mode.label));
        modeActions_[index++] = action;
        action->setIcon(icons::icon(QString::fromLatin1(mode.slug)));
        action->setToolTip(QString::fromLatin1(mode.label));
        action->setCheckable(true);
        action->setEnabled(mode.live);
        action->setChecked(mode.live);
        modeGroup_->addAction(action);
    }
    modeSeparator_ = addSeparator();
    connect(modeGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        for (int i = 0; i < 3; ++i) {
            if (modeActions_[i] == action) emit modeSelected(static_cast<ShellMode>(i));
        }
    });
}

void ActivityRail::addToolSlot(ShellMode mode, QAction* action) {
    addAction(action);
    toolSlots_.emplace_back(mode, action);
}

void ActivityRail::addToolFlyout(ShellMode mode, const QList<QAction*>& variants) {
    auto* button = new ToolFlyoutButton(variants, this);
    button->setIconSize(iconSize());
    connect(this, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
    toolSlots_.emplace_back(mode, addWidget(button));
}

void ActivityRail::setMode(ShellMode mode) {
    modeActions_[static_cast<int>(mode)]->setChecked(true);
    for (const auto& [owner, action] : toolSlots_) {
        action->setVisible(owner == mode);
    }
    refreshToolSeparators();
}

QAction* ActivityRail::modeAction(ShellMode mode) const {
    return modeActions_[static_cast<int>(mode)];
}

void ActivityRail::refreshToolSeparators() {
    const QList<QAction*> all = actions();
    const auto isSlot = [this](QAction* action) {
        for (const auto& slot : toolSlots_) {
            if (slot.second == action) return true;
        }
        return false;
    };
    bool pastModeSection = false;
    bool slotBefore = false;
    for (int i = 0; i < all.size(); ++i) {
        QAction* action = all[i];
        if (action == modeSeparator_) {
            pastModeSection = true;
            continue;
        }
        if (!pastModeSection) continue;
        if (isSlot(action)) {
            slotBefore = slotBefore || action->isVisible();
        } else if (action->isSeparator()) {
            bool slotAfter = false;
            for (int j = i + 1; j < all.size() && !slotAfter; ++j) {
                slotAfter = isSlot(all[j]) && all[j]->isVisible();
            }
            action->setVisible(slotBefore && slotAfter);
        }
    }
}

}  // namespace plnr::ui
