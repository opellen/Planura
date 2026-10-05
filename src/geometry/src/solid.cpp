#include <geo/solid.h>

#include <geo/triangulate.h>
#include <geo/vec3.h>

namespace plnr::geo {

namespace {

// One rotation step around h's origin: twin(prev(h)). Valid only once every half-edge has a face.
Id stepAroundVertex(const Model& model, Id h) {
    const HalfEdge* he = model.halfEdge(h);
    if (he == nullptr) return kInvalidId;
    const HalfEdge* hePrev = model.halfEdge(he->prev);
    if (hePrev == nullptr) return kInvalidId;
    return hePrev->twin;
}

// True if v's outgoing half-edges form one umbrella cycle; the step is a bijection, so the orbit
// must close in exactly `expected` steps. A shorter cycle (bowtie) or a malformed chain fails.
bool isVertexManifold(const Model& model, const Vertex& v) {
    const std::size_t expected = v.outgoing.size();
    if (expected == 0) return true;  // isolated vertex -- nothing to check

    const Id start = v.outgoing.front();
    Id h = start;
    for (std::size_t step = 0; step < expected; ++step) {
        h = stepAroundVertex(model, h);
        if (h == kInvalidId) return false;  // malformed chain
        if (h == start) return (step + 1) == expected;
    }
    return false;  // did not return to start within the expected orbit length
}

}  // namespace

double solidVolume(const Model& model) {
    double volume = 0.0;
    for (const auto& [faceId, face] : model.faces()) {
        (void)face;
        const std::vector<Id> tris = triangulate(model, faceId);
        for (std::size_t i = 0; i < tris.size(); i += 3) {
            const Vertex* a = model.vertex(tris[i]);
            const Vertex* b = model.vertex(tris[i + 1]);
            const Vertex* c = model.vertex(tris[i + 2]);
            if (a == nullptr || b == nullptr || c == nullptr) continue;
            volume += dot(a->pos, cross(b->pos, c->pos)) / 6.0;
        }
    }
    return volume;
}

SolidInfo isSolid(const Model& model) {
    SolidInfo info;

    for (const auto& [id, he] : model.halfEdges()) {
        (void)id;
        if (he.face == kInvalidId) {
            info.wireEdges = true;
            break;
        }
    }

    if (!info.wireEdges) {
        for (const auto& [id, v] : model.vertices()) {
            (void)id;
            if (!isVertexManifold(model, v)) {
                info.nonManifoldVertex = true;
                break;
            }
        }
    }

    info.nonPositiveVolume = !(solidVolume(model) > kEps);

    info.solid = !info.wireEdges && !info.nonManifoldVertex && !info.nonPositiveVolume;
    return info;
}

bool isSolidDefinition(const Definition& def) {
    return def.children.empty() && isSolid(def.model).solid;
}

}  // namespace plnr::geo
