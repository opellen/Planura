#pragma once

// Declarations for the in-process --gui-probe harness; app types are forward-declared.

#include <QStringList>

namespace plnr {
class MainWindow;
}

// Runs the ordered probe scenarios against `window`; returns the exit code (0 iff none failed).
// A non-empty `scenarioFilter` (case-insensitive name substring) makes a partial run, not a gate.
int runGuiProbe(plnr::MainWindow& window, const QStringList& scenarioFilter = {});
