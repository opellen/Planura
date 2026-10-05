#pragma once

// Scene-aware picking: a hit anywhere inside an Instance reports the whole top-level Instance.

#include <functional>
#include <utility>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/select.h>

namespace plnr::geo {

// instanceId = the top-level Instance hit, or kInvalidId on the root model. kind/id always name
// the underlying entity, however deeply nested.
struct ScenePickResult {
    PickKind kind = PickKind::None;
    Id id = kInvalidId;
    Vec3 point;
    double depth = 0.0;
    Id instanceId = kInvalidId;
};

// Picks relative to contextPath (instance-id chain from the root; empty = root). With
// pickInstances, a hit inside a child Instance reports that direct child's id.
// Geometry outside the context is click-transparent; a bad contextPath yields nothing.
// opts.filter covers only the context's own candidates.
// Depth comparison assumes translation-only transforms.
ScenePickResult pickScene(const Scene& scene, const Ray& ray, const PickOptions& opts, bool pickInstances,
                           const std::vector<Id>& contextPath);

// Region-pick counterpart of pickScene (same contextPath). out gets the context's own pick;
// outInstances (cleared, sorted by id) gets each direct child Instance qualifying as a unit:
// Window needs its whole subtree inside f, Crossing any vertex inside or edge crossing f.
// A vertex-less instance never qualifies; filter does not apply inside instances.
void regionPickScene(const Scene& scene, const Frustum& f, RegionMode mode,
                      const std::function<bool(EntityKind, Id)>& filter, RegionPick& out,
                      std::vector<Id>& outInstances, const std::vector<Id>& contextPath);

}  // namespace plnr::geo
