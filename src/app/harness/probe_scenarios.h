#pragma once

// Probe scenario entry points; each returns 0 on success, nonzero if a probeCheck failed.

namespace plnr {
class MainWindow;
}

int runShellTourScenario(plnr::MainWindow& window);
int runLeverParityScenario(plnr::MainWindow& window);
int runFollowMePreselectScenario(plnr::MainWindow& window);
int runFollowMeDragScenario(plnr::MainWindow& window);
