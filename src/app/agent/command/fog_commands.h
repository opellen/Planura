#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates SetFogEnabledRequested -> FogStore::setEnabled(...). Thin,
// per the Ordo role policy: the no-op-when-unchanged check lives in the
// Agent. Deliberately NOT wrapped in UndoCaptureCommand by main.cpp -- see
// that registration's own comment and FogStore's own class comment for the
// view-setting-semantics rationale (same as SetFaceStyleCommand/
// style_commands.h, SetUseSunForShadingCommand/shadow_commands.h).
class SetFogEnabledCommand : public ordo::core::Command<events::SetFogEnabledRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetFogEnabledRequested& event) override;
};

// Orchestrates SetFogRangeRequested -> FogStore::setRange(...). Thin, same
// rationale as SetFogEnabledCommand above -- also deliberately NOT
// undo-wrapped.
class SetFogRangeCommand : public ordo::core::Command<events::SetFogRangeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetFogRangeRequested& event) override;
};

// Orchestrates SetFogUseBackgroundColorRequested ->
// FogStore::setUseBackgroundColor(...). Thin, same rationale as
// SetFogEnabledCommand above -- also deliberately NOT undo-wrapped.
class SetFogUseBackgroundColorCommand : public ordo::core::Command<events::SetFogUseBackgroundColorRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetFogUseBackgroundColorRequested& event) override;
};

}  // namespace plnr::agent
