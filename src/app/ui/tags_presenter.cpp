#include "tags_presenter.h"

#include <QListWidgetItem>
#include <QString>
#include <QVariant>

#include "agent/selection_store.h"
#include "agent/tag_store.h"

namespace plnr::ui {

TagsPresenter::TagsPresenter(QListWidget* tagsList, QAbstractButton* addTagButton,
                              QComboBox* entityTagCombo)
    : Presenter(QStringLiteral("TagsPresenter"), tagsList),
      tagsList_(tagsList),
      addTagButton_(addTagButton),
      entityTagCombo_(entityTagCombo) {}

void TagsPresenter::onRegister() {
    subscribe<events::TagsChanged>(&TagsPresenter::onTagsChanged);
    subscribe<events::SelectionChanged>(&TagsPresenter::onSelectionChanged);

    connect(addTagButton_, &QAbstractButton::clicked, this, &TagsPresenter::onAddTagClicked);
    connect(tagsList_, &QListWidget::itemChanged, this, &TagsPresenter::onTagItemChanged);
    connect(entityTagCombo_, qOverload<int>(&QComboBox::activated), this, &TagsPresenter::onComboActivated);

    rebuild();  // covers tags/assignments that existed before this presenter registered
}

void TagsPresenter::onTagsChanged(const events::TagsChanged& /*event*/) {
    rebuild();
}

void TagsPresenter::onSelectionChanged(const events::SelectionChanged& /*event*/) {
    syncComboToSelection();
}

void TagsPresenter::onAddTagClicked() {
    context().send(events::TagCreateRequested{std::string()});
}

void TagsPresenter::onTagItemChanged(QListWidgetItem* item) {
    if (rebuilding_ || !item) return;

    const std::uint64_t tagId = item->data(Qt::UserRole).toULongLong();
    const bool visible = item->checkState() == Qt::Checked;
    context().send(events::TagVisibilityRequested{tagId, visible});
}

void TagsPresenter::onComboActivated(int index) {
    if (rebuilding_ || index < 0) return;

    auto selection = context().agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
    if (!selection) return;

    const std::uint64_t tagId = entityTagCombo_->itemData(index).toULongLong();
    context().send(events::TagAssignRequested{selection->items(), tagId});
}

void TagsPresenter::rebuild() {
    auto tags = context().agentAs<agent::TagStore>(agent::kTagStoreName);

    rebuilding_ = true;

    tagsList_->clear();
    entityTagCombo_->clear();

    if (tags) {
        for (const agent::Tag& tag : tags->tags()) {
            const QString name = QString::fromStdString(tag.name);

            auto* item = new QListWidgetItem(name, tagsList_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(tag.visible ? Qt::Checked : Qt::Unchecked);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(tag.id));

            entityTagCombo_->addItem(name, QVariant::fromValue<qulonglong>(tag.id));
        }
    }

    rebuilding_ = false;

    syncComboToSelection();
}

void TagsPresenter::syncComboToSelection() {
    auto tags = context().agentAs<agent::TagStore>(agent::kTagStoreName);
    auto selection = context().agentAs<agent::SelectionStore>(agent::kSelectionStoreName);

    int indexToSelect = -1;  // empty/mixed selection -> blank combo
    if (tags && selection && !selection->items().empty()) {
        const std::uint64_t firstTag = tags->tagOf(selection->items().front());
        bool uniform = true;
        for (const events::EntityRef& ref : selection->items()) {
            if (tags->tagOf(ref) != firstTag) {
                uniform = false;
                break;
            }
        }
        if (uniform) {
            for (int i = 0; i < entityTagCombo_->count(); ++i) {
                if (entityTagCombo_->itemData(i).toULongLong() == firstTag) {
                    indexToSelect = i;
                    break;
                }
            }
        }
    }

    rebuilding_ = true;
    entityTagCombo_->setCurrentIndex(indexToSelect);
    rebuilding_ = false;
}

}  // namespace plnr::ui
