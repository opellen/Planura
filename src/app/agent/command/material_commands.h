#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates MaterialCreateRequested -> MaterialRepository::create(...). Thin,
// per the Ordo role policy: auto-naming and id assignment live in the Agent.
class MaterialCreateCommand : public ordo::core::Command<events::MaterialCreateRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::MaterialCreateRequested& event) override;
};

// Orchestrates MaterialEditRequested -> MaterialRepository::edit(...). Thin, per
// the Ordo role policy: unknown-id/no-op-value rejection lives in the Agent.
class MaterialEditCommand : public ordo::core::Command<events::MaterialEditRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::MaterialEditRequested& event) override;
};

// Orchestrates PaintRequested -> MaterialRepository::paint(...). Thin, per the
// Ordo role policy: unknown-materialId rejection and the per-ref no-op check
// live in the Agent.
class PaintCommand : public ordo::core::Command<events::PaintRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::PaintRequested& event) override;
};

// Orchestrates SetActiveMaterialRequested -> MaterialRepository::setActive(...).
// Thin passthrough -- deliberately NOT wrapped in UndoCaptureCommand by
// main.cpp (see that registration's own comment): active-material selection
// is transient UI-adjacent state, never undo-worthy, never dirty-worthy (see
// MaterialRepository::setActive's own comment).
class SetActiveMaterialCommand : public ordo::core::Command<events::SetActiveMaterialRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetActiveMaterialRequested& event) override;
};

// Orchestrates SetUvTransformRequested -> MaterialRepository::setUvTransform(...).
// Thin, per the Ordo role policy: the "no assignment to attach to"/no-op-value
// rejection lives in the Agent.
class SetUvTransformCommand : public ordo::core::Command<events::SetUvTransformRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetUvTransformRequested& event) override;
};

}  // namespace plnr::agent
