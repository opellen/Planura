#include "agent/command/annotation_commands.h"

#include <ordo/core/kernel.h>

#include "agent/annotation_store.h"
#include "agent/document_store.h"
#include "agent/geometry_api.h"
#include "agent/command/selection_commands.h"
#include "agent/undo_store.h"

namespace plnr::agent {

void AddDimensionCommand::execute(const events::AddDimensionRequested& event, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!annotations || !geometry) return;  // AnnotationStore/GeometryApi not registered on this kernel.
    annotations->addDimension(event.vertexA, event.vertexB, event.offsetDir, event.offset, geometry->model());
}

void AddScreenTextCommand::execute(const events::AddScreenTextRequested& event, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->addScreenText(event.x, event.y, event.text);
}

void AddLeaderTextCommand::execute(const events::AddLeaderTextRequested& event, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->addLeaderText(event.anchor, event.target, event.text);
}

void SetAnnotationTextCommand::execute(const events::SetAnnotationTextRequested& event, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->setText(event.id, event.text);
}

void RemoveAnnotationCommand::execute(const events::RemoveAnnotationRequested& event, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->remove(event.id);
}

void RemoveAllAnnotationsCommand::execute(const events::RemoveAllAnnotationsRequested& /*event*/, ordo::core::CommandContext& context) {
    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->removeAll();
}

void GeometryChangedCommand::execute(const events::GeometryChanged& event, ordo::core::CommandContext& context) {
    // Replays SelectionStore pruning unchanged -- see this class's header comment.
    PruneSelectionCommand{}.execute(event, context);

    // Dirty/undo tracking must run before the early return below (see header).
    auto document = context.agentAs<DocumentStore>(kDocumentStoreName);
    if (document) document->markDirty();

    auto undo = context.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->notifyMutation();

    auto annotations = context.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!annotations || !geometry) return;  // Either Agent absent -- nothing to refresh.
    annotations->refreshAssociations(geometry->model());
}

}  // namespace plnr::agent
