#include <ordo/qt/presenter.h>

#include <utility>

Q_LOGGING_CATEGORY(ordoPresenter, "ordo.presenter")

namespace ordo::qt {

Presenter::Presenter(ordo::core::AppKernel& kernel, QString name, QObject* viewComponent)
    : QObject(nullptr), kernel_(kernel), name_(std::move(name)), viewComponent_(viewComponent) {
    qCDebug(ordoPresenter) << name_ << "presenter created";
}

Presenter::~Presenter() {
    kernel_.dispatcher().unsubscribe(this);
}

const QString& Presenter::name() const {
    return name_;
}

QObject* Presenter::viewComponent() const {
    return viewComponent_;
}

void Presenter::setViewComponent(QObject* view) {
    viewComponent_ = view;
}

ordo::core::AppKernel& Presenter::kernel() {
    return kernel_;
}

}  // namespace ordo::qt
