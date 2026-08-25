#include <geo/scene_pick.h>

#include <algorithm>
#include <cmath>

namespace plnr::geo {

namespace {

// Duplicated from select.cpp's identical file-local helpers (kept private
// there); scene_pick is the only other caller of this frustum-membership
// math, too small to justify exposing through select.h.
bool pointInFrustum(const Frustum& f, const Vec3& p) {
    for (const Plane& plane : f.planes) {
        if (dot(plane.normal, p) + plane.d < -kEps) {
            return false;
        }
    }
    return true;
}

bool segmentCrossesFrustum(const Frustum& f, const Vec3& a, const Vec3& b) {
    double tMin = 0.0;
    double tMax = 1.0;
    const Vec3 dir = b - a;

    for (const Plane& plane : f.planes) {
        const double d0 = dot(plane.normal, a) + plane.d;
        const double denom = dot(plane.normal, dir);

        if (std::fabs(denom) < kEps) {
            if (d0 < -kEps) {
                return false;
            }
            continue;
        }

        const double t = -d0 / denom;
        if (denom > 0.0) {
            tMin = std::max(tMin, t);
        } else {
            tMax = std::min(tMax, t);
        }
        if (tMin > tMax) {
            return false;
        }
    }
    return tMin <= tMax;
}

// Recursively picks against def's Definition tree in world space; worldRay
// stays fixed, composedXf (local-to-world, composed down from the top-level
// Instance) changes per level. topInstanceId threads through unchanged.
void pickDefinitionRecursive(const Scene& scene, const Definition& def, const Ray& worldRay,
                              const Transform& composedXf, Id topInstanceId, bool& found, ScenePickResult& best) {
    const Transform inv = composedXf.inverse();
    const Ray localRay{inv.apply(worldRay.origin), normalized(inv.applyVector(worldRay.dir))};

    // Instance interiors: no app-side filter (see pickScene's header
    // comment) -- a default-constructed PickOptions admits everything.
    const PickResult hit = pick(def.model, localRay, PickOptions{});
    if (hit.kind != PickKind::None && (!found || hit.depth < best.depth)) {
        found = true;
        best.kind = hit.kind;
        best.id = hit.id;
        best.point = composedXf.apply(hit.point);  // back to world space
        best.depth = hit.depth;  // rigid transform -- ray parameter unchanged, see header comment
        best.instanceId = topInstanceId;
    }

    for (const Instance& child : def.children) {
        const Definition* childDef = scene.definition(child.definitionId);
        if (childDef == nullptr) continue;  // defensive: shouldn't happen -- Scene keeps this id valid
        pickDefinitionRecursive(scene, *childDef, worldRay, composedXf.composed(child.transform), topInstanceId,
                                 found, best);
    }
}

// Recursive world-space vertex/edge-segment gather for regionPickScene's
// per-instance window/crossing test -- def's own geometry plus every nested
// child's, all expressed in world space via the composed transform.
void collectInstanceGeometryWorld(const Scene& scene, const Definition& def, const Transform& xf,
                                   std::vector<Vec3>& outVerts, std::vector<std::pair<Vec3, Vec3>>& outEdges) {
    for (const auto& [id, v] : def.model.vertices()) {
        (void)id;
        outVerts.push_back(xf.apply(v.pos));
    }
    for (const auto& [id, e] : def.model.edges()) {
        (void)id;
        const HalfEdge* h0 = def.model.halfEdge(e.halfEdges[0]);
        const HalfEdge* h1 = def.model.halfEdge(e.halfEdges[1]);
        if (h0 == nullptr || h1 == nullptr) continue;
        const Vertex* v0 = def.model.vertex(h0->origin);
        const Vertex* v1 = def.model.vertex(h1->origin);
        if (v0 == nullptr || v1 == nullptr) continue;
        outEdges.emplace_back(xf.apply(v0->pos), xf.apply(v1->pos));
    }
    for (const Instance& child : def.children) {
        const Definition* childDef = scene.definition(child.definitionId);
        if (childDef == nullptr) continue;  // defensive: shouldn't happen -- Scene keeps this id valid
        collectInstanceGeometryWorld(scene, *childDef, xf.composed(child.transform), outVerts, outEdges);
    }
}

// Resolves the Definition contextPath names (root if empty) plus its composed
// local-to-world Transform -- same walk as agent::GeometryApi::contextDefinition, duplicated since
// geo/ can't depend on the app domain layer. def is nullptr for an invalid path.
struct ContextResolution {
    const Definition* def = nullptr;
    Transform worldTransform = Transform::identity();
};

ContextResolution resolveContext(const Scene& scene, const std::vector<Id>& contextPath) {
    ContextResolution result;
    result.def = scene.definition(kRootDefinitionId);
    for (Id instanceId : contextPath) {
        if (result.def == nullptr) {
            return result;  // defensive: shouldn't happen -- Scene keeps every definition id valid
        }
        const Instance* inst = scene.findInstance(result.def->id, instanceId);
        if (inst == nullptr) {
            result.def = nullptr;
            return result;  // instanceId isn't a child of the previous step's definition
        }
        result.worldTransform = result.worldTransform.composed(inst->transform);
        result.def = scene.definition(inst->definitionId);
    }
    return result;
}

}  // namespace

ScenePickResult pickScene(const Scene& scene, const Ray& ray, const PickOptions& opts, bool pickInstances,
                           const std::vector<Id>& contextPath) {
    const ContextResolution ctx = resolveContext(scene, contextPath);
    if (ctx.def == nullptr) {
        return ScenePickResult{};  // unresolvable contextPath -- nothing pickable
    }

    bool found = false;
    ScenePickResult best;

    // The context definition's own geometry, picked "raw" in its local frame
    // then transformed back to world space -- a no-op when contextPath is
    // empty (ctx.worldTransform is identity), so this IS today's root-model pick.
    const Transform inv = ctx.worldTransform.inverse();
    const Ray localRay{inv.apply(ray.origin), normalized(inv.applyVector(ray.dir))};
    const PickResult ctxHit = pick(ctx.def->model, localRay, opts);
    if (ctxHit.kind != PickKind::None) {
        found = true;
        best.kind = ctxHit.kind;
        best.id = ctxHit.id;
        best.point = ctx.worldTransform.apply(ctxHit.point);
        best.depth = ctxHit.depth;  // rigid transform -- ray parameter unchanged, see header comment
        best.instanceId = kInvalidId;
    }

    if (pickInstances) {
        for (const Instance& child : ctx.def->children) {
            const Definition* childDef = scene.definition(child.definitionId);
            if (childDef == nullptr) continue;  // defensive: shouldn't happen -- Scene keeps this id valid

            bool instFound = false;
            ScenePickResult instBest;
            pickDefinitionRecursive(scene, *childDef, ray, ctx.worldTransform.composed(child.transform), child.id,
                                     instFound, instBest);
            if (instFound && (!found || instBest.depth < best.depth)) {
                found = true;
                best = instBest;
            }
        }
    }

    return found ? best : ScenePickResult{};
}

void regionPickScene(const Scene& scene, const Frustum& f, RegionMode mode,
                      const std::function<bool(EntityKind, Id)>& filter, RegionPick& out,
                      std::vector<Id>& outInstances, const std::vector<Id>& contextPath) {
    out = RegionPick{};
    outInstances.clear();

    const ContextResolution ctx = resolveContext(scene, contextPath);
    if (ctx.def == nullptr) {
        return;  // unresolvable contextPath -- nothing pickable
    }

    // Shift f into the context definition's local frame -- a no-op when
    // contextPath is empty (matching root's pre-3.3 behavior). Valid because
    // every Transform today is translation-only, so a plane's offset just shifts.
    Frustum localFrustum = f;
    for (Plane& plane : localFrustum.planes) {
        plane.d += dot(plane.normal, ctx.worldTransform.t);
    }
    out = pickInFrustum(ctx.def->model, localFrustum, mode, filter);

    for (const Instance& child : ctx.def->children) {
        const Definition* childDef = scene.definition(child.definitionId);
        if (childDef == nullptr) continue;  // defensive: shouldn't happen -- Scene keeps this id valid

        std::vector<Vec3> worldVerts;
        std::vector<std::pair<Vec3, Vec3>> worldEdges;
        collectInstanceGeometryWorld(scene, *childDef, ctx.worldTransform.composed(child.transform), worldVerts,
                                      worldEdges);

        bool qualifies = false;
        if (mode == RegionMode::Window) {
            // Vacuous-Window guard: an instance with no vertices at all
            // (shouldn't happen -- makeGroup never groups an empty selection)
            // never qualifies, rather than std::all_of vacuously returning true.
            qualifies = !worldVerts.empty() &&
                        std::all_of(worldVerts.begin(), worldVerts.end(), [&](const Vec3& p) { return pointInFrustum(f, p); });
        } else {
            qualifies =
                std::any_of(worldVerts.begin(), worldVerts.end(), [&](const Vec3& p) { return pointInFrustum(f, p); }) ||
                std::any_of(worldEdges.begin(), worldEdges.end(),
                            [&](const std::pair<Vec3, Vec3>& seg) { return segmentCrossesFrustum(f, seg.first, seg.second); });
        }

        if (qualifies) {
            outInstances.push_back(child.id);
        }
    }

    std::sort(outInstances.begin(), outInstances.end());
}

}  // namespace plnr::geo
