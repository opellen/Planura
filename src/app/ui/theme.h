#pragma once

#include <QString>

class QApplication;

namespace plnr::ui::theme {

// App-wide light theme: Fusion style, a QPalette (covers widgets no stylesheet rule names, e.g.
// dialogs and combo popups) and one application stylesheet. Call once from main() before any
// widget exists; per-widget stylesheets still win.
void apply(QApplication& app);

// The stylesheet half of apply(); usable without a QApplication.
QString appStyleSheet();

}  // namespace plnr::ui::theme
