#include "agent/command/tag_commands.h"

#include <ordo/core/kernel.h>

#include "agent/selection_store.h"
#include "agent/tag_store.h"

namespace plnr::agent {

void TagCreateCommand::execute(const events::TagCreateRequested& event, ordo::core::CommandContext& context) {
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    if (!tags) {
        return;
    }
    tags->createTag(event.name);
}

void TagAssignCommand::execute(const events::TagAssignRequested& event, ordo::core::CommandContext& context) {
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    if (!tags) return;

    tags->assignTag(event.refs, event.tagId);
}

void TagVisibilityCommand::execute(const events::TagVisibilityRequested& event, ordo::core::CommandContext& context) {
    auto tags = context.agentAs<TagStore>(kTagStoreName);
    if (!tags) return;

    tags->setTagVisible(event.tagId, event.visible);

    if (!event.visible) {
        // the reference modeler: hiding a tag deselects what is on it. Runs even if setTagVisible changed nothing (prune is then a no-op).
        auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
        if (selection) {
            selection->prune(
                [&tags](const events::EntityRef& ref) { return tags->isEntityVisible(ref); });
        }
    }
}

}  // namespace plnr::agent
