#include "viewport/edge_style.h"

namespace plnr::viewport {

BandedEdgeBuckets classifyAndBucketEdges(const std::vector<EdgeAdjacency>& edges, const geo::Vec3& eye,
                                          int bandCount) {
    BandedEdgeBuckets result;
    result.bandVerts.resize(static_cast<std::size_t>(bandCount > 0 ? bandCount : 0));
    if (edges.empty() || bandCount <= 0) return result;

    // Pass 1: min/max eye-distance among Interior edges. Recomputed in pass 2
    // rather than cached (cheap pure function; cf. material_batch.h's bucketTrianglesByMaterial).
    double minDist = 0.0;
    double maxDist = 0.0;
    bool first = true;
    for (const EdgeAdjacency& edge : edges) {
        if (isProfileWeight(classifyEdge(edge, eye))) continue;
        const geo::Vec3 mid = (edge.a + edge.b) * 0.5;
        const double d = geo::distance(mid, eye);
        if (first) {
            minDist = maxDist = d;
            first = false;
        } else {
            if (d < minDist) minDist = d;
            if (d > maxDist) maxDist = d;
        }
    }

    // Pass 2: emit each edge into its bucket.
    for (const EdgeAdjacency& edge : edges) {
        const EdgeClass cls = classifyEdge(edge, eye);
        if (isProfileWeight(cls)) {
            appendEdgeVerts(result.profileVerts, edge.a, edge.b);
            continue;
        }
        const geo::Vec3 mid = (edge.a + edge.b) * 0.5;
        const double d = geo::distance(mid, eye);
        const int band = depthBandIndex(d, minDist, maxDist, bandCount);
        appendEdgeVerts(result.bandVerts[static_cast<std::size_t>(band)], edge.a, edge.b);
    }

    return result;
}

}  // namespace plnr::viewport
