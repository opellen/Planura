#include "agent/command/undo_commands.h"

#include <optional>

#include <ordo/core/kernel.h>

#include "agent/annotation_store.h"
#include "agent/axes_store.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/selection_store.h"
#include "agent/tag_store.h"
#include "agent/transaction_delta.h"
#include "agent/transaction.h"
#include "agent/undo_store.h"

namespace plnr::agent {

namespace {

// Same refresh-Fact sequence as document_commands.cpp (separate translation unit, so duplicated).
void dispatchRefreshFacts(ordo::core::CommandContext& context) {
    context.send(events::GeometryChanged{});
    context.send(events::TagsChanged{});
    context.send(events::GuidesChanged{});
    context.send(events::AnnotationsChanged{});
    context.send(events::SectionsChanged{});
    context.send(events::AxesChanged{});
    context.send(events::MaterialsChanged{});
    context.send(events::SelectionChanged{});
    context.send(events::EditContextChanged{});
}

// Shared tail of UndoCommand/RedoCommand: applies delta, resets selection/edit-context, dispatches refresh Facts, markDirty.
// geometry is required: callers check it before popping UndoStore so a delta is never dropped.
void applyAndSettle(ordo::core::CommandContext& context, GeometryApi& geometry, const TransactionDelta& delta,
                     ApplyDirection direction) {
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    auto guides = context.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = context.agentAs<SectionStore>(kSectionStoreName);
    auto axes = context.agentAs<AxesStore>(kAxesStoreName);
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);

    // No Transaction is active here, so replaying an op cannot re-record itself.
    applyTransactionDelta(geometry, tags.get(), guides.get(), annotations.get(), sections.get(), axes.get(),
                           materials.get(), delta, direction);

    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (selection) {
        selection->clear();
    }
    auto editContext = context.agentAs<EditContextStore>(kEditContextStoreName);
    if (editContext) {
        editContext->reset();
    }

    // GeometryChangedCommand pings UndoStore::notifyMutation() here; harmless since Undo/Redo are never UndoCaptureCommand-wrapped.
    dispatchRefreshFacts(context);

    auto document = context.agentAs<DocumentStore>(kDocumentStoreName);
    if (document) {
        document->markDirty();  // no save-point tracking
    }
}

}  // namespace

void UndoCommand::execute(const events::UndoRequested& /*event*/, ordo::core::CommandContext& context) {
    auto undo = context.agentAs<UndoStore>(kUndoStoreName);
    if (!undo || !undo->canUndo()) {
        return;  // nothing to undo, or no UndoStore registered
    }
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // nothing to apply the popped delta onto
    }

    const std::optional<TransactionDelta> delta = undo->takeUndo();
    if (!delta) {
        return;  // defensive -- canUndo() already checked
    }

    applyAndSettle(context, *geometry, *delta, ApplyDirection::Backward);
}

void RedoCommand::execute(const events::RedoRequested& /*event*/, ordo::core::CommandContext& context) {
    auto undo = context.agentAs<UndoStore>(kUndoStoreName);
    if (!undo || !undo->canRedo()) {
        return;  // nothing to redo, or no UndoStore registered
    }
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // nothing to apply the popped delta onto
    }

    const std::optional<TransactionDelta> delta = undo->takeRedo();
    if (!delta) {
        return;  // defensive -- canRedo() already checked
    }

    applyAndSettle(context, *geometry, *delta, ApplyDirection::Forward);
}

}  // namespace plnr::agent
