#include "agent/command/solid_commands.h"

#include <ordo/core/app_kernel.h>

#include <geo/entity.h>

#include "agent/geometry_api.h"
#include "agent/material_repository.h"
#include "agent/selection_store.h"

namespace plnr::agent {

void SolidOpCommand::execute(ordo::core::AppKernel& kernel, const events::SolidOpRequested& event) {
    auto geometry = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        return;  // No GeometryApi registered on this kernel.
    }

    const GeometryApi::SolidOpResult result = geometry->applySolidOp(event.op, event.instanceIds);
    if (!result.ok) {
        kernel.send(events::StatusHintChanged{result.hint});
        return;
    }

    // Best-effort front-material carry-over -- see this class's own header
    // comment for why the lookup+paint happens HERE rather than inside
    // GeometryApi::applySolidOp.
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (materials) {
        for (const GeometryApi::SolidOpFaceProvenance& p : result.provenance) {
            const events::EntityRef sourceRef{geo::EntityKind::Face, p.sourceFaceId};
            const MaterialAssignment* assignment = materials->assignment(sourceRef);
            if (assignment != nullptr && assignment->frontMaterialId != geo::kInvalidId) {
                const events::EntityRef destRef{geo::EntityKind::Face, p.newFaceId};
                materials->paint({destRef}, assignment->frontMaterialId);
            }
        }
    }

    // the reference modeler: the consumed operands' old refs are dead anyway -- same
    // "clear selection on success" convention GroupCreateCommand's own
    // comment documents.
    auto selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
    if (selection) {
        selection->clear();
    }
}

}  // namespace plnr::agent
