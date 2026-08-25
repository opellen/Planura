#pragma once

#include <functional>

#include <geo/entity.h>
#include <geo/model.h>

namespace plnr::geo {

// A picking ray. dir must already be unit length -- pick() does not
// normalize it internally, callers own that.
struct Ray {
    Vec3 origin;
    Vec3 dir;
};

// World-unit tolerances used by pick().
struct PickOptions {
    double vertexTol{};
    double edgeTol{};
    // Pick-through filter: when set, pick() skips any candidate this returns
    // false for (as if absent, falling through to what's behind it, or
    // PickKind::None). Null (default) = everything pickable.
    std::function<bool(EntityKind, Id)> filter;
};

enum class PickKind { None, Vertex, Edge, Face };

struct PickResult {
    PickKind kind = PickKind::None;
    Id id = kInvalidId;
    Vec3 point;
    double depth = 0.0;
};

// Casts ray against model geometry and returns the highest-priority hit:
// vertex candidates beat edge candidates, which beat face candidates. Within
// a tier, the nearest (smallest ray parameter t) candidate wins. Returns a
// default (PickKind::None) result when nothing qualifies.
PickResult pick(const Model& model, const Ray& ray, const PickOptions& opts);

}  // namespace plnr::geo
