// SettingsStore / SettingsRegistry: in-memory JSON, tier priority, atomic write, flush.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "infra/settings_registry.h"
#include "infra/settings_store.h"

namespace {

using plnr::infra::SettingsRegistry;
using plnr::infra::SettingsStore;
using plnr::infra::StoreScope;

QCoreApplication& app() {
    static int argc = 1;
    static char name[] = "infra_settings_test";
    static char* argv[] = {name, nullptr};
    static QCoreApplication instance(argc, argv);
    return instance;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class InfraSettingsTest : public ::testing::Test {
protected:
    void SetUp() override { app(); }
};

TEST_F(InfraSettingsTest, InMemoryRoundTrip) {
    SettingsStore a{QString(), QString()};
    a.set(QStringLiteral("view.panel"), true);
    a.set(QStringLiteral("files.lastDir"), QStringLiteral("C:/models"));
    const QString json = a.saveUserToJson();

    SettingsStore b{QString(), QString()};
    ASSERT_TRUE(b.loadUserFromJson(json));
    EXPECT_TRUE(b.get(QStringLiteral("view.panel")).toBool());
    EXPECT_EQ(b.get(QStringLiteral("files.lastDir")).toString(), QStringLiteral("C:/models"));
    EXPECT_FALSE(b.loadUserFromJson(QStringLiteral("not json")));
    EXPECT_FALSE(b.loadUserFromJson(QStringLiteral("[1,2]")));
}

TEST_F(InfraSettingsTest, NestedJsonReadsDottedKeys) {
    SettingsStore s{QString(), QString()};
    ASSERT_TRUE(s.loadUserFromJson(QStringLiteral(R"({"view":{"tray":false}})")));
    EXPECT_FALSE(s.get(QStringLiteral("view.tray"), true).toBool());
}

TEST_F(InfraSettingsTest, FallbackWhenMissing) {
    SettingsStore s{QString(), QString()};
    EXPECT_TRUE(s.get(QStringLiteral("view.panel"), true).toBool());
    EXPECT_FALSE(s.hasAny(QStringLiteral("view.panel")));
}

TEST_F(InfraSettingsTest, WorkspaceOverridesUser) {
    SettingsStore s{QString(), QString()};
    const QString key = QStringLiteral("view.statusBar");
    s.set(key, true, StoreScope::User);
    EXPECT_EQ(s.effectiveScope(key), StoreScope::User);
    EXPECT_FALSE(s.isOverriddenInWorkspace(key));

    s.set(key, false, StoreScope::Workspace);
    EXPECT_FALSE(s.get(key).toBool());
    EXPECT_EQ(s.effectiveScope(key), StoreScope::Workspace);
    EXPECT_TRUE(s.isOverriddenInWorkspace(key));
    EXPECT_TRUE(s.getValueInScope(key, StoreScope::User).toBool());

    s.remove(key, StoreScope::Workspace);
    EXPECT_TRUE(s.get(key).toBool());
    EXPECT_FALSE(s.has(key, StoreScope::Workspace));
    EXPECT_TRUE(s.has(key, StoreScope::User));
}

TEST_F(InfraSettingsTest, SetEmitsOnlyOnEffectiveChange) {
    SettingsStore s{QString(), QString()};
    int changes = 0;
    QObject::connect(&s, &SettingsStore::settingChanged, [&] { ++changes; });
    s.set(QStringLiteral("view.tray"), false);
    s.set(QStringLiteral("view.tray"), false);
    EXPECT_EQ(changes, 1);
}

TEST_F(InfraSettingsTest, RemoveAndAllKeys) {
    SettingsStore s{QString(), QString()};
    s.set(QStringLiteral("b"), 1);
    s.set(QStringLiteral("a"), 2, StoreScope::Workspace);
    EXPECT_EQ(s.allKeys(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    s.remove(QStringLiteral("b"), StoreScope::User);
    EXPECT_FALSE(s.hasAny(QStringLiteral("b")));
}

TEST_F(InfraSettingsTest, AtomicWriteJsonFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("sub/settings.json"));
    ASSERT_TRUE(SettingsStore::atomicWriteJsonFile(target, QStringLiteral("{\"k\":1}")));
    EXPECT_EQ(readAll(target), QByteArray("{\"k\":1}"));
    // Overwrites an existing file and leaves no temp file behind.
    ASSERT_TRUE(SettingsStore::atomicWriteJsonFile(target, QStringLiteral("{\"k\":2}")));
    EXPECT_EQ(readAll(target), QByteArray("{\"k\":2}"));
    EXPECT_EQ(QDir(dir.filePath(QStringLiteral("sub"))).entryList(QDir::Files).size(), 1);
    EXPECT_FALSE(SettingsStore::atomicWriteJsonFile(QString(), QStringLiteral("{}")));
}

TEST_F(InfraSettingsTest, FlushSyncPersistsAndReloads) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("user.json"));
    {
        SettingsStore s{path, QString()};
        s.set(QStringLiteral("view.panel"), true);
        // The 300 ms debounce has not fired yet; nothing on disk.
        EXPECT_FALSE(QFile::exists(path));
        int flushes = 0;
        QObject::connect(&s, &SettingsStore::flushCompleted, [&] { ++flushes; });
        s.flushSync();
        EXPECT_EQ(flushes, 1);
        EXPECT_TRUE(QFile::exists(path));
    }
    SettingsStore reloaded{path, QString()};
    ASSERT_TRUE(reloaded.loadUser());
    EXPECT_TRUE(reloaded.get(QStringLiteral("view.panel")).toBool());
}

TEST_F(InfraSettingsTest, DestructorFlushesPendingSave) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("user.json"));
    {
        SettingsStore s{path, QString()};
        s.set(QStringLiteral("files.lastDir"), QStringLiteral("D:/x"));
    }
    const QJsonObject obj = QJsonDocument::fromJson(readAll(path)).object();
    EXPECT_EQ(obj.value(QStringLiteral("files.lastDir")).toString(), QStringLiteral("D:/x"));
}

