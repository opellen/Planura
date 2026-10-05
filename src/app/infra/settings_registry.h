// Ported from ordo-state-designer infra/settings_registry.h.
#pragma once

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <vector>

namespace plnr::infra {

enum class SettingType {
    Bool,
    Int,
    Double,
    String,
    Enum,
    FilePath,
    DirPath,
    Color
};

enum class SettingScope {
    UserOnly,       // Global settings only (e.g. license keys, global editor prefs)
    WorkspaceOnly,  // Workspace-bound only (.planura/settings.json)
    Overridable,    // Default to user, can be overridden in workspace
    ProjectOnly     // Reserved, unused: no per-document manifest exists yet
};

struct SettingDefinition {
    QString key;                  // Dotted unique key, e.g. "files.lastDir"
    QString title;                // Human-readable title: "Last Directory"
    QString description;          // Description / hint text
    SettingType type = SettingType::String;
    QJsonValue defaultValue;      // Default value
    SettingScope scope = SettingScope::Overridable;
    QString category;             // Category: "Files", "View"

    // Optional constraints
    double minimum = 0.0;
    double maximum = 1000.0;
    QStringList enumOptions;      // For SettingType::Enum
    QString fileFilter;           // For FilePath
};

// Schema-driven registry for Planura settings.
// Holds declarative definitions for core settings and future plugin contributions.
class SettingsRegistry {
public:
    static SettingsRegistry& instance();

    void registerSetting(SettingDefinition def);
    const SettingDefinition* find(const QString& key) const;
    bool has(const QString& key) const;

    const std::vector<SettingDefinition>& allSettings() const { return definitions_; }
    std::vector<SettingDefinition> settingsForCategory(const QString& category) const;
    const QStringList& categories() const { return categories_; }

    void registerCoreSettings();
    void clear();

private:
    SettingsRegistry();

    std::vector<SettingDefinition> definitions_;
    QHash<QString, std::size_t> keyIndexMap_;
    QStringList categories_;
};

}  // namespace plnr::infra
