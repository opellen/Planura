#include "agent/command/material_commands.h"

#include <ordo/core/kernel.h>

#include "agent/material_repository.h"

namespace plnr::agent {

void MaterialCreateCommand::execute(const events::MaterialCreateRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->create(event.name, event.r, event.g, event.b, event.opacity);
}

void MaterialEditCommand::execute(const events::MaterialEditRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->edit(event.id, event.name, event.r, event.g, event.b, event.opacity);
}

void PaintCommand::execute(const events::PaintRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->paint(event.targets, event.materialId);
}

void SetActiveMaterialCommand::execute(const events::SetActiveMaterialRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->setActive(event.id);
}

void SetUvTransformCommand::execute(const events::SetUvTransformRequested& event, ordo::core::CommandContext& context) {
    auto materials = context.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->setUvTransform(event.ref, event.transform);
}

}  // namespace plnr::agent