TEST_F(InfraSettingsTest, DebouncedSaveFiresViaEventLoop) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("user.json"));
    SettingsStore s{path, QString()};
    bool flushed = false;
    QObject::connect(&s, &SettingsStore::flushCompleted, [&] { flushed = true; });
    s.set(QStringLiteral("view.tray"), false);

    QElapsedTimer timer;
    timer.start();
    while (!flushed && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    EXPECT_TRUE(flushed);
    EXPECT_TRUE(QFile::exists(path));
}

TEST_F(InfraSettingsTest, ActiveStoreLifecycle) {
    EXPECT_EQ(SettingsStore::activeStore(), nullptr);
    {
        SettingsStore s{QString(), QString()};
        SettingsStore::setActiveStore(&s);
        EXPECT_EQ(SettingsStore::activeStore(), &s);
    }
    EXPECT_EQ(SettingsStore::activeStore(), nullptr);
}

TEST_F(InfraSettingsTest, DefaultUserPathIsPlanura) {
    EXPECT_TRUE(SettingsStore::defaultUserPath().endsWith(QStringLiteral("/planura/settings.json")));
}

TEST_F(InfraSettingsTest, RegistryHoldsPlanuraKeys) {
    const SettingsRegistry& reg = SettingsRegistry::instance();
    const QStringList boolKeys = {QStringLiteral("view.tray"),          QStringLiteral("view.tray.entityInfo"),
                                  QStringLiteral("view.tray.materials"), QStringLiteral("view.tray.tags"),
                                  QStringLiteral("view.tray.styles"),    QStringLiteral("view.panel"),
                                  QStringLiteral("view.statusBar")};
    for (const QString& key : boolKeys) {
        const auto* def = reg.find(key);
        ASSERT_NE(def, nullptr) << qPrintable(key);
        EXPECT_EQ(def->type, plnr::infra::SettingType::Bool);
        EXPECT_EQ(def->defaultValue.toBool(), key != QStringLiteral("view.panel")) << qPrintable(key);
    }
    const auto* dir = reg.find(QStringLiteral("files.lastDir"));
    ASSERT_NE(dir, nullptr);
    EXPECT_EQ(dir->type, plnr::infra::SettingType::DirPath);
    EXPECT_FALSE(reg.has(QStringLiteral("canvas.gridStep")));
}

}  // namespace
