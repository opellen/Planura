#include "materials_presenter.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QIcon>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QPixmap>
#include <QSpinBox>
#include <QVariant>

#include "agent/material_repository.h"

namespace plnr::ui {

namespace {

constexpr int kSwatchSize = 16;

QPixmap swatchPixmap(double r, double g, double b) {
    QPixmap pixmap(kSwatchSize, kSwatchSize);
    const auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
    pixmap.fill(QColor::fromRgbF(clamp01(r), clamp01(g), clamp01(b)));
    return pixmap;
}

QString swatchStyleSheet(double r, double g, double b) {
    const auto to255 = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return QStringLiteral("background-color: rgb(%1,%2,%3);").arg(to255(r)).arg(to255(g)).arg(to255(b));
}

}  // namespace

MaterialsPresenter::MaterialsPresenter(QListWidget* materialsList,
                                        QAbstractButton* addMaterialButton, QLineEdit* nameEdit,
                                        QAbstractButton* colorButton, QSpinBox* opacitySpin,
                                        QAbstractButton* textureCheck, QDoubleSpinBox* tileWSpin,
                                        QDoubleSpinBox* tileHSpin, UiSessionState* uiState)
    : Presenter(QStringLiteral("MaterialsPresenter"), materialsList),
      materialsList_(materialsList),
      addMaterialButton_(addMaterialButton),
      nameEdit_(nameEdit),
      colorButton_(colorButton),
      opacitySpin_(opacitySpin),
      textureCheck_(textureCheck),
      tileWSpin_(tileWSpin),
      tileHSpin_(tileHSpin),
      uiState_(uiState) {}

void MaterialsPresenter::onRegister() {
    subscribe<events::MaterialsChanged>(&MaterialsPresenter::onMaterialsChanged);

    connect(addMaterialButton_, &QAbstractButton::clicked, this, &MaterialsPresenter::onAddMaterialClicked);
    connect(materialsList_, &QListWidget::itemClicked, this, &MaterialsPresenter::onMaterialItemClicked);
    connect(colorButton_, &QAbstractButton::clicked, this, &MaterialsPresenter::onColorButtonClicked);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &MaterialsPresenter::onNameEditingFinished);
    connect(opacitySpin_, qOverload<int>(&QSpinBox::valueChanged), this, &MaterialsPresenter::onOpacityChanged);
    connect(textureCheck_, &QAbstractButton::toggled, this, &MaterialsPresenter::onTextureCheckToggled);
    connect(tileWSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MaterialsPresenter::onTileChanged);
    connect(tileHSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MaterialsPresenter::onTileChanged);

    rebuild();  // covers materials that existed before this presenter registered
}

void MaterialsPresenter::onMaterialsChanged(const events::MaterialsChanged& /*event*/) {
    rebuild();
}

void MaterialsPresenter::onAddMaterialClicked() {
    const QColor color =
        QColorDialog::getColor(Qt::gray, materialsList_->window(), QStringLiteral("Create Material"));  // color-literal-ok(color-picker seed, user-data default)
    if (!color.isValid()) return;  // cancelled

    // Empty name auto-names it "Material N" -- MaterialRepository::create's own
    // job, not this presenter's.
    context().send(events::MaterialCreateRequested{std::string(), color.redF(), color.greenF(), color.blueF(), 1.0});
}

void MaterialsPresenter::onMaterialItemClicked(QListWidgetItem* item) {
    if (rebuilding_ || !item) return;

    const geo::Id id = static_cast<geo::Id>(item->data(Qt::UserRole).toULongLong());
    context().send(events::SetActiveMaterialRequested{id});
    // setActive() dispatches nothing (see this class's header comment) --
    // rebuild manually so the highlight/edit affordance reflect the newly
    // active material immediately.
    rebuild();
}

void MaterialsPresenter::onColorButtonClicked() {
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!materials) return;
    const agent::Material* active = materials->material(materials->activeMaterialId());
    if (!active) return;

    const QColor initial = QColor::fromRgbF(active->r, active->g, active->b);
    const QColor color = QColorDialog::getColor(initial, materialsList_->window(), QStringLiteral("Material Color"));
    if (!color.isValid()) return;  // cancelled

    context().send(events::MaterialEditRequested{active->id, active->name, color.redF(), color.greenF(),
                                                 color.blueF(), active->opacity});
}

void MaterialsPresenter::onNameEditingFinished() {
    if (rebuilding_) return;
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!materials) return;
    const agent::Material* active = materials->material(materials->activeMaterialId());
    if (!active) return;

    context().send(events::MaterialEditRequested{active->id, nameEdit_->text().toStdString(), active->r, active->g,
                                                 active->b, active->opacity});
}

