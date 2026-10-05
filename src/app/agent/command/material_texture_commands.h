#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

// MaterialSetTextureRequested's own command. Needs QFile, so it compiles straight into the
// executable and test binaries, never into plnr_agent.
namespace plnr::agent {

// event.path empty clears the texture; otherwise reads the file via QFile (only png/jpg/jpeg,
// else rejects via DocumentIoFailed), registers it in AssetRepository, and applies via
// MaterialRepository::setTexture. Unknown materialId is the Agent's own no-op.
class MaterialSetTextureCommand : public ordo::core::Command<events::MaterialSetTextureRequested> {
public:
    void execute(const events::MaterialSetTextureRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
