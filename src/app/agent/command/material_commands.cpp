#include "agent/command/material_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/material_repository.h"

namespace plnr::agent {

void MaterialCreateCommand::execute(ordo::core::AppKernel& kernel, const events::MaterialCreateRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->create(event.name, event.r, event.g, event.b, event.opacity);
}

void MaterialEditCommand::execute(ordo::core::AppKernel& kernel, const events::MaterialEditRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->edit(event.id, event.name, event.r, event.g, event.b, event.opacity);
}

void PaintCommand::execute(ordo::core::AppKernel& kernel, const events::PaintRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->paint(event.targets, event.materialId);
}

void SetActiveMaterialCommand::execute(ordo::core::AppKernel& kernel, const events::SetActiveMaterialRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->setActive(event.id);
}

void SetUvTransformCommand::execute(ordo::core::AppKernel& kernel, const events::SetUvTransformRequested& event) {
    auto materials = kernel.agentAs<MaterialRepository>(kMaterialRepositoryName);
    if (!materials) return;  // No MaterialRepository registered on this kernel.
    materials->setUvTransform(event.ref, event.transform);
}

}  // namespace plnr::agent
