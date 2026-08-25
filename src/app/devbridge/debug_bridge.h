#pragma once

// In-app TCP JSON debug bridge: synthetic input + state queries over a
// newline-delimited localhost protocol. Mutations use the real input path by
// policy (documented kernel_.send exceptions) -- see debug_bridge.cpp's top comment.

#include <deque>
#include <string>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QtGlobal>

#include <ordo/core/app_kernel.h>
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
    // Default port when neither --debug-bridge nor PLNR_BRIDGE_PORT is set.
    // The Python-side default must stay in sync with this value.
    static constexpr quint16 kDefaultPort = 45454;

    // Starts listening on 127.0.0.1:port immediately. kernel/window are
    // non-owning and must outlive this object (true for the app's whole
    // lifetime).
    DebugBridge(ordo::core::AppKernel& kernel, MainWindow* window, quint16 port, QObject* parent = nullptr);
    ~DebugBridge() override;

    bool isListening() const;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    // Dispatches one request line via execute() and always writes back exactly
    // one response line, {"id":N,"ok":true,...} or {"id":N,"ok":false,"error":...},
    // even on a parse failure or a thrown command.
    void handleLine(QTcpSocket* socket, const QByteArray& line);

    // Returns the command-specific fields for the {"ok":true,...} response.
    // Throws CommandError on any recognized failure; handleLine() turns that
    // into an {"ok":false,...} reply.
    QJsonObject execute(const QString& cmd, const QJsonObject& req);

    // Query commands -- read GeometryApi/Camera/MainWindow state, never
    // mutate anything.
    QJsonObject cmdPing() const;
    QJsonObject cmdScene() const;
    QJsonObject cmdCamera() const;
    // Sets a subset of the camera pose via CameraStore::restoreCamera -- see
    // debug_bridge.cpp's protocol comment for the shape.
    QJsonObject cmdCameraSet(const QJsonObject& req);
    QJsonObject cmdStatus() const;
    QJsonObject cmdSelection() const;
    QJsonObject cmdEditContext() const;
    QJsonObject cmdEntityInfo() const;
    // Read-only VCB (Measurements Box) query. No command yet types into/commits the field.
    QJsonObject cmdVcb() const;
    QJsonObject cmdOverlayStats() const;
    QJsonObject cmdTags() const;
    QJsonObject cmdPick(const QJsonObject& req) const;
    QJsonObject cmdInfer(const QJsonObject& req) const;
    QJsonObject cmdProject(const QJsonObject& req) const;
    QJsonObject cmdScreenshot(const QJsonObject& req) const;
    QJsonObject cmdEvents(const QJsonObject& req) const;

    // Shared by cmdDoc's "state" query and its mutating actions -- see
    // debug_bridge.cpp's protocol comment for the shape.
    QJsonObject docStateJson() const;

    // Shared by cmdStyle's "state" query and its mutating actions -- see
    // debug_bridge.cpp's protocol comment for the shape.
    QJsonObject styleStateJson() const;

    // Shared by cmdShadow's "state" query and its mutating actions -- see
    // debug_bridge.cpp's protocol comment for the shape.
    QJsonObject shadowStateJson() const;

    // Shared by cmdFog's "state" query and its mutating actions -- see
    // debug_bridge.cpp's protocol comment for the shape.
    QJsonObject fogStateJson() const;

    // Injection commands -- synthesize Qt input events and post them to the
    // viewport widget, the same delivery path real input takes.
    QJsonObject cmdMouse(const QString& cmd, const QJsonObject& req);
    QJsonObject cmdKey(const QJsonObject& req);
    QJsonObject cmdTool(const QJsonObject& req);

    // Finds and triggers the QAction under menu `menu` matching text `text`
    // -- for menu-only commands with no toolbar/palette entry.
    QJsonObject cmdMenuAction(const QJsonObject& req);

    // Scripts ToolController's confirm/prompt/warning dialogs so a scenario
    // run never opens a real Qt dialog -- see debug_bridge.cpp's protocol
    // comment for the action shapes.
    QJsonObject cmdModal(const QJsonObject& req);

    // Drives ToolController::buildContextMenuItems directly -- never opens a
    // real QMenu. See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdContextMenu(const QJsonObject& req);

    // Bypasses confirmDiscardChanges() and the Open/Save QFileDialogs on
    // purpose. See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdDoc(const QJsonObject& req);

    // kernel_.send exception -- TagsPresenter's Tray-dock widgets carry no
    // QAction. See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdTag(const QJsonObject& req);

    // kernel_.send exception -- MaterialsPresenter's Tray-dock widgets carry
    // no QAction. See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdMaterial(const QJsonObject& req);

    // QAction::trigger() path (View menu), except set_ao_strength (no tray
    // panel yet). See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdStyle(const QJsonObject& req);

    // Hybrid of QAction triggers and kernel_.send exceptions. See
    // debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdShadow(const QJsonObject& req);

    // Hybrid like cmdShadow. See debug_bridge.cpp's protocol comment for the action shapes.
    QJsonObject cmdFog(const QJsonObject& req);

    // Read-only solid-classification query, or (action:"apply") a kernel_.send
    // exception triggering a Solid Tools op -- see debug_bridge.cpp's protocol
    // comment for the shapes. The one cmd* not const (action:"apply" mutates).
    QJsonObject cmdSolid(const QJsonObject& req);

    // Dispatcher trace observer (see the ctor) -- appends to traceLog_,
    // dropping the oldest past kTraceLogCapacity. Never mutates domain state.
    void onDispatchTrace(const ordo::core::DispatchRecord& record);

    ordo::core::AppKernel& kernel_;
    MainWindow* window_;
    QTcpServer* server_;

    // Partial (not-yet-newline-terminated) bytes received so far, per live
    // connection -- a request can arrive split across multiple TCP reads.
    QHash<QTcpSocket*, QByteArray> lineBuffers_;

    // Cumulative pressed-button state across synthesized mouse_press/move/
    // release calls -- a QMouseEvent needs both the button that changed and
    // the full current button set, and callers only tell us the former.
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
