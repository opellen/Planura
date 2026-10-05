#include "ui/io_presenter.h"

#include <utility>

#include <QObject>
#include <QString>

namespace plnr::ui {

IoPresenter::IoPresenter(QObject* parent, SnapshotHandler onSnapshot, ObjBytesHandler onObjBytes)
    : Presenter(QStringLiteral("IoPresenter"), parent),
      onSnapshot_(std::move(onSnapshot)),
      onObjBytes_(std::move(onObjBytes)) {}

void IoPresenter::onRegister() {
    subscribe<events::DocumentSnapshotReady>(&IoPresenter::onSnapshotReady);
    subscribe<events::ObjBytesReady>(&IoPresenter::onObjBytesReady);
}

void IoPresenter::onObjBytesReady(const events::ObjBytesReady& event) {
    if (onObjBytes_) onObjBytes_(event);
}

void IoPresenter::onSnapshotReady(const events::DocumentSnapshotReady& event) {
    if (onSnapshot_) onSnapshot_(event);
}

}  // namespace plnr::ui
