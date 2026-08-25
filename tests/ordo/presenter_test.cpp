#include <ordo/qt/presenter.h>
#include <ordo/qt/presenter_host.h>

#include <ordo/core/app_kernel.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QObject>
#include <QString>

#include <gtest/gtest.h>

namespace {

using ordo::core::AppKernel;
using ordo::qt::Presenter;
using ordo::qt::PresenterHost;

// A minimal event, mirroring the ordo_core dispatcher tests' style.
struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

// Exposes the protected member-function subscribe() so tests can register a
// handler, and records what it received.
class RecordingPresenter : public Presenter {
public:
    RecordingPresenter(AppKernel& kernel, QString name, QObject* viewComponent = nullptr)
        : Presenter(kernel, std::move(name), viewComponent) {
        subscribe<PingEvent>(&RecordingPresenter::onPing);
    }

    int receivedCount = 0;
    int lastValue = 0;

private:
    void onPing(const PingEvent& event) {
        ++receivedCount;
        lastValue = event.value;
    }
};

TEST(PresenterTest, MemberFunctionSubscriptionReceivesDispatchedPayload) {
    AppKernel kernel;
    RecordingPresenter presenter(kernel, QStringLiteral("Recorder"));

    kernel.dispatcher().dispatch(PingEvent{42});

    EXPECT_EQ(presenter.receivedCount, 1);
    EXPECT_EQ(presenter.lastValue, 42);
}

TEST(PresenterTest, TwoPresentersOnSameKernelBothReceive) {
    AppKernel kernel;
    RecordingPresenter presenterA(kernel, QStringLiteral("A"));
    RecordingPresenter presenterB(kernel, QStringLiteral("B"));

    kernel.dispatcher().dispatch(PingEvent{7});

    EXPECT_EQ(presenterA.receivedCount, 1);
    EXPECT_EQ(presenterB.receivedCount, 1);
    EXPECT_EQ(presenterA.lastValue, 7);
    EXPECT_EQ(presenterB.lastValue, 7);
}

TEST(PresenterTest, DestroyingOnePresenterUnsubscribesOnlyItsHandlers) {
    AppKernel kernel;
    auto presenterA = std::make_unique<RecordingPresenter>(kernel, QStringLiteral("A"));
    RecordingPresenter presenterB(kernel, QStringLiteral("B"));

    presenterA.reset();  // ~Presenter() must unsubscribe presenterA only

    kernel.dispatcher().dispatch(PingEvent{99});

    EXPECT_EQ(presenterB.receivedCount, 1);
    EXPECT_EQ(presenterB.lastValue, 99);
}

TEST(PresenterTest, SetViewComponentRoundTrips) {
    AppKernel kernel;
    Presenter presenter(kernel, QStringLiteral("Plain"));
    QObject view;

    EXPECT_EQ(presenter.viewComponent(), nullptr);

    presenter.setViewComponent(&view);
    EXPECT_EQ(presenter.viewComponent(), &view);
}

// A presenter that records its own lifecycle: onRegister() flips a flag on
// itself, and onRemove() appends its name to a shared order-recording vector
// (owned outside the presenter so it survives the presenter's destruction).
class LifecyclePresenter : public Presenter {
public:
    LifecyclePresenter(AppKernel& kernel, QString name, std::vector<std::string>* removeOrder)
        : Presenter(kernel, std::move(name)), removeOrder_(removeOrder) {
        subscribe<PingEvent>(&LifecyclePresenter::onPing);
    }

    void onRegister() override { registered = true; }
    void onRemove() override { removeOrder_->push_back(name().toStdString()); }

    bool registered = false;
    int receivedCount = 0;

private:
    void onPing(const PingEvent&) { ++receivedCount; }

    std::vector<std::string>* removeOrder_;
};

TEST(PresenterHostTest, AddCallsOnRegister) {
    AppKernel kernel;
    PresenterHost host;
    std::vector<std::string> removeOrder;

    auto* presenter = host.add<LifecyclePresenter>(kernel, QStringLiteral("Solo"), &removeOrder);

    EXPECT_TRUE(presenter->registered);
}

TEST(PresenterHostTest, ClearCallsOnRemoveInLifoOrder) {
    AppKernel kernel;
    PresenterHost host;
    std::vector<std::string> removeOrder;

    host.add<LifecyclePresenter>(kernel, QStringLiteral("First"), &removeOrder);
    host.add<LifecyclePresenter>(kernel, QStringLiteral("Second"), &removeOrder);
    host.add<LifecyclePresenter>(kernel, QStringLiteral("Third"), &removeOrder);

    host.clear();

    const std::vector<std::string> expected = {"Third", "Second", "First"};
    EXPECT_EQ(removeOrder, expected);
}

TEST(PresenterHostTest, DestructionCallsOnRemoveInLifoOrder) {
    AppKernel kernel;
    std::vector<std::string> removeOrder;

    {
        PresenterHost host;
        host.add<LifecyclePresenter>(kernel, QStringLiteral("First"), &removeOrder);
        host.add<LifecyclePresenter>(kernel, QStringLiteral("Second"), &removeOrder);
    }  // ~PresenterHost() must call onRemove() LIFO

    const std::vector<std::string> expected = {"Second", "First"};
    EXPECT_EQ(removeOrder, expected);
}

TEST(PresenterHostTest, ReturnedPointerIsUsable) {
    AppKernel kernel;
    PresenterHost host;
    std::vector<std::string> removeOrder;

    auto* presenter = host.add<LifecyclePresenter>(kernel, QStringLiteral("Recorder"), &removeOrder);

    kernel.dispatcher().dispatch(PingEvent{5});

    EXPECT_EQ(presenter->receivedCount, 1);
}

}  // namespace
