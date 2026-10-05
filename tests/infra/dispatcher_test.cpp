// IoDispatcher: DirectSynchronous paths are deterministic; the Async smoke drains the queued
// completion hop with processEvents.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <optional>
#include <thread>
#include <vector>

#include "infra/io_dispatcher.h"

namespace {

using plnr::infra::ExecutionPolicy;
using plnr::infra::IoDispatcher;
namespace events = plnr::infra::events;

QCoreApplication& app() {
    static int argc = 1;
    static char name[] = "infra_dispatcher_test";
    static char* argv[] = {name, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

// Pumps the event loop until `done` or a 5 s deadline.
template <typename Pred>
bool pumpUntil(Pred done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

class InfraDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override { app(); }
};

TEST_F(InfraDispatcherTest, DirectSynchronousTaskSuccess) {
    IoDispatcher d;
    d.setExecutionPolicy(ExecutionPolicy::DirectSynchronous);

    std::vector<events::IoOperationKind> started;
    int completed = 0;
    QObject::connect(&d, &IoDispatcher::ioStarted, [&](const events::IoStarted& e) { started.push_back(e.kind); });
    QObject::connect(&d, &IoDispatcher::ioCompleted, [&](const events::IoCompleted& e) {
        ++completed;
        EXPECT_EQ(std::any_cast<int>(e.resultPayload), 42);
        EXPECT_EQ(e.targetPath, QStringLiteral("a.plr"));
    });

    int got = 0;
    const quint64 id = d.submitTask<int>(
        events::IoOperationKind::OpenDocument, QStringLiteral("a.plr"), QStringLiteral("Open"), true,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback progress, QString*) -> std::optional<int> {
            progress(50, QStringLiteral("half"));
            return 42;
        },
        [&](int v) { got = v; });

    EXPECT_NE(id, 0u);
    EXPECT_EQ(got, 42);
    EXPECT_EQ(completed, 1);
    ASSERT_EQ(started.size(), 1u);
    EXPECT_EQ(started[0], events::IoOperationKind::OpenDocument);
    EXPECT_FALSE(d.isRunning(id));
}

TEST_F(InfraDispatcherTest, DirectSynchronousTaskProgressEmitted) {
    IoDispatcher d;
    d.setExecutionPolicy(ExecutionPolicy::DirectSynchronous);
    int lastPercent = -2;
    QObject::connect(&d, &IoDispatcher::ioProgress, [&](const events::IoProgress& e) { lastPercent = e.percentage; });
    d.submitTask<int>(
        events::IoOperationKind::LoadTexture, QString(), QStringLiteral("Tex"), false,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback progress, QString*) -> std::optional<int> {
            progress(75, QStringLiteral("x"));
            return 1;
        });
    EXPECT_EQ(lastPercent, 75);
}

TEST_F(InfraDispatcherTest, DirectSynchronousTaskError) {
    IoDispatcher d;
    d.setExecutionPolicy(ExecutionPolicy::DirectSynchronous);

    QString signalError;
    QObject::connect(&d, &IoDispatcher::ioFailed, [&](const events::IoFailed& e) { signalError = e.errorMessage; });

    bool succeeded = false;
    QString cbError;
    d.submitTask<int>(
        events::IoOperationKind::ImportObj, QStringLiteral("m.obj"), QStringLiteral("Import"), true,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback, QString* error) -> std::optional<int> {
            *error = QStringLiteral("boom");
            return std::nullopt;
        },
        [&](int) { succeeded = true; }, [&](const QString& e) { cbError = e; });

    EXPECT_FALSE(succeeded);
    EXPECT_EQ(cbError, QStringLiteral("boom"));
    EXPECT_EQ(signalError, QStringLiteral("boom"));
}

TEST_F(InfraDispatcherTest, DirectSynchronousTaskCancelReportsCanceled) {
    IoDispatcher d;
    d.setExecutionPolicy(ExecutionPolicy::DirectSynchronous);

    QString signalError;
    QObject::connect(&d, &IoDispatcher::ioFailed, [&](const events::IoFailed& e) { signalError = e.errorMessage; });

    bool succeeded = false;
    QString cbError;
    // The work returns a value but the token is set: the cancel-after-success
    // race reports it canceled and discards the result.
    d.submitTask<int>(
        events::IoOperationKind::ExportObj, QString(), QStringLiteral("Export"), true,
        [](const std::atomic<bool>& token, IoDispatcher::ProgressCallback, QString*) -> std::optional<int> {
            const_cast<std::atomic<bool>&>(token).store(true);
            return 7;
        },
        [&](int) { succeeded = true; }, [&](const QString& e) { cbError = e; });

    EXPECT_FALSE(succeeded);
    EXPECT_EQ(cbError, QStringLiteral("Operation canceled"));
    EXPECT_EQ(signalError, QStringLiteral("Operation canceled"));
}

TEST_F(InfraDispatcherTest, CancelUnknownOpIdReturnsFalse) {
    IoDispatcher d;
    EXPECT_FALSE(d.cancel(9999));
    EXPECT_FALSE(d.isRunning(9999));
}

TEST_F(InfraDispatcherTest, SubmitActionWrapperSuccessAndFailure) {
    IoDispatcher d;
    d.setExecutionPolicy(ExecutionPolicy::DirectSynchronous);

    bool ok = false;
    d.submitAction(
        events::IoOperationKind::SaveDocument, QStringLiteral("s.plr"), QStringLiteral("Save"), true,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback, QString*) { return true; }, [&] { ok = true; });
    EXPECT_TRUE(ok);

    QString err;
    bool ok2 = false;
    d.submitAction(
        events::IoOperationKind::SaveDocument, QStringLiteral("s.plr"), QStringLiteral("Save"), true,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback, QString* e) {
            *e = QStringLiteral("disk full");
            return false;
        },
        [&] { ok2 = true; }, [&](const QString& e) { err = e; });
    EXPECT_FALSE(ok2);
    EXPECT_EQ(err, QStringLiteral("disk full"));
}

TEST_F(InfraDispatcherTest, AsyncSuccessCallbackLandsOnCallerThread) {
    IoDispatcher d;
    ASSERT_EQ(d.executionPolicy(), ExecutionPolicy::Async);

    const auto mainThread = std::this_thread::get_id();
    bool done = false;
    int got = 0;
    std::thread::id callbackThread;
    d.submitTask<int>(
        events::IoOperationKind::OpenDocument, QStringLiteral("a.plr"), QStringLiteral("Open"), false,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback, QString*) -> std::optional<int> { return 5; },
        [&](int v) {
            got = v;
            callbackThread = std::this_thread::get_id();
            done = true;
        });
    d.waitForDone();
    ASSERT_TRUE(pumpUntil([&] { return done; }));
    EXPECT_EQ(got, 5);
    EXPECT_EQ(callbackThread, mainThread);
}

TEST_F(InfraDispatcherTest, AsyncErrorCallbackLands) {
    IoDispatcher d;
    bool done = false;
    QString err;
    d.submitTask<int>(
        events::IoOperationKind::ImportObj, QString(), QStringLiteral("Import"), false,
        [](const std::atomic<bool>&, IoDispatcher::ProgressCallback, QString* e) -> std::optional<int> {
            *e = QStringLiteral("bad");
            return std::nullopt;
        },
        nullptr, [&](const QString& e) {
            err = e;
            done = true;
        });
    d.waitForDone();
    ASSERT_TRUE(pumpUntil([&] { return done; }));
    EXPECT_EQ(err, QStringLiteral("bad"));
}

}  // namespace
