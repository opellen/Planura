#pragma once

#include <QDockWidget>
#include <QString>
#include <QWidget>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace plnr::ui {

// One collapsible header + content pair inside the Default Tray, mirroring
// the reference modeler's inspector sections (Entity Info, Materials, ...).
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    QWidget* contentWidget() const { return content_; }
    void setExpanded(bool expanded);

private:
    void toggle();

    QToolButton* headerButton_;
    QWidget* content_;
};

// Right-hand "Default Tray" dock: a scrollable column of collapsible section
// stubs. Entity Info starts expanded, the rest start collapsed.
class Tray : public QDockWidget {
    Q_OBJECT

public:
    explicit Tray(QWidget* parent = nullptr);

    // EntityInfoPresenter sets this to reflect the live selection.
    QLabel* entityInfoLabel() const { return entityInfoLabel_; }

    // Tag-assign combo; TagsPresenter assigns the live selection on select.
    QComboBox* entityTagCombo() const { return entityTagCombo_; }

    // One checkable item per tag (checked = visible), kept in sync by TagsPresenter.
    QListWidget* tagsList() const { return tagsList_; }

    // Out-of-line (tray.cpp): the QToolButton*->QAbstractButton* upcast
    // needs QToolButton's complete type.
    QAbstractButton* addTagButton() const;

    // In-Model material list; click sets the active material.
    QListWidget* materialsList() const { return materialsList_; }
    // Opens a QColorDialog then sends MaterialCreateRequested.
    QAbstractButton* addMaterialButton() const;
    // Edit affordance for the ACTIVE material; disabled when none is active.
    QLineEdit* materialNameEdit() const { return materialNameEdit_; }
    QAbstractButton* materialColorButton() const;
    QSpinBox* materialOpacitySpin() const { return materialOpacitySpin_; }

    // Toggle-on opens a QFileDialog (cancel reverts, uncheck clears); tile
    // spins are model units, min 0.01, default 1.0.
    QAbstractButton* materialTextureCheck() const;
    QDoubleSpinBox* materialTileWSpin() const { return materialTileWSpin_; }
    QDoubleSpinBox* materialTileHSpin() const { return materialTileHSpin_; }

private:
    CollapsibleSection* addSection(QVBoxLayout* columnLayout, const QString& title, bool expanded);

    QLabel* entityInfoLabel_ = nullptr;
    QComboBox* entityTagCombo_ = nullptr;
    QListWidget* tagsList_ = nullptr;
    QToolButton* addTagButton_ = nullptr;
    QListWidget* materialsList_ = nullptr;
    QToolButton* addMaterialButton_ = nullptr;
    QLineEdit* materialNameEdit_ = nullptr;
    QToolButton* materialColorButton_ = nullptr;
    QSpinBox* materialOpacitySpin_ = nullptr;
    QCheckBox* materialTextureCheck_ = nullptr;
    QDoubleSpinBox* materialTileWSpin_ = nullptr;
    QDoubleSpinBox* materialTileHSpin_ = nullptr;
};

}  // namespace plnr::ui
