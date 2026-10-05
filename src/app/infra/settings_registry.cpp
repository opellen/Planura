// Ported from ordo-state-designer infra/settings_registry.cpp.
#include "infra/settings_registry.h"

namespace plnr::infra {

SettingsRegistry::SettingsRegistry() {
    registerCoreSettings();
}

SettingsRegistry& SettingsRegistry::instance() {
    static SettingsRegistry registry;
    return registry;
}

void SettingsRegistry::registerSetting(SettingDefinition def) {
    if (def.key.isEmpty()) {
        return;
    }

    if (keyIndexMap_.contains(def.key)) {
        // Update existing definition
        std::size_t idx = keyIndexMap_.value(def.key);
        definitions_[idx] = std::move(def);
        return;
    }

    if (!def.category.isEmpty() && !categories_.contains(def.category)) {
        categories_.append(def.category);
    }

    std::size_t idx = definitions_.size();
    keyIndexMap_.insert(def.key, idx);
    definitions_.push_back(std::move(def));
}

const SettingDefinition* SettingsRegistry::find(const QString& key) const {
    auto it = keyIndexMap_.constFind(key);
    if (it == keyIndexMap_.constEnd()) {
        return nullptr;
    }
    return &definitions_[it.value()];
}

bool SettingsRegistry::has(const QString& key) const {
    return keyIndexMap_.contains(key);
}

std::vector<SettingDefinition> SettingsRegistry::settingsForCategory(const QString& category) const {
    std::vector<SettingDefinition> result;
    for (const auto& def : definitions_) {
        if (def.category.compare(category, Qt::CaseInsensitive) == 0) {
            result.push_back(def);
        }
    }
    return result;
}

void SettingsRegistry::clear() {
    definitions_.clear();
    keyIndexMap_.clear();
    categories_.clear();
}

void SettingsRegistry::registerCoreSettings() {
    if (!definitions_.empty()) {
        return;
    }
    definitions_.reserve(8);

    {
        SettingDefinition def;
        def.key = QStringLiteral("files.lastDir");
        def.title = QStringLiteral("Last File Dialog Directory");
        def.description = QStringLiteral("Directory the Open, Save As, Import and Export dialogs start in.");
        def.type = SettingType::DirPath;
        def.defaultValue = QString();
        def.category = QStringLiteral("Files");
        def.scope = SettingScope::UserOnly;
        registerSetting(def);
    }

    // Shell chrome visibility; only the bottom panel starts hidden.
    const struct {
        const char* key;
        const char* title;
        bool defaultValue;
    } viewBools[] = {
        {"view.tray", "Show Tray", true},
        {"view.tray.entityInfo", "Show Entity Info Section", true},
        {"view.tray.materials", "Show Materials Section", true},
        {"view.tray.tags", "Show Tags Section", true},
        {"view.tray.styles", "Show Styles Section", true},
        {"view.panel", "Show Bottom Panel", false},
        {"view.statusBar", "Show Status Bar", true},
    };
    for (const auto& entry : viewBools) {
        SettingDefinition def;
        def.key = QString::fromLatin1(entry.key);
        def.title = QString::fromLatin1(entry.title);
        def.description = QStringLiteral("Restored on the next launch.");
        def.type = SettingType::Bool;
        def.defaultValue = entry.defaultValue;
        def.category = QStringLiteral("View");
        def.scope = SettingScope::UserOnly;
        registerSetting(def);
    }
}

}  // namespace plnr::infra
