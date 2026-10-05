#pragma once

#include <QList>
#include <QTimer>
#include <QToolButton>

class QAction;

namespace plnr::ui {

// Rail slot for a tool family. The face is one variant's QAction (the button's default action,
// so icon/tooltip/checked state follow it). Short click triggers the face; a long press or a
// click on the bottom-right corner opens a palette of every variant, and picking one triggers
// it and makes it the face. Variants stay caller-owned. Only checkable variants become the face
// (a dialog-opening action like 3D Text is triggerable but never replaces it), and the face
// follows whichever checkable variant becomes checked.
class ToolFlyoutButton : public QToolButton {
    Q_OBJECT

public:
    // variants must be non-empty; the first is the initial face unless a
    // checkable one is already checked.
    explicit ToolFlyoutButton(const QList<QAction*>& variants, QWidget* parent = nullptr);

    QAction* face() const { return face_; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setFace(QAction* action);
    void openPalette();
    bool inCorner(const QPoint& pos) const;

    QList<QAction*> variants_;
    QAction* face_ = nullptr;
    QTimer longPress_;
    // The press already opened the palette; its release must not click the face.
    bool pressConsumed_ = false;
};

}  // namespace plnr::ui
