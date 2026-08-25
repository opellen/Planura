#include "agent/command/annotation_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/annotation_store.h"
#include "agent/document_store.h"
#include "agent/geometry_api.h"
#include "agent/command/selection_commands.h"
#include "agent/undo_store.h"

namespace plnr::agent {

void AddDimensionCommand::execute(ordo::core::AppKernel& kernel, const events::AddDimensionRequested& event) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!annotations || !geometry) return;  // AnnotationStore/GeometryApi not registered on this kernel.
    annotations->addDimension(event.vertexA, event.vertexB, event.offsetDir, event.offset, geometry->model());
}

void AddScreenTextCommand::execute(ordo::core::AppKernel& kernel, const events::AddScreenTextRequested& event) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->addScreenText(event.x, event.y, event.text);
}

void AddLeaderTextCommand::execute(ordo::core::AppKernel& kernel, const events::AddLeaderTextRequested& event) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->addLeaderText(event.anchor, event.target, event.text);
}

void SetAnnotationTextCommand::execute(ordo::core::AppKernel& kernel, const events::SetAnnotationTextRequested& event) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->setText(event.id, event.text);
}

void RemoveAnnotationCommand::execute(ordo::core::AppKernel& kernel, const events::RemoveAnnotationRequested& event) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->remove(event.id);
}

void RemoveAllAnnotationsCommand::execute(ordo::core::AppKernel& kernel,
                                           const events::RemoveAllAnnotationsRequested& /*event*/) {
    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    if (!annotations) return;  // No AnnotationStore registered on this kernel.
    annotations->removeAll();
}

void GeometryChangedCommand::execute(ordo::core::AppKernel& kernel, const events::GeometryChanged& event) {
    // Replays SelectionStore pruning unchanged -- see this class's header comment.
    PruneSelectionCommand{}.execute(kernel, event);

    // Dirty/undo tracking must run before the early return below (see header).
    auto document = kernel.agentAs<DocumentStore>(kDocumentStoreName);
    if (document) document->markDirty();

    auto undo = kernel.agentAs<UndoStore>(kUndoStoreName);
    if (undo) undo->notifyMutation();

    auto annotations = kernel.agentAs<AnnotationStore>(kAnnotationStoreName);
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!annotations || !geometry) return;  // Either Agent absent -- nothing to refresh.
    annotations->refreshAssociations(geometry->model());
}

}  // namespace plnr::agent
