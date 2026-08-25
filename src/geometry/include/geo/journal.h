#pragma once

#include <string>
#include <vector>

#include <geo/model.h>
#include <geo/vec3.h>

namespace plnr::geo {

// Non-owning observer interfaces notified from geo::Model/geo::Scene's record-lifecycle choke points.
// Every method defaults empty -- override only what you need. Once attached, notifications fire from
// every mutation path including restore*/*ForRollback. Events speak the .plr vocabulary (vertex pos,
// edge endpoints, ordered face vertex loops); half-edge records are NEVER journaled.

// Observes plnr::geo::Model record lifecycle events.
struct ModelJournal {
    virtual ~ModelJournal() = default;

    virtual void vertexCreated(Id id, Vec3 pos) {}
    virtual void vertexMoved(Id id, Vec3 before, Vec3 after) {}
    virtual void vertexDeleted(Id id, Vec3 posBefore) {}

    virtual void edgeCreated(Id id, Id v0, Id v1) {}
    virtual void edgeDeleted(Id id, Id v0, Id v1) {}

    // loop/loopBefore/loopAfter are ordered vertex-id boundary loops, as Model::faceVertexLoop returns.
    virtual void faceCreated(Id id, const std::vector<Id>& loop) {}
    virtual void faceDeleted(Id id, const std::vector<Id>& loopBefore) {}
    // splitEdge's in-place face re-chain (the face id survives): fired instead of delete + create.
    virtual void faceLoopModified(Id id, const std::vector<Id>& loopBefore, const std::vector<Id>& loopAfter) {}
};

// Observes Scene record lifecycle events; a definition's own mesh events go to its Model's journal.
struct SceneJournal {
    virtual ~SceneJournal() = default;

    virtual void definitionCreated(Id id, const std::string& name, bool isGroup) {}
    virtual void definitionDeleted(Id id, const std::string& name, bool isGroup) {}

    virtual void instanceCreated(Id parentDefId, Id id, Id definitionId, const Transform& transform,
                                  const std::string& name) {}
    virtual void instanceDeleted(Id parentDefId, Id id, Id definitionId, const Transform& transformBefore,
                                  const std::string& nameBefore) {}
    virtual void instanceModified(Id parentDefId, Id id, const Transform& before, const Transform& after,
                                   const std::string& nameBefore, const std::string& nameAfter) {}
};

}  // namespace plnr::geo
