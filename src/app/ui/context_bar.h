#pragma once

#include <map>
#include <optional>

#include <QString>
#include <QToolBar>

#include "agent/events.h"
#include "ui/activity_rail.h"

class QAction;
class QIcon;
class QLabel;
class QSpinBox;
class QToolButton;

namespace plnr::ui {

// Top options bar: the active tool's face (icon + name), its segment count ("Sides") when
// it has one, the document's units, and the Perspective/Parallel toggles. A passive view:
// it holds no kernel; ContextBarPresenter feeds it and turns spinner edits into events.
class ContextBar : public QToolBar {
    Q_OBJECT

public:
    explicit ContextBar(QWidget* parent = nullptr);

    // Binds the toolbuttons to the Camera menu's existing QActions; the actions
    // stay owned (and kept checked) elsewhere.
    void setProjectionActions(QAction* perspective, QAction* parallel);

    void setToolFace(const QIcon& icon, const QString& name);

    // Edit shows the tool cluster (face, Sides); other modes hide all of it. The
    // units/projection cluster stays in every mode (camera is mode-neutral).
    void setMode(ShellMode mode);

    // Records whether the active tool reports segments; the "Sides" spinner shows
    // only when this and the mode both allow it. Hidden until a tool reports.
    void setSegmentsVisible(bool visible);
    // Sets the spinner without emitting segmentsEdited.
    void setSegmentsValue(int value);
    int segmentsValue() const;
    // Whether the "Sides" spinner is currently shown (tool wants it and mode allows).
    bool segmentsShown() const;

    void setUnits(const QString& units);
    // Read-only views of the face name and units label text (dev bridge).
    QString toolName() const;
    QString unitsText() const;

    // App-global per-tool option state: the last segment count reported for `tool`,
    // surviving document focus switches.
    void rememberSegments(events::ToolId tool, int segments) { segmentsCache_[tool] = segments; }
    std::optional<int> lastSegments(events::ToolId tool) const;

signals:
    // The user committed a spinner value (not emitted by setSegmentsValue).
    void segmentsEdited(int segments);

private:
    QLabel* toolIcon_ = nullptr;
    QLabel* toolName_ = nullptr;
    QWidget* segmentsBox_ = nullptr;
    QAction* segmentsAction_ = nullptr;
    // Tool cluster actions (face icon, name, separator) hidden outside Edit.
    QAction* toolIconAction_ = nullptr;
    QAction* toolNameAction_ = nullptr;
    QAction* toolSeparatorAction_ = nullptr;
    ShellMode mode_ = ShellMode::Edit;
    bool segmentsWanted_ = false;
    QSpinBox* segmentsSpin_ = nullptr;
    QLabel* unitsLabel_ = nullptr;
    QToolButton* perspectiveButton_ = nullptr;
    QToolButton* parallelButton_ = nullptr;
    std::map<events::ToolId, int> segmentsCache_;
};

}  // namespace plnr::ui
