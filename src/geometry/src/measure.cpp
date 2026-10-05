#include <geo/measure.h>

#include <geo/triangulate.h>
#include <geo/vec3.h>

namespace plnr::geo {

double edgeLength(const Model& model, Id edgeId) {
    const Edge* edge = model.edge(edgeId);
    if (!edge) return 0.0;

    const HalfEdge* h0 = model.halfEdge(edge->halfEdges[0]);
    const HalfEdge* h1 = model.halfEdge(edge->halfEdges[1]);
    if (!h0 || !h1) return 0.0;

    const Vertex* v0 = model.vertex(h0->origin);
    const Vertex* v1 = model.vertex(h1->origin);
    if (!v0 || !v1) return 0.0;

    return distance(v0->pos, v1->pos);
}

double faceArea(const Model& model, Id faceId) {
    const std::vector<Id> tris = triangulate(model, faceId);

    double area = 0.0;
    for (std::size_t i = 0; i < tris.size(); i += 3) {
        const Vertex* a = model.vertex(tris[i]);
        const Vertex* b = model.vertex(tris[i + 1]);
        const Vertex* c = model.vertex(tris[i + 2]);
        if (!a || !b || !c) continue;
        area += 0.5 * length(cross(b->pos - a->pos, c->pos - a->pos));
    }
    return area;
}

}  // namespace plnr::geo
