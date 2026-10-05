#include "ui/tab_badge_adapter.h"

#include <QFileInfo>

#include "agent/document_store.h"

namespace plnr::ui {

QString documentDisplayName(const agent::DocumentStore& document) {
    const std::string& path = document.filePath();
    return path.empty() ? QStringLiteral("Untitled") : QFileInfo(QString::fromStdString(path)).fileName();
}

TabBadgeAdapter::TabBadgeAdapter() : ViewAdapter(QStringLiteral("TabBadgeAdapter")) {}

void TabBadgeAdapter::onRegister() {
    subscribe<events::DocumentStateChanged>(&TabBadgeAdapter::onDocumentStateChanged);
    refresh();
}

void TabBadgeAdapter::onDocumentStateChanged(const events::DocumentStateChanged& /*event*/) {
    refresh();
}

QString TabBadgeAdapter::displayText() const {
    return dirty_ ? title_ + QLatin1Char('*') : title_;
}

void TabBadgeAdapter::refresh() {
    auto document = context().agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document) return;  // No DocumentStore registered -- keep the last snapshot.

    const QString title = documentDisplayName(*document);
    const bool dirty = document->dirty();
    if (title == title_ && dirty == dirty_) return;
    title_ = title;
    dirty_ = dirty;
    emit stateChanged();
}

}  // namespace plnr::ui
