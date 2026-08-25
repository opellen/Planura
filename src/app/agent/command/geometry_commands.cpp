#include "agent/command/geometry_commands.h"

#include <ordo/core/app_kernel.h>

#include "agent/geometry_api.h"

namespace plnr::agent {

void AddEdgeCommand::execute(ordo::core::AppKernel& kernel, const events::AddEdgeRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addEdge(event.a, event.b);
}

void AddRectangleCommand::execute(ordo::core::AppKernel& kernel, const events::AddRectangleRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addRectangle(event.corner1, event.corner2);
}

void ExtrudeFaceCommand::execute(ordo::core::AppKernel& kernel, const events::ExtrudeFaceRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->extrudeFace(event.faceId, event.distance);
}

void MoveEntityCommand::execute(ordo::core::AppKernel& kernel, const events::MoveEntityRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->moveEntity(event.kind, event.id, event.delta);
}

void RemoveEdgeCommand::execute(ordo::core::AppKernel& kernel, const events::RemoveEdgeRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->removeEdge(event.edgeId);
}

void AddPolylineCommand::execute(ordo::core::AppKernel& kernel, const events::AddPolylineRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->addPolyline(event.points, event.closed);
}

void Add3dTextCommand::execute(ordo::core::AppKernel& kernel, const events::Add3dTextRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->add3dText(event.name, event.outlines, event.extrusion, event.origin);
}

void ReplaceLastPolylineCommand::execute(ordo::core::AppKernel& kernel,
                                          const events::ReplaceLastPolylineRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->replaceLastPolyline(event.points, event.closed);
}

void TransformEntitiesCommand::execute(ordo::core::AppKernel& kernel,
                                        const events::TransformEntitiesRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->transformEntities(event.refs, event.spec, event.copies);
}

void ApplyArrayCommand::execute(ordo::core::AppKernel& kernel, const events::ApplyArrayRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
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

void FollowMeCommand::execute(ordo::core::AppKernel& kernel, const events::FollowMeRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->followMe(event.profileFaceId, event.pathPoints, event.closedPath);
}

void DivideEdgeCommand::execute(ordo::core::AppKernel& kernel, const events::DivideEdgeRequested& event) {
    auto agent = kernel.agentAs<GeometryApi>(kGeometryApiName);
    if (!agent) {
        // No GeometryApi registered on this kernel -- no-op (Qt-free, no logging).
        return;
    }
    agent->divideEdge(event.edgeId, event.n);
}

}  // namespace plnr::agent
