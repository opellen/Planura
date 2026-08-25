#include "tray.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace plnr::ui {

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent) : QWidget(parent) {
    headerButton_ = new QToolButton(this);
    headerButton_->setText(title);
    headerButton_->setCheckable(true);
    headerButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    headerButton_->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: bold; }"));

    content_ = new QWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(headerButton_);
    layout->addWidget(content_);

    connect(headerButton_, &QToolButton::clicked, this, &CollapsibleSection::toggle);

    setExpanded(false);  // callers opt individual sections in via setExpanded(true)
}

void CollapsibleSection::setExpanded(bool expanded) {
    content_->setVisible(expanded);
    headerButton_->setChecked(expanded);
    headerButton_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
}

void CollapsibleSection::toggle() {
    setExpanded(!content_->isVisible());
}

Tray::Tray(QWidget* parent) : QDockWidget(QStringLiteral("Default Tray"), parent) {
    setObjectName(QStringLiteral("defaultTray"));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    auto* column = new QWidget(this);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(4, 4, 4, 4);
    columnLayout->setSpacing(2);

    CollapsibleSection* entityInfo = addSection(columnLayout, QStringLiteral("Entity Info"), true);
    auto* entityInfoLayout = new QVBoxLayout(entityInfo->contentWidget());
    entityInfoLabel_ = new QLabel(QStringLiteral("No Selection"), entityInfo->contentWidget());
    entityInfoLayout->addWidget(entityInfoLabel_);
    // Tag-assign affordance: TagsPresenter populates this with one entry per
    // tag; picking one assigns the live selection to it.
    entityTagCombo_ = new QComboBox(entityInfo->contentWidget());
    entityInfoLayout->addWidget(entityTagCombo_);

    // Materials section: MaterialsPresenter drives all five widgets; Tray
    // stays kernel-ignorant and only exposes them via accessors, same split
    // as the Tags section below.
    CollapsibleSection* materials = addSection(columnLayout, QStringLiteral("Materials"), false);
    auto* materialsLayout = new QVBoxLayout(materials->contentWidget());
    materialsList_ = new QListWidget(materials->contentWidget());
    materialsLayout->addWidget(materialsList_);
    addMaterialButton_ = new QToolButton(materials->contentWidget());
    addMaterialButton_->setText(QStringLiteral("+"));
    materialsLayout->addWidget(addMaterialButton_);

    // Edit affordance for the active material: name field + color-swatch
    // button on one row, opacity spin box below.
    auto* materialEditRow = new QHBoxLayout();
    materialNameEdit_ = new QLineEdit(materials->contentWidget());
    materialEditRow->addWidget(materialNameEdit_);
    materialColorButton_ = new QToolButton(materials->contentWidget());
    materialColorButton_->setText(QStringLiteral("Color"));
    materialEditRow->addWidget(materialColorButton_);
    materialsLayout->addLayout(materialEditRow);

    materialOpacitySpin_ = new QSpinBox(materials->contentWidget());
    materialOpacitySpin_->setRange(0, 100);
    materialOpacitySpin_->setSuffix(QStringLiteral("%"));
    materialOpacitySpin_->setValue(100);
    materialsLayout->addWidget(materialOpacitySpin_);

    // Texture row: checkbox's own toggle-on IS the browse gesture --
    // MaterialsPresenter opens a QFileDialog when it flips to checked
    // (checkbox + tile spins only; aspect-link deferred). Tile spin boxes:
    // model units, min 0.01, default 1.0.
    materialTextureCheck_ = new QCheckBox(QStringLiteral("Use texture image"), materials->contentWidget());
    materialsLayout->addWidget(materialTextureCheck_);

    auto* materialTileRow = new QHBoxLayout();
    materialTileRow->addWidget(new QLabel(QStringLiteral("Tile:"), materials->contentWidget()));
    materialTileWSpin_ = new QDoubleSpinBox(materials->contentWidget());
    materialTileWSpin_->setRange(0.01, 1000.0);
    materialTileWSpin_->setDecimals(2);
    materialTileWSpin_->setValue(1.0);
    materialTileRow->addWidget(materialTileWSpin_);
    materialTileRow->addWidget(new QLabel(QStringLiteral("x"), materials->contentWidget()));
    materialTileHSpin_ = new QDoubleSpinBox(materials->contentWidget());
    materialTileHSpin_->setRange(0.01, 1000.0);
    materialTileHSpin_->setDecimals(2);
    materialTileHSpin_->setValue(1.0);
    materialTileRow->addWidget(materialTileHSpin_);
    materialsLayout->addLayout(materialTileRow);

    addSection(columnLayout, QStringLiteral("Components"), false);
    addSection(columnLayout, QStringLiteral("Styles"), false);
    addSection(columnLayout, QStringLiteral("Environments"), false);

    // Tags section (the reference modeler "Layers"): a checkbox list (checked = visible)
    // plus an "Add Tag" button. TagsPresenter drives both; Tray stays
    // kernel-ignorant and only exposes the widgets via accessors.
    CollapsibleSection* tags = addSection(columnLayout, QStringLiteral("Tags"), false);
    auto* tagsLayout = new QVBoxLayout(tags->contentWidget());
    tagsList_ = new QListWidget(tags->contentWidget());
    tagsLayout->addWidget(tagsList_);
    addTagButton_ = new QToolButton(tags->contentWidget());
    addTagButton_->setText(QStringLiteral("Add Tag"));
    tagsLayout->addWidget(addTagButton_);

    addSection(columnLayout, QStringLiteral("Shadows"), false);
    addSection(columnLayout, QStringLiteral("Scenes"), false);
    addSection(columnLayout, QStringLiteral("Instructor"), false);

    columnLayout->addStretch(1);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(column);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setWidget(scrollArea);
}

QAbstractButton* Tray::addTagButton() const {
    return addTagButton_;
}

QAbstractButton* Tray::addMaterialButton() const {
    return addMaterialButton_;
}

QAbstractButton* Tray::materialColorButton() const {
    return materialColorButton_;
}

QAbstractButton* Tray::materialTextureCheck() const {
    return materialTextureCheck_;
}

CollapsibleSection* Tray::addSection(QVBoxLayout* columnLayout, const QString& title, bool expanded) {
    auto* section = new CollapsibleSection(title, this);
    section->setExpanded(expanded);
    columnLayout->addWidget(section);
    return section;
}

}  // namespace plnr::ui
