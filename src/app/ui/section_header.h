#pragma once

#include <QAbstractButton>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QVector>

namespace plnr::ui {

// Ghost icon button whose glyph is a painted plus (glyphs are painted, never text). Fixed to
// the row-gutter slot; hover/pressed use the ghost state fills.
class PlusGlyphButton : public QToolButton {
    Q_OBJECT

public:
    explicit PlusGlyphButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

// The one collapsible section header: a top hairline, an optional 16px identity icon, a Title
// Case title with an optional "(N)" count, section actions as ghost buttons left of the chevron,
// and a painted chevron. Clicking toggles the content widget's visibility; it is never reparented.
class SectionHeader : public QAbstractButton {
    Q_OBJECT

public:
    // iconSlug empty = no icon (the title moves to the left anchor). A non-collapsible header
    // draws no chevron and keeps its content visible.
    explicit SectionHeader(const QString& title, const QString& iconSlug = QString(),
                           bool collapsible = true, QWidget* parent = nullptr);

    // Widget whose visibility follows the expanded state.
    void setContent(QWidget* content);

    // Appends "(N)" to the title; a negative count removes the badge.
    void setCount(int count);

    // A section action in the header's right gutter, left of the chevron. The header takes
    // ownership and reparents the button into itself.
    void addHeaderAction(QToolButton* action);

    bool isExpanded() const { return expanded_; }
    void setExpanded(bool expanded);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void nextCheckState() override;

private:
    QString displayTitle() const;
    void placeActions();

    QString title_;
    QString iconSlug_;
    bool collapsible_;
    bool expanded_ = true;
    int count_ = -1;
    QPointer<QWidget> content_;
    QVector<QToolButton*> actions_;
};

}  // namespace plnr::ui
