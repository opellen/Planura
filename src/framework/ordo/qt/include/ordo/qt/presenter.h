#pragma once

#include <functional>
#include <typeinfo>
#include <utility>

#include <QLoggingCategory>
#include <QObject>
#include <QString>

#include <ordo/core/app_kernel.h>
#include <ordo/core/dispatcher.h>

Q_DECLARE_LOGGING_CATEGORY(ordoPresenter)

namespace ordo::qt {

// PureMVC Mediator role: the only Ordo layer allowed to touch Qt. Bridges
// kernel events to a Qt view component (a widget, a QML root object, ...).
class Presenter : public QObject {
    Q_OBJECT

public:
    // Does not take ownership of viewComponent -- the caller keeps managing its
    // lifetime (typically Qt's parent/child tree owns it separately). name is an
    // identity used for logging/debugging only.
    explicit Presenter(ordo::core::AppKernel& kernel, QString name, QObject* viewComponent = nullptr);

    // Unsubscribes everything this presenter subscribed on kernel().dispatcher(),
    // regardless of event type, so a destroyed presenter can never leave a
    // dangling handler behind.
    ~Presenter() override;

    const QString& name() const;
    QObject* viewComponent() const;
    void setViewComponent(QObject* view);

    // Lifecycle hooks, called by the owner after construction / before teardown
    // (mirrors Agent::onRegister/onRemove; kept manual until an app-level
    // presenter registry exists).
    virtual void onRegister() {}
    virtual void onRemove() {}

protected:
    ordo::core::AppKernel& kernel();

    // Subscribes a member function of the most-derived presenter class:
    //   subscribe<events::ToolChanged>(&MyPresenter::onToolChanged);
    template <typename EventT, typename SelfT>
    void subscribe(void (SelfT::*memberFn)(const EventT&)) {
        auto* self = static_cast<SelfT*>(this);
        subscribe<EventT>(
            std::function<void(const EventT&)>([self, memberFn](const EventT& event) { (self->*memberFn)(event); }));
    }

    // Subscribes an arbitrary callable, e.g. subscribe<EventT>([](const EventT&
    // e) { ... }). Owner cookie is always `this`, so ~Presenter() cleans it up.
    template <typename EventT>
    void subscribe(std::function<void(const EventT&)> handler) {
        qCDebug(ordoPresenter) << name_ << "subscribing to" << typeid(EventT).name();
        kernel_.dispatcher().subscribe<EventT>(this, std::move(handler));
    }

private:
    ordo::core::AppKernel& kernel_;
    QString name_;
    QObject* viewComponent_;
};

}  // namespace ordo::qt
