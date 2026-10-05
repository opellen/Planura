#pragma once

// Ported from ordo-state-designer progress_overlay_widget.

#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace plnr::ui {

// Modal progress scrim with a centered card. Covers its parent;
// showOperation() arms a grace delay so an operation that finishes first never flashes it.
class ProgressOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ProgressOverlay(QWidget* parent = nullptr);
    ~ProgressOverlay() override = default;

    void showOperation(quint64 opId, const QString& title, const QString& message, bool cancelable = true);
    // percentage < 0 keeps the bar indeterminate. Safe before the grace delay has elapsed.
    void setProgress(int percentage, const QString& message);
    void hideOperation();

    quint64 currentOpId() const { return currentOpId_; }
    bool isCancelable() const { return cancelable_; }

signals:
    void cancelRequested(quint64 opId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateCardGeometry();
    void revealNow();

    quint64 currentOpId_ = 0;
    QString title_;
    QString statusMessage_;
    int percentage_ = -1;
    int spinnerAngle_ = 0;
    bool cancelable_ = false;
    QTimer spinnerTimer_;
    QTimer graceTimer_;

    QWidget* card_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
};

}  // namespace plnr::ui
