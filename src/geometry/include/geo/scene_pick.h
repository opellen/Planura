#pragma once

// Scene-aware picking: geo::pick only sees the root Model, so it misses geometry inside an Instance.
// Here, a hit anywhere inside an Instance reports the whole top-level Instance instead.

#include <functional>
#include <utility>
#include <vector>

#include <geo/entity.h>
#include <geo/pick.h>
#include <geo/scene.h>
#include <geo/select.h>

namespace plnr::geo {

// PickResult plus instanceId: the top-level Instance the hit belongs to, or kInvalidId for a hit on
// the root model. kind/id always name the underlying entity, however deeply nested.
struct ScenePickResult {
    PickKind kind = PickKind::None;
    Id id = kInvalidId;
    Vec3 point;
    double depth = 0.0;
    Id instanceId = kInvalidId;
};

// Casts ray against the scene relative to contextPath (instance-id chain from the root; empty =
// root). The context definition is picked like geo::pick; with pickInstances, child Instances recurse
// too but a hit reports that DIRECT CHILD's id. Anything outside the context definition is
// click-transparent and an unresolvable contextPath yields nothing. opts.filter applies only to the
// context definition's own candidates; depth comparison assumes rigid (translation-only) transforms.
ScenePickResult pickScene(const Scene& scene, const Ray& ray, const PickOptions& opts, bool pickInstances,
                           const std::vector<Id>& contextPath);

// pickInFrustum companion to pickScene, same contextPath semantics. out gets the context definition's
// own region pick (f is shifted into its local frame first). outInstances is cleared, then filled
// ascending by id with each direct child Instance qualifying as a UNIT: Window needs every transformed
// vertex of its recursive subtree inside f, Crossing any vertex inside or any edge crossing f (an
// instance with no vertices never qualifies). filter is not applied to instance interiors.
void regionPickScene(const Scene& scene, const Frustum& f, RegionMode mode,
                      const std::function<bool(EntityKind, Id)>& filter, RegionPick& out,
                      std::vector<Id>& outInstances, const std::vector<Id>& contextPath);

}  // namespace plnr::geo
