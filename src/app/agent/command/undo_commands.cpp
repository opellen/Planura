#include "agent/command/undo_commands.h"

#include <optional>

#include <ordo/core/app_kernel.h>

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
#include "agent/transaction_manager.h"
#include "agent/undo_store.h"

namespace plnr::agent {

namespace {

// Same refresh-Fact sequence as document_commands.cpp's dispatchRefreshFacts
// -- duplicated here since undo_commands.h deliberately does not include
// document_commands.h (different translation unit's anonymous namespace).
void dispatchRefreshFacts(ordo::core::AppKernel& kernel) {
    kernel.send(events::GeometryChanged{});
    kernel.send(events::TagsChanged{});
    kernel.send(events::GuidesChanged{});
    kernel.send(events::AnnotationsChanged{});
    kernel.send(events::SectionsChanged{});
    kernel.send(events::AxesChanged{});
    kernel.send(events::MaterialsChanged{});
    kernel.send(events::SelectionChanged{});
    kernel.send(events::EditContextChanged{});
}

// Shared tail of UndoCommand/RedoCommand: applies delta in the given
// direction via applyTransactionDelta, then the selection/edit-context
// reset + refresh-Facts + markDirty sequence both commands need. geometry
// is required -- callers check for it before popping from UndoStore, so a
// delta is never silently dropped.
void applyAndSettle(ordo::core::AppKernel& kernel, GeometryApi& geometry, const TransactionDelta& delta,
                     ApplyDirection direction) {
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    auto guides = kernel.agentAs<GuideStore>(kGuideStoreName);
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto sections = kernel.agentAs<SectionStore>(kSectionStoreName);
    auto axes = kernel.agentAs<AxesStore>(kAxesStoreName);
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);

    // No TransactionManager exists on this call path, so no journal is
    // attached anywhere right now -- replaying a captured op can never
    // re-record itself, structurally (there is nothing to notify).
    applyTransactionDelta(geometry, tags.get(), guides.get(), annotations.get(), sections.get(), axes.get(),
                           materials.get(), delta, direction);

    auto selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
    if (selection) {
        selection->clear();
    }
    auto editContext = kernel.agentAs<EditContextStore>(kEditContextStoreName);
    if (editContext) {
        editContext->reset();
    }

    // GeometryChanged/etc. below ripple through GeometryChangedCommand's
    // UndoStore::notifyMutation() ping same as any mutation -- harmless
    // here since UndoCommand/RedoCommand are never wrapped in
    // UndoCaptureCommand, so no transaction is ever active to receive it.
    dispatchRefreshFacts(kernel);

    auto document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
    if (document) {
        document->markDirty();
    }
}

}  // namespace

void UndoCommand::execute(ordo::core::AppKernel& kernel, const events::UndoRequested& /*event*/) {
    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    if (!undo || !undo->canUndo()) {
        return;  // nothing to undo, or no UndoStore registered
    }
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // nothing to apply the popped delta onto
    }

    const std::optional<TransactionDelta> delta = undo->takeUndo();
    if (!delta) {
        return;  // defensive -- canUndo() already checked
    }

    applyAndSettle(kernel, *geometry, *delta, ApplyDirection::Backward);
}

void RedoCommand::execute(ordo::core::AppKernel& kernel, const events::RedoRequested& /*event*/) {
    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    if (!undo || !undo->canRedo()) {
        return;  // nothing to redo, or no UndoStore registered
    }
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // nothing to apply the popped delta onto
    }

    const std::optional<TransactionDelta> delta = undo->takeRedo();
    if (!delta) {
        return;  // defensive -- canRedo() already checked
    }

    applyAndSettle(kernel, *geometry, *delta, ApplyDirection::Forward);
}

}  // namespace plnr::agent
