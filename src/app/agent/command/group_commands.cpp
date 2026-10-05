#include "agent/command/group_commands.h"

#include <vector>

#include <ordo/core/kernel.h>

#include <geo/entity.h>

#include "agent/geometry_api.h"
#include "agent/selection_store.h"

namespace plnr::agent {

void GroupCreateCommand::execute(const events::GroupCreateRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!geometry || !selection) {
        // Either agent missing from this kernel -- no-op (Qt-free, no logging).
        return;
    }

    const geo::Id instanceId = geometry->makeGroup(selection->items(), event.asComponent, event.name);
    if (instanceId != geo::kInvalidId) {
        // the reference modeler: grouping deselects the (now-moved, ids-dead) loose geometry.
        selection->clear();
    }
}

void ExplodeCommand::execute(const events::ExplodeRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }

    if (event.instanceId != geo::kInvalidId) {
        // Original explicit-target path, unchanged.
        geometry->explode(event.instanceId);
        return;
    }

    // instanceId == kInvalidId: explode every currently selected Instance
    // instead. An empty/no-Instance selection is simply a no-op.
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!selection) return;

    std::vector<events::EntityRef> instanceRefs;
    for (const events::EntityRef& ref : selection->items()) {
        if (ref.kind == geo::EntityKind::Instance) {
            instanceRefs.push_back(ref);
        }
    }
    for (const events::EntityRef& ref : instanceRefs) {
        // Each call fires its own GeometryChanged -- acceptable, same
        // coarse per-call event granularity every other mutator here uses.
        geometry->explode(ref.id);
    }
    if (!instanceRefs.empty()) {
        // The exploded instances' ids are gone from the scene -- drop their
        // now-dead refs from the selection, same as any other
        // exploded/deleted entity.
        selection->subtract(instanceRefs);
    }
}

}  // namespace plnr::agent
