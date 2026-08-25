#include "agent/command/tag_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/selection_store.h"
#include "agent/tag_store.h"

namespace plnr::agent {

void TagCreateCommand::execute(ordo::core::AppKernel& kernel, const events::TagCreateRequested& event) {
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    if (!tags) {
        // No TagStore registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    tags->createTag(event.name);
}

void TagAssignCommand::execute(ordo::core::AppKernel& kernel, const events::TagAssignRequested& event) {
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    if (!tags) return;  // TagStore not registered on this kernel.

    tags->assignTag(event.refs, event.tagId);
}

void TagVisibilityCommand::execute(ordo::core::AppKernel& kernel, const events::TagVisibilityRequested& event) {
    auto tags = kernel.agentAs<TagStore>(kTagStoreName);
    if (!tags) return;  // TagStore not registered on this kernel.

    tags->setTagVisible(event.tagId, event.visible);

    if (!event.visible) {
        // the reference modeler: hiding a tag deselects everything currently on it too.
        // Runs regardless of whether setTagVisible actually changed
        // anything (an unknown tagId, or a tag already hidden) -- prune is
        // itself a no-op event-wise when nothing selected needs dropping,
        // same as SetHiddenCommand's unconditional selection->subtract().
        auto selection = kernel.agentAs<SelectionStore>(kSelectionStoreName);
        if (selection) {
            selection->prune(
                [&tags](const events::EntityRef& ref) { return tags->isEntityVisible(ref); });
        }
    }
}

}  // namespace plnr::agent
