#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates SetFaceStyleRequested -> StyleStore::setFaceStyle(...). Thin,
// per the Ordo role policy: the no-op-when-unchanged check lives in the
// Agent. Deliberately NOT wrapped in UndoCaptureCommand by main.cpp -- see
// that registration's own comment and StyleStore's own class comment for the
// view-setting-semantics rationale.
class SetFaceStyleCommand : public ordo::core::Command<events::SetFaceStyleRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetFaceStyleRequested& event) override;
};

// Orchestrates SetEdgeStyleFlagRequested -> StyleStore::setEdgeFlag(...).
// Thin, same rationale as SetFaceStyleCommand above -- also deliberately NOT
// undo-wrapped.
class SetEdgeStyleFlagCommand : public ordo::core::Command<events::SetEdgeStyleFlagRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetEdgeStyleFlagRequested& event) override;
};

// Orchestrates SetAmbientOcclusionRequested -> StyleStore::
// setAmbientOcclusion(...). Thin, same rationale as SetFaceStyleCommand
// above -- also deliberately NOT undo-wrapped.
class SetAmbientOcclusionCommand : public ordo::core::Command<events::SetAmbientOcclusionRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetAmbientOcclusionRequested& event) override;
};

// Orchestrates SetAoStrengthRequested -> StyleStore::setAoStrength(...).
// Thin, same rationale as SetAmbientOcclusionCommand above.
class SetAoStrengthCommand : public ordo::core::Command<events::SetAoStrengthRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetAoStrengthRequested& event) override;
};

}  // namespace plnr::agent
