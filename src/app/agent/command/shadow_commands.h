#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates SetUseSunForShadingRequested -> ShadowStore::
// setUseSunForShading(...). Thin, per the Ordo role policy: the
// no-op-when-unchanged check lives in the Agent. Deliberately NOT
// undo-wrapped by main.cpp -- see that registration's comment and
// ShadowStore's own class comment for the view-setting rationale.
class SetUseSunForShadingCommand : public ordo::core::Command<events::SetUseSunForShadingRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetUseSunForShadingRequested& event) override;
};

// Orchestrates SetShowShadowsRequested -> ShadowStore::setShowShadows(...).
// Thin, same rationale as SetUseSunForShadingCommand above -- also
// deliberately NOT undo-wrapped.
class SetShowShadowsCommand : public ordo::core::Command<events::SetShowShadowsRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetShowShadowsRequested& event) override;
};

// Orchestrates SetSunPositionRequested -> ShadowStore::setPosition(...).
// Thin, same rationale as SetUseSunForShadingCommand above -- also
// deliberately NOT undo-wrapped.
class SetSunPositionCommand : public ordo::core::Command<events::SetSunPositionRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetSunPositionRequested& event) override;
};

// Orchestrates SetSunDateTimeRequested -> ShadowStore::setDateTime(...).
// Thin, same rationale as SetUseSunForShadingCommand above -- also
// deliberately NOT undo-wrapped.
class SetSunDateTimeCommand : public ordo::core::Command<events::SetSunDateTimeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetSunDateTimeRequested& event) override;
};

// Orchestrates SetShadowLightRequested -> ShadowStore::setLight(...).
// Thin, same rationale as SetUseSunForShadingCommand above -- also
// deliberately NOT undo-wrapped.
class SetShadowLightCommand : public ordo::core::Command<events::SetShadowLightRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetShadowLightRequested& event) override;
};

// Orchestrates SetShadowDarkRequested -> ShadowStore::setDark(...).
// Thin, same rationale as SetUseSunForShadingCommand above -- also
// deliberately NOT undo-wrapped.
class SetShadowDarkCommand : public ordo::core::Command<events::SetShadowDarkRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetShadowDarkRequested& event) override;
};

}  // namespace plnr::agent
