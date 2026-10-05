#pragma once

// In-app TCP JSON debug bridge: synthetic input + state queries over a newline-delimited
// localhost protocol. Mutations use the real input path except documented kernel().send cases.

#include <deque>
#include <string>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QtGlobal>

#include <ordo/core/kernel.h>
#include <ordo/core/dispatcher.h>

class QByteArray;
class QJsonObject;
class QTcpServer;
class QTcpSocket;

namespace plnr {
class MainWindow;
}  // namespace plnr

namespace plnr::devbridge {

class DebugBridge : public QObject {
    Q_OBJECT

public:
    // Default port when neither --debug-bridge nor PLNR_BRIDGE_PORT is set; keep in sync with the Python side.
    static constexpr quint16 kDefaultPort = 45454;

    // Listens on 127.0.0.1:port immediately. window is non-owning and must outlive this object.
    DebugBridge(MainWindow* window, quint16 port, QObject* parent = nullptr);
    ~DebugBridge() override;

    bool isListening() const;

    // Moves the dispatcher trace observer to the now-focused session's kernel.
    void onFocusedSessionChanged();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    // Dispatches one request line via execute(); always writes exactly one response line, even on a parse failure or thrown command.
    void handleLine(QTcpSocket* socket, const QByteArray& line);

    // Returns the command-specific fields of the {"ok":true,...} reply; throws CommandError on a recognized failure.
    QJsonObject execute(const QString& cmd, const QJsonObject& req);

    // Query commands: read state, never mutate.
    QJsonObject cmdPing() const;
    QJsonObject cmdScene() const;
    QJsonObject cmdCamera() const;
    // Sets a subset of the camera pose via CameraStore::restoreCamera.
    QJsonObject cmdCameraSet(const QJsonObject& req);
    QJsonObject cmdStatus() const;
    QJsonObject cmdSelection() const;
    QJsonObject cmdEditContext() const;
    QJsonObject cmdEntityInfo() const;
    // Read-only VCB (Measurements Box) query. No command yet types into/commits the field.
    QJsonObject cmdVcb() const;
    // Read-only context-bar query.
    QJsonObject cmdContextBar() const;
    QJsonObject cmdOverlayStats() const;
    QJsonObject cmdTags() const;
    QJsonObject cmdPick(const QJsonObject& req) const;
    QJsonObject cmdInfer(const QJsonObject& req) const;
    QJsonObject cmdProject(const QJsonObject& req) const;
    QJsonObject cmdScreenshot(const QJsonObject& req) const;
    QJsonObject cmdEvents(const QJsonObject& req) const;

    // Chrome-capture aids.
    QJsonObject cmdWindowGrab(const QJsonObject& req) const;
    QJsonObject cmdHarnessLayout(const QJsonObject& req);
    QJsonObject cmdRailSelect(const QJsonObject& req);

    // Shared by cmdDoc's "state" query and its mutating actions.
    QJsonObject docStateJson() const;

    // Shared by cmdStyle's "state" query and its mutating actions.
    QJsonObject styleStateJson() const;

    // Shared by cmdShadow's "state" query and its mutating actions.
    QJsonObject shadowStateJson() const;

    // Shared by cmdFog's "state" query and its mutating actions.
    QJsonObject fogStateJson() const;

    // Injection commands: post synthesized Qt input events to the viewport, as real input does.
    QJsonObject cmdMouse(const QString& cmd, const QJsonObject& req);
    QJsonObject cmdKey(const QJsonObject& req);
    QJsonObject cmdTool(const QJsonObject& req);

    // Triggers the QAction under menu `menu` matching `text`, for menu-only commands.
    QJsonObject cmdMenuAction(const QJsonObject& req);

    // Scripts ToolController's confirm/prompt/warning dialogs so a run never opens a real dialog.
    QJsonObject cmdModal(const QJsonObject& req);

    // Drives ToolController::buildContextMenuItems directly; never opens a real QMenu.
    QJsonObject cmdContextMenu(const QJsonObject& req);

    // Bypasses confirmDiscardChanges() and the Open/Save QFileDialogs on purpose.
    QJsonObject cmdDoc(const QJsonObject& req);

    // kernel().send exception: TagsPresenter's Tray-dock widgets carry no QAction.
    QJsonObject cmdTag(const QJsonObject& req);

    // kernel().send exception: MaterialsPresenter's Tray-dock widgets carry no QAction.
    QJsonObject cmdMaterial(const QJsonObject& req);

    // QAction::trigger() path (View menu), except set_ao_strength (no tray panel yet).
    QJsonObject cmdStyle(const QJsonObject& req);

    // Hybrid of QAction triggers and kernel().send exceptions.
    QJsonObject cmdShadow(const QJsonObject& req);

    // Hybrid like cmdShadow.
    QJsonObject cmdFog(const QJsonObject& req);

    // Read-only solid-classification query, or (action:"apply") a kernel().send Solid Tools op. The one cmd* that is not const.
    QJsonObject cmdSolid(const QJsonObject& req);

    // Dispatcher trace observer: appends to traceLog_, dropping the oldest past kTraceLogCapacity.
    void onDispatchTrace(const ordo::core::DispatchRecord& record);

    // Session commands: `session_list`, `session_focus`, `session_new_tab`,
    // `session_close`, `session_split`, `session_move`.
    QJsonObject cmdSessionList() const;
    QJsonObject cmdSessionFocus(const QJsonObject& req);
    QJsonObject cmdSessionNewTab();
    QJsonObject cmdSessionClose(const QJsonObject& req);
    QJsonObject cmdSessionSplit(const QJsonObject& req);
    QJsonObject cmdSessionMove(const QJsonObject& req);

    // The focused session's kernel, resolved per command.
    ordo::core::Kernel& kernel() const;

    // Installs/removes the dispatcher trace observer (one per dispatcher).
    // attach clears any previous attachment first.
    void attachTraceObserver(ordo::core::Kernel& kernel);
    void detachTraceObserver();

    MainWindow* window_;
    ordo::core::Kernel* observedKernel_ = nullptr;
    QTcpServer* server_;

    // Partial (not-yet-newline-terminated) bytes per live connection; a request can span TCP reads.
    QHash<QTcpSocket*, QByteArray> lineBuffers_;

    // Cumulative pressed-button state across synthesized mouse events (QMouseEvent needs the changed button and the full set).
    Qt::MouseButtons pressedButtons_ = Qt::NoButton;

    // One recorded Dispatcher trace, as reported by the "events" command.
    struct TraceEntry {
        int seq{};
        std::string name;        // eventName, or "0x"+hex(typeHash) when the event has none
        std::size_t subscribers{};
        qint64 tMs{};             // ms since traceClock_ started (bridge construction)
    };

    static constexpr int kTraceLogCapacity = 100;

    std::deque<TraceEntry> traceLog_;
    int nextTraceSeq_ = 1;  // monotonic across the bridge's lifetime, independent of traceLog_ eviction
    QElapsedTimer traceClock_;
};

}  // namespace plnr::devbridge
