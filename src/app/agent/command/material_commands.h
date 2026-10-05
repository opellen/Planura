#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// MaterialCreateRequested -> MaterialRepository::create(); auto-naming and id assignment live in the Agent.
class MaterialCreateCommand : public ordo::core::Command<events::MaterialCreateRequested> {
public:
    void execute(const events::MaterialCreateRequested& event, ordo::core::CommandContext& context) override;
};

// MaterialEditRequested -> MaterialRepository::edit(); unknown-id/no-op rejection lives in the Agent.
class MaterialEditCommand : public ordo::core::Command<events::MaterialEditRequested> {
public:
    void execute(const events::MaterialEditRequested& event, ordo::core::CommandContext& context) override;
};

// PaintRequested -> MaterialRepository::paint(); unknown materialId and per-ref no-op checks live in the Agent.
class PaintCommand : public ordo::core::Command<events::PaintRequested> {
public:
    void execute(const events::PaintRequested& event, ordo::core::CommandContext& context) override;
};

// SetActiveMaterialRequested -> MaterialRepository::setActive(). Not undo-wrapped, never dirties: transient UI-adjacent state.
class SetActiveMaterialCommand : public ordo::core::Command<events::SetActiveMaterialRequested> {
public:
    void execute(const events::SetActiveMaterialRequested& event, ordo::core::CommandContext& context) override;
};

// SetUvTransformRequested -> MaterialRepository::setUvTransform(); no-assignment/no-op rejection lives in the Agent.
class SetUvTransformCommand : public ordo::core::Command<events::SetUvTransformRequested> {
public:
    void execute(const events::SetUvTransformRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
