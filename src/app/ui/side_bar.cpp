#include "ui/side_bar.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "constants/design_tokens.h"
#include "ui/section_header.h"

namespace plnr::ui {

namespace {

// Section body: row container with the side inset and the space-4 bottom margin an expanded
// section adds (a hidden body takes it along).
QWidget* makeSectionBody(QWidget* parent) {
    auto* body = new QWidget(parent);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(design::kSpace2, 0, design::kSpace2, design::kSpace4);
    layout->setSpacing(design::kSpace2);
    return body;
}

// One row on the shared axis: fixed type-label column, a stretching value field, and an
// optional right-gutter slot (design-grammar §6).
void addRow(QVBoxLayout* rows, QWidget* parent, const QString& labelText, QWidget* field,
            QWidget* gutter = nullptr) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(design::kSpace2);
    auto* label = new QLabel(labelText, row);
    label->setStyleSheet(design::kTypeLabel);
    label->setFixedWidth(design::kRowLabelColumnWidth);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(label);
    field->setParent(row);
    layout->addWidget(field, 1);
    // A fixed-size field (e.g. the Color QToolButton) must still start at the
    // shared value-column x instead of centering in the stretched cell
    // (design-grammar §6).
    if (field->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed ||
        qobject_cast<QToolButton*>(field) != nullptr) {
        layout->setAlignment(field, Qt::AlignLeft);
    }
    if (gutter != nullptr) {
        gutter->setParent(row);
        gutter->setFixedSize(design::kRowGutterSlot, design::kRowGutterSlot);
        layout->addWidget(gutter);
    }
    rows->addWidget(row);
}

// Keeps the header's "(N)" badge equal to the list's row count.
void bindCount(SectionHeader* header, QListWidget* list) {
    auto refresh = [header, list]() { header->setCount(list->count()); };
    QAbstractItemModel* model = list->model();
    QObject::connect(model, &QAbstractItemModel::rowsInserted, header, refresh);
    QObject::connect(model, &QAbstractItemModel::rowsRemoved, header, refresh);
    QObject::connect(model, &QAbstractItemModel::modelReset, header, refresh);
    refresh();
}

}  // namespace

MaterialsView::MaterialsView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Section 1: the material list; the add button is the header action.
    auto* listHeader = new SectionHeader(QStringLiteral("Materials"), QStringLiteral("tb_paint"), true, this);
    QWidget* listBody = makeSectionBody(this);
    layout->addWidget(listHeader);
    layout->addWidget(listBody, 1);
    materialsList_ = new QListWidget(listBody);
    listBody->layout()->addWidget(materialsList_);
    addMaterialButton_ = new PlusGlyphButton();
    addMaterialButton_->setToolTip(QStringLiteral("Add Material"));
    listHeader->addHeaderAction(addMaterialButton_);
    listHeader->setContent(listBody);
    bindCount(listHeader, materialsList_);

    // Section 2: edit affordance for the active material, as grammar rows.
    auto* editHeader = new SectionHeader(QStringLiteral("Edit Material"), QString(), true, this);
    QWidget* editBody = makeSectionBody(this);
    layout->addWidget(editHeader);
    layout->addWidget(editBody);
    editHeader->setContent(editBody);
    auto* rows = static_cast<QVBoxLayout*>(editBody->layout());

    materialNameEdit_ = new QLineEdit(editBody);
    addRow(rows, editBody, QStringLiteral("Name"), materialNameEdit_);

    materialColorButton_ = new QToolButton(editBody);
    materialColorButton_->setText(QStringLiteral("Color"));
    addRow(rows, editBody, QStringLiteral("Color"), materialColorButton_);

    materialOpacitySpin_ = new QSpinBox(editBody);
    materialOpacitySpin_->setRange(0, 100);
    materialOpacitySpin_->setSuffix(QStringLiteral("%"));
    materialOpacitySpin_->setValue(100);
    addRow(rows, editBody, QStringLiteral("Opacity"), materialOpacitySpin_);

    // The checkbox's own toggle-on IS the browse gesture: MaterialsPresenter
    // opens a QFileDialog when it flips to checked (aspect-link deferred).
    materialTextureCheck_ = new QCheckBox(QStringLiteral("Use texture image"), editBody);
    addRow(rows, editBody, QStringLiteral("Texture"), materialTextureCheck_);

    auto* tileField = new QWidget(editBody);
    auto* tileLayout = new QHBoxLayout(tileField);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(design::kSpace1);
    materialTileWSpin_ = new QDoubleSpinBox(tileField);
    materialTileWSpin_->setRange(0.01, 1000.0);
    materialTileWSpin_->setDecimals(2);
    materialTileWSpin_->setValue(1.0);
    tileLayout->addWidget(materialTileWSpin_);
    materialTileHSpin_ = new QDoubleSpinBox(tileField);
    materialTileHSpin_->setRange(0.01, 1000.0);
    materialTileHSpin_->setDecimals(2);
    materialTileHSpin_->setValue(1.0);
    tileLayout->addWidget(materialTileHSpin_);
    addRow(rows, editBody, QStringLiteral("Tile W/H"), tileField);
}

