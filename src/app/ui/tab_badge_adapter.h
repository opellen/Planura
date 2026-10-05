#pragma once

#include <ordo/qt/view_adapter.h>

#include <QString>

#include "agent/events.h"

namespace plnr::agent {
class DocumentStore;
}  // namespace plnr::agent

namespace plnr::ui {

// The document's display name: the file's basename, or "Untitled" while
// unsaved. Single source for the window title, the tab text and the
// close-confirmation prompt.
QString documentDisplayName(const agent::DocumentStore& document);

// Mirrors one session's DocumentStore identity/dirty state for its editor tab. Lives in a
// ViewHost the session owns for its whole lifetime (not the tool host), so it tracks across
// presenter rebinds. Not a Presenter: no view component, only the stateChanged() signal.
class TabBadgeAdapter : public ordo::qt::ViewAdapter {
    Q_OBJECT

public:
    TabBadgeAdapter();

    // Subscribes to DocumentStateChanged and takes the initial snapshot.
    void onRegister() override;

    // Display name only; the "*" dirty marker is applied by displayText().
    const QString& title() const { return title_; }
    bool dirty() const { return dirty_; }

    // The tab text: title plus "*" while dirty.
    QString displayText() const;

signals:
    void stateChanged();

private:
    void onDocumentStateChanged(const events::DocumentStateChanged& event);
    void refresh();

    QString title_;
    bool dirty_ = false;
};

}  // namespace plnr::ui
