// Shell tour: state-based asserts across the v2 chrome (rail, context bar, right tray,
// session count) plus one tool activation round trip. No pixel asserts.

#include <QAction>
#include <QString>

#include "agent/events.h"
#include "harness/probe_scenarios.h"
#include "harness/probe_support.h"
#include "main_window.h"
#include "ui/activity_rail.h"
#include "ui/context_bar.h"
#include "ui/side_bar.h"

namespace {

struct ChromeState {
    bool editChecked;
    QString toolName;
    QString units;
    bool segments;
    bool trayVisible[4];
};

ChromeState readChrome(plnr::MainWindow& window, plnr::ui::ActivityRail* rail, plnr::ui::ContextBar* bar,
                       plnr::ui::RightTray* tray) {
    const QWidget* sections[] = {tray->entityInfoView(), tray->materialsView(), tray->tagsView(), tray->stylesView()};
    ChromeState s{rail->modeAction(plnr::ui::ShellMode::Edit)->isChecked(), bar->toolName(), bar->unitsText(),
                  bar->segmentsShown(), {}};
    for (int i = 0; i < 4; ++i) s.trayVisible[i] = sections[i]->isVisibleTo(&window);
    return s;
}

bool sameChrome(const ChromeState& a, const ChromeState& b) {
    for (int i = 0; i < 4; ++i) {
        if (a.trayVisible[i] != b.trayVisible[i]) return false;
    }
    return a.editChecked == b.editChecked && a.toolName == b.toolName && a.units == b.units && a.segments == b.segments;
}

}  // namespace

int runShellTourScenario(plnr::MainWindow& window) {
    using plnr::events::ToolId;
    using plnr::ui::ShellMode;
    bool ok = true;

    // Chrome widgets are window-owned children with no public accessor; look them up like the debug bridge.
    auto* rail = window.findChild<plnr::ui::ActivityRail*>();
    auto* bar = window.findChild<plnr::ui::ContextBar*>();
    auto* tray = window.findChild<plnr::ui::RightTray*>();
    ok &= probeCheck("shell.chrome-widgets-found", rail && bar && tray);
    if (!(rail && bar && tray)) return 1;

    ok &= probeCheck("shell.rail-edit-mode-checked", rail->modeAction(ShellMode::Edit)->isChecked());
    ok &= probeCheck("shell.contextbar-tool-select", bar->toolName() == QStringLiteral("Select"), bar->toolName());
    ok &= probeCheck("shell.contextbar-units-in", bar->unitsText() == QStringLiteral("Units: in"), bar->unitsText());
    ok &= probeCheck("shell.contextbar-segments-hidden", !bar->segmentsShown());

    ok &= probeCheck("shell.tray-entity-info-visible",
                     tray->entityInfoView() && tray->entityInfoView()->isVisibleTo(&window));
    ok &= probeCheck("shell.tray-materials-visible",
                     tray->materialsView() && tray->materialsView()->isVisibleTo(&window));
    ok &= probeCheck("shell.tray-tags-visible", tray->tagsView() && tray->tagsView()->isVisibleTo(&window));
    ok &= probeCheck("shell.tray-styles-visible", tray->stylesView() && tray->stylesView()->isVisibleTo(&window));
    ok &= probeCheck("shell.single-session", window.sessionCount() == 1, QString::number(window.sessionCount()));

    // applyMode is private; re-triggering the Edit mode action reaches it via the rail's modeSelected.
    const ChromeState before = readChrome(window, rail, bar, tray);
    rail->modeAction(ShellMode::Edit)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("shell.apply-mode-edit-idempotent", sameChrome(before, readChrome(window, rail, bar, tray)));

    // Circle reports a segment count through ToolSegmentsChanged, which becomes the Sides spinner.
    window.toolAction(ToolId::Circle)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("shell.circle-active", window.activeTool() == ToolId::Circle);
    ok &= probeCheck("shell.contextbar-tool-circle", bar->toolName() == QStringLiteral("Circle"), bar->toolName());
    ok &= probeCheck("shell.contextbar-segments-shown-for-circle", bar->segmentsShown());

    window.toolAction(ToolId::Select)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("shell.select-active", window.activeTool() == ToolId::Select);
    ok &= probeCheck("shell.contextbar-segments-hidden-after-select", !bar->segmentsShown());

    return ok ? 0 : 1;
}
