#pragma once

#include <QAbstractButton>
#include <QListWidget>
#include <QObject>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"
#include "document_session.h"

class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace plnr::agent {
class MaterialRepository;
}  // namespace plnr::agent

namespace plnr::ui {

// Mirrors MaterialRepository onto the Tray's Materials section: swatch list, "+"
// create button, and an edit affordance for the ACTIVE material.
// Known MVP gap: SetActiveMaterialRequested-only paths (e.g. Alt-sample)
// leave the panel stale until the next MaterialsChanged.
class MaterialsPresenter : public ordo::qt::Presenter {
public:
    MaterialsPresenter(QListWidget* materialsList, QAbstractButton* addMaterialButton,
                        QLineEdit* nameEdit, QAbstractButton* colorButton, QSpinBox* opacitySpin,
                        QAbstractButton* textureCheck, QDoubleSpinBox* tileWSpin, QDoubleSpinBox* tileHSpin,
                        UiSessionState* uiState);

    void onRegister() override;

private:
    void onMaterialsChanged(const events::MaterialsChanged& event);

    void onAddMaterialClicked();
    void onMaterialItemClicked(QListWidgetItem* item);
    void onColorButtonClicked();
    void onNameEditingFinished();
    void onOpacityChanged(int value);

    // Checked opens a QFileDialog (cancel reverts); unchecked sends an
    // empty path, which the command treats as clearing the texture.
    void onTextureCheckToggled(bool checked);

    // No-op unless the active material is textured and has a
    // uiState_->materialTexturePaths entry (needs the browsed file path to re-apply).
    void onTileChanged();

    // Rebuilds materialsList_ and the edit affordance from the active
    // material. Guarded by rebuilding_ against re-entrant sends.
    void rebuild();

    QListWidget* materialsList_;
    QAbstractButton* addMaterialButton_;
    QLineEdit* nameEdit_;
    QAbstractButton* colorButton_;
    QSpinBox* opacitySpin_;
    QAbstractButton* textureCheck_;
    QDoubleSpinBox* tileWSpin_;
    QDoubleSpinBox* tileHSpin_;

    // Non-owning; the session owns the state (last browsed texture path per
    // material id), so it survives presenter reconstruction. Materials
    // textured another way have no entry, so their tile spins stay disabled
    // until re-browsed.
    UiSessionState* uiState_;

    bool rebuilding_ = false;
};

}  // namespace plnr::ui
