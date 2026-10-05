#pragma once

#include <functional>
#include <vector>

#include <QString>
#include <QTabBar>
#include <QWidget>

class QContextMenuEvent;
class QVBoxLayout;
class QLabel;
class QMouseEvent;
class QPoint;
class QStackedWidget;

namespace plnr {
class DocumentSession;
}  // namespace plnr

namespace plnr::ui {

// The group's tab strip: a QTabBar plus three signals: a right-click's position,
// any press on the strip (a focus request), and a click on the empty area past
// the last tab.
class EditorTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit EditorTabBar(QWidget* parent = nullptr);

signals:
    void tabContextMenuRequested(int index, const QPoint& globalPos);
    void barPressed();
    void barBackgroundClicked();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
};

// A tab strip over a stack of pages. A session tab's page holds the session's viewport; a
// custom tab's page is the caller's widget. With no tab open the stack shows an empty page.
// Owns tab/page mechanics only: never destroys a DocumentSession. A session-tab close is
// reported through tabCloseRequested() and MainWindow decides; the last-session-tab rule
// spans every group, so MainWindow supplies it as a close guard (setCloseGuard()).
class EditorGroup : public QWidget {
    Q_OBJECT

public:
    struct Tab {
        DocumentSession* session = nullptr;  // non-owning; null for a custom tab
        QWidget* page = nullptr;             // the stack page this tab shows
        QString title;
        bool isCustom = false;
    };

    explicit EditorGroup(QWidget* parent = nullptr);

    // Appends a tab for `session`, makes it current, and moves the session's
    // viewport into the new page. Call before the viewport is shown: the
    // reparent is one-time.
    void addSessionTab(DocumentSession& session, const QString& title);

    // Appends a custom widget tab, makes it current, and returns it.
    QWidget* openCustomTab(QWidget* widget, const QString& title);

    // Session tab: emits tabCloseRequested() and leaves the tab in place. Custom
    // tab: removes it and deletes its widget later. A session-tab close is
    // ignored while the close guard (default: this group holds more than one
    // session tab) says no.
    void closeTab(int index);

    // Erases `session`'s tab and destroys its page, and with it the session's
    // viewport, without emitting tabCloseRequested(). The caller has already
    // decided the close; the session itself stays alive for the caller to erase.
    void removeSessionTab(DocumentSession* session);

    // Installs the "may a session tab close" predicate. MainWindow passes the all-groups rule
    // (total session tabs > 1). Call refreshCloseButtons() on every group whenever the answer
    // can change.
    void setCloseGuard(std::function<bool()> guard) { closeGuard_ = std::move(guard); }
    void refreshCloseButtons();

    // Focus ring: sets the dynamic property `focused` the theme keys its ring
    // rule on, and re-polishes. The border sits in a 1px layout margin that
    // setRingReserved() turns on only while the group shares the editor area,
    // so a lone group keeps its full size.
    void setFocused(bool focused);
    void setRingReserved(bool reserved);

    // Title of `session`'s tab, or an empty string.
    QString tabTitle(const DocumentSession* session) const;

    // Sets the visible text of `session`'s tab (no-op if it has none here).
    void setSessionTabTitle(DocumentSession* session, const QString& title);

    // Makes `session`'s tab current (emits currentChanged when it changes).
    void setCurrentSession(DocumentSession* session);

    // Tab index of `session`, or -1.
    int indexOf(const DocumentSession* session) const;

    DocumentSession* currentSession() const;
    DocumentSession* sessionAt(int index) const;
    int count() const { return static_cast<int>(tabs_.size()); }

    // Hides the tab bar only; the pages keep their geometry rules.
    void setTabStripVisible(bool visible);

signals:
    void currentChanged(DocumentSession* session);
    void tabCloseRequested(DocumentSession* session);
    void tabContextMenuRequested(int tabIndex, const QPoint& globalPos);
    void barBackgroundClicked();
    // Any press on the tab strip, tab or background: a request to focus this group.
    void focusRequested();

private:
    int indexOfPage(const QWidget* page) const;
    int sessionTabCount() const;
    void addCloseButton(int tabIndex, QWidget* page);
    bool sessionClosable() const;
    void syncStackToCurrent();

    EditorTabBar* tabBar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyHint_ = nullptr;  // stack page shown while no tab is open
    std::vector<Tab> tabs_;        // index-aligned with tabBar_'s tabs
    QVBoxLayout* layout_ = nullptr;
    std::function<bool()> closeGuard_;
};

}  // namespace plnr::ui