void MaterialsPresenter::onOpacityChanged(int value) {
    if (rebuilding_) return;
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!materials) return;
    const agent::Material* active = materials->material(materials->activeMaterialId());
    if (!active) return;

    context().send(events::MaterialEditRequested{active->id, active->name, active->r, active->g, active->b,
                                                 static_cast<double>(value) / 100.0});
}

void MaterialsPresenter::onTextureCheckToggled(bool checked) {
    if (rebuilding_) return;
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!materials) return;
    const agent::Material* active = materials->material(materials->activeMaterialId());
    if (!active) return;

    if (!checked) {
        // Clear -- the command ignores tileW/tileH on an empty path, so the
        // current spin values are sent verbatim for simplicity, not because
        // they matter here.
        context().send(
            events::MaterialSetTextureRequested{active->id, std::string(), tileWSpin_->value(), tileHSpin_->value()});
        return;
    }

    // Checked: this IS the browse gesture. A cancelled dialog must not leave
    // the checkbox checked with nothing applied -- revert it, guarded so the
    // revert itself doesn't re-enter this handler.
    const QString path = QFileDialog::getOpenFileName(materialsList_->window(), QStringLiteral("Choose Texture Image"),
                                                        QString(), QStringLiteral("Images (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) {
        rebuilding_ = true;
        textureCheck_->setChecked(false);
        rebuilding_ = false;
        return;
    }

    const std::string pathStd = path.toStdString();
    uiState_->materialTexturePaths[active->id] = pathStd;
    context().send(events::MaterialSetTextureRequested{active->id, pathStd, tileWSpin_->value(), tileHSpin_->value()});
}

void MaterialsPresenter::onTileChanged() {
    if (rebuilding_) return;
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    if (!materials) return;
    const agent::Material* active = materials->material(materials->activeMaterialId());
    if (!active || active->assetHash.empty()) return;  // untextured -- spins are disabled, but guard anyway

    // No cached path this session -- can't re-apply without the original
    // file (see uiState_'s comment (materialTexturePaths)). rebuild()'s own enablement
    // logic already keeps the spins disabled here; this is a defensive no-op.
    const auto it = uiState_->materialTexturePaths.find(active->id);
    if (it == uiState_->materialTexturePaths.end()) return;

    context().send(events::MaterialSetTextureRequested{active->id, it->second, tileWSpin_->value(), tileHSpin_->value()});
}

void MaterialsPresenter::rebuild() {
    auto materials = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);

    rebuilding_ = true;

    materialsList_->clear();

    QListWidgetItem* activeItem = nullptr;
    const agent::Material* active = nullptr;
    if (materials) {
        const geo::Id activeId = materials->activeMaterialId();
        for (const agent::Material& m : materials->materials()) {
            auto* item = new QListWidgetItem(QString::fromStdString(m.name), materialsList_);
            item->setIcon(QIcon(swatchPixmap(m.r, m.g, m.b)));
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(m.id));
            if (m.id == activeId) {
                activeItem = item;
                active = &m;
            }
        }
    }
    materialsList_->setCurrentItem(activeItem);

    const bool hasActive = active != nullptr;
    nameEdit_->setEnabled(hasActive);
    colorButton_->setEnabled(hasActive);
    opacitySpin_->setEnabled(hasActive);
    if (hasActive) {
        nameEdit_->setText(QString::fromStdString(active->name));
        colorButton_->setStyleSheet(swatchStyleSheet(active->r, active->g, active->b));
        opacitySpin_->setValue(static_cast<int>(std::lround(active->opacity * 100.0)));
    } else {
        nameEdit_->clear();
        colorButton_->setStyleSheet(QString());
        opacitySpin_->setValue(100);
    }

    // Texture row: checkbox mirrors assetHash's empty/non-empty state; tile
    // spins mirror tileW/tileH but stay disabled unless this session has a
    // cached browse path for THIS material (see uiState_'s comment (materialTexturePaths)).
    const bool hasTexture = hasActive && !active->assetHash.empty();
    const bool tileEditable = hasTexture && uiState_->materialTexturePaths.count(active->id) > 0;
    textureCheck_->setEnabled(hasActive);
    textureCheck_->setChecked(hasTexture);
    tileWSpin_->setEnabled(tileEditable);
    tileHSpin_->setEnabled(tileEditable);
    if (hasActive) {
        tileWSpin_->setValue(active->tileW);
        tileHSpin_->setValue(active->tileH);
    } else {
        tileWSpin_->setValue(1.0);
        tileHSpin_->setValue(1.0);
    }

    rebuilding_ = false;
}

}  // namespace plnr::ui
