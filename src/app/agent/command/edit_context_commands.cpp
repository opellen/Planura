#include "agent/command/edit_context_commands.h"

#include <ordo/core/kernel.h>

#include <geo/scene.h>

#include "agent/edit_context_store.h"
#include "agent/geometry_api.h"
#include "agent/selection_store.h"

namespace plnr::agent {

void EnterContextCommand::execute(const events::EnterContextRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto editContext = context.agentAs<EditContextStore>(kEditContextStoreName);
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!geometry || !editContext || !selection) {
        // Some agent missing from this kernel -- nothing to orchestrate.
        // Qt-free by design, so this is a plain guard clause rather than a
        // logged warning.
        return;
    }

    const geo::Definition* currentDef = geometry->contextDefinition(editContext->path());
    if (currentDef == nullptr) {
        return;  // defensive: the CURRENT context itself is somehow invalid
    }
    if (geometry->scene().findInstance(currentDef->id, event.instanceId) == nullptr) {
        return;  // not a child of the current context -- unknown/foreign id, no-op
    }

    editContext->push(event.instanceId);
    // the reference modeler: entering a context deselects the (now out-of-context) loose
    // geometry/instances that were selected before.
    selection->clear();
}

void ExitContextCommand::execute(const events::ExitContextRequested& event, ordo::core::CommandContext& context) {
    (void)event;
    auto editContext = context.agentAs<EditContextStore>(kEditContextStoreName);
    if (!editContext) {
        return;  // EditContextStore not registered on this kernel.
    }

    editContext->pop();
    // the reference modeler: exiting a context deselects too -- runs even when pop() was
    // itself a no-op (already at root), same as the harmless idempotent
    // pattern GroupCreateCommand/ExplodeCommand use elsewhere.
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (selection) {
        selection->clear();
    }
}

}  // namespace plnr::agent
