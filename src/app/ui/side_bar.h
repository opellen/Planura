#pragma once

#include <QScrollArea>
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

namespace plnr::ui {

// Materials view: the widget set MaterialsPresenter drives. Kernel-ignorant;
// only exposes the controls via accessors.
class MaterialsView : public QWidget {
    Q_OBJECT

public:
    explicit MaterialsView(QWidget* parent = nullptr);

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
    QListWidget* materialsList_ = nullptr;
    QToolButton* addMaterialButton_ = nullptr;
    QLineEdit* materialNameEdit_ = nullptr;
    QToolButton* materialColorButton_ = nullptr;
    QSpinBox* materialOpacitySpin_ = nullptr;
    QCheckBox* materialTextureCheck_ = nullptr;
    QDoubleSpinBox* materialTileWSpin_ = nullptr;
    QDoubleSpinBox* materialTileHSpin_ = nullptr;
};

// Tags view: a checkbox list (checked = visible) plus an Add Tag button,
// both driven by TagsPresenter.
class TagsView : public QWidget {
    Q_OBJECT

public:
    explicit TagsView(QWidget* parent = nullptr);

    QListWidget* tagsList() const { return tagsList_; }
    // Out-of-line: the QToolButton* -> QAbstractButton* upcast needs the complete type.
    QAbstractButton* addTagButton() const;

private:
    QListWidget* tagsList_ = nullptr;
    QToolButton* addTagButton_ = nullptr;
};

// Styles view: empty-state stub until style controls land.
class StylesView : public QWidget {
    Q_OBJECT

public:
    explicit StylesView(QWidget* parent = nullptr);
};

// Entity Info view: the selection's info label and the tag-assign combo.
class EntityInfoView : public QWidget {
    Q_OBJECT

public:
    explicit EntityInfoView(QWidget* parent = nullptr);

    // EntityInfoPresenter sets this to reflect the live selection.
    QLabel* entityInfoLabel() const { return entityInfoLabel_; }
    // Tag-assign combo; TagsPresenter assigns the live selection on select.
    QComboBox* entityTagCombo() const { return entityTagCombo_; }

private:
    QLabel* entityInfoLabel_ = nullptr;
    QComboBox* entityTagCombo_ = nullptr;
};

// Right tray: one scrollable column of sections, top to bottom Entity Info,
// Materials, Tags, Styles.
class RightTray : public QScrollArea {
    Q_OBJECT

public:
    explicit RightTray(QWidget* parent = nullptr);

    EntityInfoView* entityInfoView() const { return entityInfoView_; }
    MaterialsView* materialsView() const { return materialsView_; }
    TagsView* tagsView() const { return tagsView_; }
    StylesView* stylesView() const { return stylesView_; }

private:
    EntityInfoView* entityInfoView_ = nullptr;
    MaterialsView* materialsView_ = nullptr;
    TagsView* tagsView_ = nullptr;
    StylesView* stylesView_ = nullptr;
};

}  // namespace plnr::ui
