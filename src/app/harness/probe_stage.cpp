// The --gui-probe stage: runGuiProbe(), the single ordered scenario call list.

#include "harness/harness.h"

#include <QString>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "harness/probe_scenarios.h"
#include "main_window.h"

int runGuiProbe(plnr::MainWindow& window, const QStringList& scenarioFilter) {
    int exitCode = 0;
    int ran = 0;
    QStringList skipped;
    // --gui-probe-only runs only scenarios whose name contains a filter term; a filtered run is not a gate.
    auto runSelected = [&](const char* name, const std::function<int()>& scenario) {
        const QString scenarioName = QString::fromLatin1(name);
        const bool selected =
            scenarioFilter.isEmpty() || std::any_of(scenarioFilter.begin(), scenarioFilter.end(), [&](const QString& term) {
                return scenarioName.contains(term, Qt::CaseInsensitive);
            });
        if (!selected) {
            skipped << scenarioName;
            return;
        }
        ++ran;
        const int rc = scenario();
        if (rc != 0) std::fprintf(stderr, "FAIL: scenario %s returned %d\n", name, rc);
        exitCode |= rc;
    };

    // Order is load-bearing: scenarios share one window and document, each starting from the previous end state.
    runSelected("runShellTourScenario", [&] { return runShellTourScenario(window); });
    runSelected("runLeverParityScenario", [&] { return runLeverParityScenario(window); });
    runSelected("runFollowMePreselectScenario", [&] { return runFollowMePreselectScenario(window); });
    runSelected("runFollowMeDragScenario", [&] { return runFollowMeDragScenario(window); });

    if (!scenarioFilter.isEmpty()) {
        std::fprintf(stderr, "==== PARTIAL RUN -- NOT a verification gate (filter: %s; skipped: %s) ====\n",
                     scenarioFilter.join(QLatin1Char(',')).toLocal8Bit().constData(),
                     skipped.isEmpty() ? "none" : skipped.join(QLatin1Char(',')).toLocal8Bit().constData());
    }
    if (ran == 0) {
        std::fprintf(stderr, "FAIL: no scenario matched the filter\n");
        exitCode |= 1;
    }
    std::printf("gui-probe: %d scenario(s) run, %s\n", ran, exitCode == 0 ? "all passed" : "FAILURES");
    std::fflush(stdout);
    return exitCode == 0 ? 0 : 1;
}
