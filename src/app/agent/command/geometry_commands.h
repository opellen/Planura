#pragma once

#include <ordo/core/command.h>

#include "agent/events.h"

namespace plnr::agent {

// Orchestrates AddEdgeRequested -> GeometryApi::addEdge().
class AddEdgeCommand : public ordo::core::Command<events::AddEdgeRequested> {
public:
    void execute(const events::AddEdgeRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates AddRectangleRequested -> GeometryApi::addRectangle().
class AddRectangleCommand : public ordo::core::Command<events::AddRectangleRequested> {
public:
    void execute(const events::AddRectangleRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates ExtrudeFaceRequested -> GeometryApi::extrudeFace().
class ExtrudeFaceCommand : public ordo::core::Command<events::ExtrudeFaceRequested> {
public:
    void execute(const events::ExtrudeFaceRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates MoveEntityRequested -> GeometryApi::moveEntity().
class MoveEntityCommand : public ordo::core::Command<events::MoveEntityRequested> {
public:
    void execute(const events::MoveEntityRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates RemoveEdgeRequested -> GeometryApi::removeEdge().
class RemoveEdgeCommand : public ordo::core::Command<events::RemoveEdgeRequested> {
public:
    void execute(const events::RemoveEdgeRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates AddPolylineRequested -> GeometryApi::addPolyline().
class AddPolylineCommand : public ordo::core::Command<events::AddPolylineRequested> {
public:
    void execute(const events::AddPolylineRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates Add3dTextRequested -> GeometryApi::add3dText().
class Add3dTextCommand : public ordo::core::Command<events::Add3dTextRequested> {
public:
    void execute(const events::Add3dTextRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates ReplaceLastPolylineRequested -> GeometryApi::replaceLastPolyline().
class ReplaceLastPolylineCommand : public ordo::core::Command<events::ReplaceLastPolylineRequested> {
public:
    void execute(const events::ReplaceLastPolylineRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates TransformEntitiesRequested -> GeometryApi::transformEntities().
class TransformEntitiesCommand : public ordo::core::Command<events::TransformEntitiesRequested> {
public:
    void execute(const events::TransformEntitiesRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates ApplyArrayRequested -> GeometryApi::applyArrayTimes()/applyArrayDivide().
class ApplyArrayCommand : public ordo::core::Command<events::ApplyArrayRequested> {
public:
    void execute(const events::ApplyArrayRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates FollowMeRequested -> GeometryApi::followMe().
class FollowMeCommand : public ordo::core::Command<events::FollowMeRequested> {
public:
    void execute(const events::FollowMeRequested& event, ordo::core::CommandContext& context) override;
};

// Orchestrates DivideEdgeRequested -> GeometryApi::divideEdge().
class DivideEdgeCommand : public ordo::core::Command<events::DivideEdgeRequested> {
public:
    void execute(const events::DivideEdgeRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
