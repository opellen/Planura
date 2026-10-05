#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// SetUseSunForShadingRequested -> ShadowStore::setUseSunForShading(). Not undo-wrapped (view setting).
class SetUseSunForShadingCommand : public ordo::core::Command<events::SetUseSunForShadingRequested> {
public:
    void execute(const events::SetUseSunForShadingRequested& event, ordo::core::CommandContext& context) override;
};

// SetShowShadowsRequested -> ShadowStore::setShowShadows(). Not undo-wrapped (view setting).
class SetShowShadowsCommand : public ordo::core::Command<events::SetShowShadowsRequested> {
public:
    void execute(const events::SetShowShadowsRequested& event, ordo::core::CommandContext& context) override;
};

// SetSunPositionRequested -> ShadowStore::setPosition(). Not undo-wrapped (view setting).
class SetSunPositionCommand : public ordo::core::Command<events::SetSunPositionRequested> {
public:
    void execute(const events::SetSunPositionRequested& event, ordo::core::CommandContext& context) override;
};

// SetSunDateTimeRequested -> ShadowStore::setDateTime(). Not undo-wrapped (view setting).
class SetSunDateTimeCommand : public ordo::core::Command<events::SetSunDateTimeRequested> {
public:
    void execute(const events::SetSunDateTimeRequested& event, ordo::core::CommandContext& context) override;
};

// SetShadowLightRequested -> ShadowStore::setLight(). Not undo-wrapped (view setting).
class SetShadowLightCommand : public ordo::core::Command<events::SetShadowLightRequested> {
public:
    void execute(const events::SetShadowLightRequested& event, ordo::core::CommandContext& context) override;
};

// SetShadowDarkRequested -> ShadowStore::setDark(). Not undo-wrapped (view setting).
class SetShadowDarkCommand : public ordo::core::Command<events::SetShadowDarkRequested> {
public:
    void execute(const events::SetShadowDarkRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
