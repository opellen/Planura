#include <ordo/qt/presenter.h>
#include <ordo/qt/view_host.h>

#include <ordo/core/kernel.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QObject>
#include <QString>

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using ordo::qt::Presenter;
using ordo::qt::ViewHost;

// A minimal event, mirroring the ordo_core dispatcher tests' style.
struct PingEvent {
    static constexpr std::string_view eventName = "Ping";
    int value = 0;
};

// Exposes the protected member-function subscribe() so tests can register a
// handler, and records what it received. Subscription happens in onRegister()
// -- the context is injected at registration, never available in the ctor.
class RecordingPresenter : public Presenter {
public:
    explicit RecordingPresenter(const QString& name, QObject* viewComponent = nullptr)
        : Presenter(name, viewComponent) {}

    void onRegister() override { subscribe<PingEvent>(&RecordingPresenter::onPing); }

    int receivedCount = 0;
    int lastValue = 0;

private:
    void onPing(const PingEvent& event) {
        ++receivedCount;
        lastValue = event.value;
    }
};

TEST(PresenterTest, MemberFunctionSubscriptionReceivesDispatchedPayload) {
    Kernel kernel;
    ViewHost host(kernel);
    auto* presenter = host.add<RecordingPresenter>(QStringLiteral("Recorder"));

    kernel.dispatcher().dispatch(PingEvent{42});

    EXPECT_EQ(presenter->receivedCount, 1);
    EXPECT_EQ(presenter->lastValue, 42);
}

TEST(PresenterTest, TwoPresentersOnSameKernelBothReceive) {
    Kernel kernel;
    ViewHost host(kernel);
    auto* presenterA = host.add<RecordingPresenter>(QStringLiteral("A"));
    auto* presenterB = host.add<RecordingPresenter>(QStringLiteral("B"));

    kernel.dispatcher().dispatch(PingEvent{7});

    EXPECT_EQ(presenterA->receivedCount, 1);
    EXPECT_EQ(presenterB->receivedCount, 1);
    EXPECT_EQ(presenterA->lastValue, 7);
    EXPECT_EQ(presenterB->lastValue, 7);
}

TEST(PresenterTest, DestroyingOnePresenterUnsubscribesOnlyItsHandlers) {
    Kernel kernel;
    ViewHost hostB(kernel);
    auto* presenterB = hostB.add<RecordingPresenter>(QStringLiteral("B"));

    {
        ViewHost hostA(kernel);
        hostA.add<RecordingPresenter>(QStringLiteral("A"));
    }  // ~Presenter() must unsubscribe A's handlers only

    kernel.dispatcher().dispatch(PingEvent{99});

    EXPECT_EQ(presenterB->receivedCount, 1);
    EXPECT_EQ(presenterB->lastValue, 99);
}

TEST(PresenterTest, SetViewComponentRoundTrips) {
    // View plumbing needs no kernel and no registration at all.
    Presenter presenter(QStringLiteral("Plain"));
    QObject view;

    EXPECT_EQ(presenter.viewComponent(), nullptr);

    presenter.setViewComponent(&view);
    EXPECT_EQ(presenter.viewComponent(), &view);
}

// A presenter that records its own lifecycle: onRegister() flips a flag and
// subscribes, and onRemove() appends its name to a shared order-recording
// vector (owned outside the presenter so it survives destruction).
class LifecyclePresenter : public Presenter {
public:
    LifecyclePresenter(const QString& name, std::vector<std::string>* removeOrder)
        : Presenter(name), removeOrder_(removeOrder) {}

    void onRegister() override {
        registered = true;
        subscribe<PingEvent>(&LifecyclePresenter::onPing);
    }
    void onRemove() override { removeOrder_->push_back(name().toStdString()); }

    bool registered = false;
    int receivedCount = 0;

private:
    void onPing(const PingEvent&) { ++receivedCount; }

    std::vector<std::string>* removeOrder_;
};

TEST(ViewHostTest, AddInjectsContextAndCallsOnRegister) {
    Kernel kernel;
    ViewHost host(kernel);
    std::vector<std::string> removeOrder;

    auto* presenter = host.add<LifecyclePresenter>(QStringLiteral("Solo"), &removeOrder);

    EXPECT_TRUE(presenter->registered);
}

TEST(ViewHostTest, ClearCallsOnRemoveInLifoOrder) {
    Kernel kernel;
    ViewHost host(kernel);
    std::vector<std::string> removeOrder;

    host.add<LifecyclePresenter>(QStringLiteral("First"), &removeOrder);
    host.add<LifecyclePresenter>(QStringLiteral("Second"), &removeOrder);
    host.add<LifecyclePresenter>(QStringLiteral("Third"), &removeOrder);

    host.clear();

    const std::vector<std::string> expected = {"Third", "Second", "First"};
    EXPECT_EQ(removeOrder, expected);
}

TEST(ViewHostTest, DestructionCallsOnRemoveInLifoOrder) {
    Kernel kernel;
    std::vector<std::string> removeOrder;

    {
        ViewHost host(kernel);
        host.add<LifecyclePresenter>(QStringLiteral("First"), &removeOrder);
        host.add<LifecyclePresenter>(QStringLiteral("Second"), &removeOrder);
    }  // ~ViewHost() must call onRemove() LIFO

    const std::vector<std::string> expected = {"Second", "First"};
    EXPECT_EQ(removeOrder, expected);
}

TEST(ViewHostTest, ReturnedPointerIsUsable) {
    Kernel kernel;
    ViewHost host(kernel);
    std::vector<std::string> removeOrder;

    auto* presenter = host.add<LifecyclePresenter>(QStringLiteral("Recorder"), &removeOrder);

    kernel.dispatcher().dispatch(PingEvent{5});

    EXPECT_EQ(presenter->receivedCount, 1);
}

}  // namespace
