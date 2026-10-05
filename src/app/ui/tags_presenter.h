#pragma once

#include <QAbstractButton>
#include <QComboBox>
#include <QListWidget>
#include <QObject>

#include <ordo/core/kernel.h>
#include <ordo/qt/presenter.h>

#include "agent/events.h"

namespace plnr::ui {

// Mirrors TagStore onto the Tray's Tags section (checkable list + "Add Tag"
// button) and the Entity Info tag-assign combo, kept synced to the live selection.
class TagsPresenter : public ordo::qt::Presenter {
public:
    TagsPresenter(QListWidget* tagsList, QAbstractButton* addTagButton,
                  QComboBox* entityTagCombo);

    void onRegister() override;

private:
    void onTagsChanged(const events::TagsChanged& event);
    void onSelectionChanged(const events::SelectionChanged& event);

    void onAddTagClicked();
    void onTagItemChanged(QListWidgetItem* item);
    void onComboActivated(int index);

    // Rebuilds tagsList_/entityTagCombo_ from TagStore (id in Qt::UserRole),
    // then resyncs the combo. Guarded by rebuilding_ against re-entrant sends.
    void rebuild();

    // Empty/mixed selection sets index -1 (renders blank).
    void syncComboToSelection();

    QListWidget* tagsList_;
    QAbstractButton* addTagButton_;
    QComboBox* entityTagCombo_;

    bool rebuilding_ = false;
};

}  // namespace plnr::ui
