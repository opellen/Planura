#include "agent/command/geometry_commands.h"

#include <ordo/core/kernel.h>

#include "agent/geometry_api.h"

namespace plnr::agent {

void AddEdgeCommand::execute(const events::AddEdgeRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addEdge(event.a, event.b);
}

void AddRectangleCommand::execute(const events::AddRectangleRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addRectangle(event.corner1, event.corner2);
}

void ExtrudeFaceCommand::execute(const events::ExtrudeFaceRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->extrudeFace(event.faceId, event.distance);
}

void MoveEntityCommand::execute(const events::MoveEntityRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->moveEntity(event.kind, event.id, event.delta);
}

void RemoveEdgeCommand::execute(const events::RemoveEdgeRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->removeEdge(event.edgeId);
}

void AddPolylineCommand::execute(const events::AddPolylineRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addPolyline(event.points, event.closed);
}

void Add3dTextCommand::execute(const events::Add3dTextRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->add3dText(event.name, event.outlines, event.extrusion, event.origin);
}

void ReplaceLastPolylineCommand::execute(const events::ReplaceLastPolylineRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->replaceLastPolyline(event.points, event.closed);
}

void TransformEntitiesCommand::execute(const events::TransformEntitiesRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->transformEntities(event.refs, event.spec, event.copies);
}

void ApplyArrayCommand::execute(const events::ApplyArrayRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    if (event.divide) {
        agent->applyArrayDivide(event.n);
    } else {
        agent->applyArrayTimes(event.n);
    }
}

void FollowMeCommand::execute(const events::FollowMeRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->followMe(event.profileFaceId, event.pathPoints, event.closedPath);
}

void DivideEdgeCommand::execute(const events::DivideEdgeRequested& event, ordo::core::CommandContext& context) {
    auto agent = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->divideEdge(event.edgeId, event.n);
}

}  // namespace plnr::agent
