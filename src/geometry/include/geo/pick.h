#pragma once

#include <functional>

#include <geo/entity.h>
#include <geo/model.h>

namespace plnr::geo {

// dir must be unit length; pick() does not normalize it.
struct Ray {
    Vec3 origin;
    Vec3 dir;
};

// World-unit tolerances used by pick().
struct PickOptions {
    double vertexTol{};
    double edgeTol{};
    // Candidates it rejects are skipped as if absent (pick falls through). Null = all pickable.
    std::function<bool(EntityKind, Id)> filter;
};

enum class PickKind { None, Vertex, Edge, Face };

struct PickResult {
    PickKind kind = PickKind::None;
    Id id = kInvalidId;
    Vec3 point;
    double depth = 0.0;
};

// Highest-priority hit: vertex beats edge beats face; the nearest wins within a tier.
// PickKind::None when nothing qualifies.
PickResult pick(const Model& model, const Ray& ray, const PickOptions& opts);

}  // namespace plnr::geo
