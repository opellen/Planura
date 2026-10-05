#pragma once

#include <functional>

#include <ordo/qt/presenter.h>

#include "agent/events.h"

class QObject;

namespace plnr::ui {

// Forwards DocumentSnapshotReady (answer to SaveSnapshotRequested) and ObjBytesReady (answer to
// ExportObjSnapshotRequested) to the MainWindow-owned async flows; dispatcher orchestration
// stays in MainWindow.
class IoPresenter : public ordo::qt::Presenter {
public:
    using SnapshotHandler = std::function<void(const events::DocumentSnapshotReady&)>;
    using ObjBytesHandler = std::function<void(const events::ObjBytesReady&)>;

    IoPresenter(QObject* parent, SnapshotHandler onSnapshot, ObjBytesHandler onObjBytes);

    void onRegister() override;

private:
    void onSnapshotReady(const events::DocumentSnapshotReady& event);
    void onObjBytesReady(const events::ObjBytesReady& event);

    SnapshotHandler onSnapshot_;
    ObjBytesHandler onObjBytes_;
};

}  // namespace plnr::ui
