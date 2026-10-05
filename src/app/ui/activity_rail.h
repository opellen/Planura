#pragma once

#include <utility>
#include <vector>

#include <QList>
#include <QToolBar>

class QAction;
class QActionGroup;

namespace plnr::ui {

// Shell chrome mode: which tool section, context-bar cluster and tray panel set is
// on screen. Not kernel state.
enum class ShellMode { Edit, Present, Render };

// Icon-only left rail (objectName "activityRail"): a mode section (Edit,
// Present, Render), a separator, then the tool slots of the current mode. The
// rail owns only the mode actions; tool QActions stay owned by the caller.
class ActivityRail : public QToolBar {
    Q_OBJECT

public:
    explicit ActivityRail(QWidget* parent = nullptr);

    // Single-tool slot of `mode`: the action's own button.
    void addToolSlot(ShellMode mode, QAction* action);

    // Family slot of `mode` showing one variant at a time; see ToolFlyoutButton.
    void addToolFlyout(ShellMode mode, const QList<QAction*>& variants);

    // Checks the mode action and swaps the visible tool section to `mode`'s
    // slots; does not emit modeSelected.
    void setMode(ShellMode mode);

    // The mode section's QAction for `mode`.
    QAction* modeAction(ShellMode mode) const;

signals:
    // An enabled mode action was triggered by the user.
    void modeSelected(ShellMode mode);

private:
    // Hides tool-section separators that no longer sit between two visible slots.
    void refreshToolSeparators();

    QActionGroup* modeGroup_;
    QAction* modeActions_[3] = {};
    QAction* modeSeparator_ = nullptr;
    // Slot QActions as registered (addAction / addWidget return) with their mode.
    std::vector<std::pair<ShellMode, QAction*>> toolSlots_;
};

}  // namespace plnr::ui