QAbstractButton* MaterialsView::addMaterialButton() const {
    return addMaterialButton_;
}

QAbstractButton* MaterialsView::materialColorButton() const {
    return materialColorButton_;
}

QAbstractButton* MaterialsView::materialTextureCheck() const {
    return materialTextureCheck_;
}

TagsView::TagsView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new SectionHeader(QStringLiteral("Tags"), QStringLiteral("tb_label"), true, this);
    QWidget* body = makeSectionBody(this);
    layout->addWidget(header);
    layout->addWidget(body, 1);
    tagsList_ = new QListWidget(body);
    body->layout()->addWidget(tagsList_);
    addTagButton_ = new PlusGlyphButton();
    addTagButton_->setToolTip(QStringLiteral("Add Tag"));
    header->addHeaderAction(addTagButton_);
    header->setContent(body);
    bindCount(header, tagsList_);
}

QAbstractButton* TagsView::addTagButton() const {
    return addTagButton_;
}

StylesView::StylesView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new SectionHeader(QStringLiteral("Styles"), QStringLiteral("tb_shaded"), true, this);
    QWidget* body = makeSectionBody(this);
    layout->addWidget(header);
    layout->addWidget(body);
    header->setContent(body);
    auto* hint = new QLabel(QStringLiteral("No style controls yet"), body);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(design::resolveRoles(
        QStringLiteral("color: {text-disabled}; font-size: %1px;").arg(design::kTypeSizePx)));
    body->layout()->addWidget(hint);
}

EntityInfoView::EntityInfoView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new SectionHeader(QStringLiteral("Entity Info"), QString(), true, this);
    QWidget* body = makeSectionBody(this);
    layout->addWidget(header);
    layout->addWidget(body);
    header->setContent(body);
    auto* rows = static_cast<QVBoxLayout*>(body->layout());
    // Full-width info line, type-value styling.
    entityInfoLabel_ = new QLabel(QStringLiteral("No Selection"), body);
    entityInfoLabel_->setStyleSheet(design::kTypeValue);
    entityInfoLabel_->setWordWrap(true);
    rows->addWidget(entityInfoLabel_);
    // Tag-assign affordance: TagsPresenter populates this with one entry per
    // tag; picking one assigns the live selection to it.
    entityTagCombo_ = new QComboBox(body);
    addRow(rows, body, QStringLiteral("Tag"), entityTagCombo_);
}

RightTray::RightTray(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("rightTray"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* column = new QWidget();
    column->setObjectName(QStringLiteral("rightTrayColumn"));
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, design::kSpace2, 0, design::kSpace2);
    layout->setSpacing(design::kSpace2);
    entityInfoView_ = new EntityInfoView(column);
    materialsView_ = new MaterialsView(column);
    tagsView_ = new TagsView(column);
    stylesView_ = new StylesView(column);
    layout->addWidget(entityInfoView_);
    layout->addWidget(materialsView_);
    layout->addWidget(tagsView_);
    layout->addWidget(stylesView_);
    layout->addStretch(1);
    setWidget(column);
}

}  // namespace plnr::ui
