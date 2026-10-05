// Application entry point: builds the industry-standard main window shell
// (which owns the document session and its Ordo kernel), then hands control to the Qt event loop.

#include <cstring>
#include <memory>

#include <QApplication>
#include <QByteArray>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

#include "devbridge/debug_bridge.h"
#include "agent/events.h"
#include "agent/geometry_api.h"
#include "document_session.h"
#include "harness/harness.h"
#include "infra/io_dispatcher.h"
#include "infra/settings_store.h"
#include "main_window.h"
#include "ui/theme.h"
#include "viewport/viewport_widget.h"

namespace {

// `--screenshot <path>`: grabs the viewport framebuffer shortly after show() and exits.
QString screenshotPathFromArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Simple argv boolean-flag check.
bool hasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// `--debug-bridge [port]`: starts the in-app TCP JSON debug bridge. Port
// resolution order: explicit arg -> PLNR_BRIDGE_PORT env var -> kDefaultPort.
// Returns 0 (no bridge) when the flag is absent.
quint16 debugBridgePortFromArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug-bridge") == 0) {
            if (i + 1 < argc) {
                bool ok = false;
                const int port = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
                if (ok && port > 0 && port <= 65535) {
                    return static_cast<quint16>(port);
                }
            }
            const int envPort = qEnvironmentVariableIntValue("PLNR_BRIDGE_PORT");
            if (envPort > 0 && envPort <= 65535) {
                return static_cast<quint16>(envPort);
            }
            return plnr::devbridge::DebugBridge::kDefaultPort;
        }
    }
    return 0;
}

// `--gui-probe-only a,b`: comma-separated scenario-name terms (next argv).
QStringList probeFilterFromArgs(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--gui-probe-only") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]).split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
    }
    return {};
}

}  // namespace

int main(int argc, char* argv[]) {
    // The probe runs offscreen unless --gui-probe-visible is given. QT_QPA_PLATFORM is read at
    // QApplication construction, so this raw argv scan must precede it.
    if (hasFlag(argc, argv, "--gui-probe") && !hasFlag(argc, argv, "--gui-probe-visible")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication app(argc, argv);
    // App window icon: the small variant (the full logo's dash ring dies at 16px).
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/planura_logo_small.svg")));
    // Offscreen on Windows loads no system fonts (text renders as boxes), so load Segoe UI
    // explicitly. Runs before theme::apply, which only adjusts the existing font's pixel size.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        bool loaded = false;
        for (const char* file : {"segoeui.ttf", "segoeuib.ttf", "segoeuil.ttf", "seguisb.ttf"}) {
            const QString path = QStringLiteral("C:/Windows/Fonts/") + QLatin1String(file);
            if (QFileInfo::exists(path) && QFontDatabase::addApplicationFont(path) >= 0) loaded = true;
        }
        if (loaded) {
            QFont f = app.font();
            f.setFamily(QStringLiteral("Segoe UI"));
            app.setFont(f);
        }
    }
    // Fusion + token palette + app stylesheet.
    plnr::ui::theme::apply(app);

    // Agent-driven runs (bridge, harness layout, --gui-probe) must not read the user's settings
    // file or run IO on worker threads: persisted chrome and worker timing would vary scenario
    // results across machines. They keep compiled-default chrome and DirectSynchronous IO;
    // PLNR_IO_ASYNC=1 forces the real Async policy for smokes of the threaded path.
    const quint16 debugBridgePort = debugBridgePortFromArgs(argc, argv);
    const bool guiProbe = hasFlag(argc, argv, "--gui-probe");
    const bool agentRun = debugBridgePort != 0 || hasFlag(argc, argv, "--harness-layout") || guiProbe;

    // Settings load before the window so its constructor can restore view state.
    // No workspace path: documents are files, not workspaces.
    // The store always exists (stack lifetime); agent runs just never activate it.
    plnr::infra::SettingsStore settings;
    if (!agentRun) {
        settings.loadUser();
        plnr::infra::SettingsStore::setActiveStore(&settings);
    }

    plnr::MainWindow window;
    window.setIoExecutionPolicy(agentRun && qEnvironmentVariable("PLNR_IO_ASYNC") != QLatin1String("1")
                                    ? plnr::infra::ExecutionPolicy::DirectSynchronous
                                    : plnr::infra::ExecutionPolicy::Async);

    // Commands register after MainWindow so presenters exist first.
    window.activeSession().registerCommands();

    // Constructed after commands are registered (so injected input can
    // round-trip immediately) and before show().
    std::unique_ptr<plnr::devbridge::DebugBridge> debugBridge;
    if (debugBridgePort != 0) {
        debugBridge = std::make_unique<plnr::devbridge::DebugBridge>(&window, debugBridgePort);
    }
    // --harness-layout: hides the shell chrome so the viewport keeps its pre-tab size for
    // pinned-coordinate runs. Independent of --debug-bridge, which only makes the instance inspectable.
    if (hasFlag(argc, argv, "--harness-layout")) {
        window.setHarnessLayout(true);
    }

    // --gui-probe: drive the scenarios in-process and exit with their verdict; no bridge,
    // no event loop. `--gui-probe-only a,b` filters scenarios by name substring.
    if (guiProbe) {
        if (hasFlag(argc, argv, "--gui-probe-visible")) window.show();
        // A never-shown offscreen run tears down cleanly: ~ViewportWidget
        // no-ops when no GL context was ever created.
        return runGuiProbe(window, probeFilterFromArgs(argc, argv));
    }

    // --start-minimized: agent-driven test runs; neither bridge injection
    // nor grabFramebuffer() needs the window visible.
    if (hasFlag(argc, argv, "--start-minimized")) {
        window.showMinimized();
    } else {
        window.show();
    }

    const bool demoGeometry = hasFlag(argc, argv, "--demo-geometry");
    const bool demoExtrude = hasFlag(argc, argv, "--demo-extrude");
    if (demoGeometry || demoExtrude) {
        // 4x3 rectangle on the ground plane; the closing edge completes a face.
        window.activeSession().kernel().send(plnr::events::AddEdgeRequested{{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}});
        window.activeSession().kernel().send(plnr::events::AddEdgeRequested{{4.0, 0.0, 0.0}, {4.0, 3.0, 0.0}});
        window.activeSession().kernel().send(plnr::events::AddEdgeRequested{{4.0, 3.0, 0.0}, {0.0, 3.0, 0.0}});
        window.activeSession().kernel().send(plnr::events::AddEdgeRequested{{0.0, 3.0, 0.0}, {0.0, 0.0, 0.0}});
    }

    if (demoExtrude) {
        // Push the demo rectangle's face into a box (ExtrudeFaceRequested).
        auto agent = window.activeSession().kernel().agentAs<plnr::agent::GeometryApi>(plnr::agent::kGeometryApiName);
        if (agent && !agent->model().faces().empty()) {
            const plnr::geo::Id faceId = agent->model().faces().begin()->first;
            window.activeSession().kernel().send(plnr::events::ExtrudeFaceRequested{faceId, 2.0});
        }
    }

    const QString screenshotPath = screenshotPathFromArgs(argc, argv);
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &window, [&window, screenshotPath]() {
            window.viewportWidget()->grabFramebuffer().save(screenshotPath);
            QApplication::quit();
        });
    }

    return app.exec();
}
