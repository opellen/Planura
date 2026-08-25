#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddEdgeRequested -> GeometryApi::addEdge().
class AddEdgeCommand : public ordo::core::Command<events::AddEdgeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddEdgeRequested& event) override;
};

// Orchestrates AddRectangleRequested -> GeometryApi::addRectangle().
class AddRectangleCommand : public ordo::core::Command<events::AddRectangleRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddRectangleRequested& event) override;
};

// Orchestrates ExtrudeFaceRequested -> GeometryApi::extrudeFace().
class ExtrudeFaceCommand : public ordo::core::Command<events::ExtrudeFaceRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ExtrudeFaceRequested& event) override;
};

// Orchestrates MoveEntityRequested -> GeometryApi::moveEntity().
class MoveEntityCommand : public ordo::core::Command<events::MoveEntityRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::MoveEntityRequested& event) override;
};

// Orchestrates RemoveEdgeRequested -> GeometryApi::removeEdge().
class RemoveEdgeCommand : public ordo::core::Command<events::RemoveEdgeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::RemoveEdgeRequested& event) override;
};

// Orchestrates AddPolylineRequested -> GeometryApi::addPolyline().
class AddPolylineCommand : public ordo::core::Command<events::AddPolylineRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::AddPolylineRequested& event) override;
};

// Orchestrates Add3dTextRequested -> GeometryApi::add3dText().
class Add3dTextCommand : public ordo::core::Command<events::Add3dTextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::Add3dTextRequested& event) override;
};

// Orchestrates ReplaceLastPolylineRequested -> GeometryApi::replaceLastPolyline().
class ReplaceLastPolylineCommand : public ordo::core::Command<events::ReplaceLastPolylineRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ReplaceLastPolylineRequested& event) override;
};

// Orchestrates TransformEntitiesRequested -> GeometryApi::transformEntities().
class TransformEntitiesCommand : public ordo::core::Command<events::TransformEntitiesRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::TransformEntitiesRequested& event) override;
};

// Orchestrates ApplyArrayRequested -> GeometryApi::applyArrayTimes()/applyArrayDivide().
class ApplyArrayCommand : public ordo::core::Command<events::ApplyArrayRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::ApplyArrayRequested& event) override;
};

// Orchestrates FollowMeRequested -> GeometryApi::followMe().
class FollowMeCommand : public ordo::core::Command<events::FollowMeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::FollowMeRequested& event) override;
};

// Orchestrates DivideEdgeRequested -> GeometryApi::divideEdge().
class DivideEdgeCommand : public ordo::core::Command<events::DivideEdgeRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::DivideEdgeRequested& event) override;
};

}  // namespace plnr::agent
