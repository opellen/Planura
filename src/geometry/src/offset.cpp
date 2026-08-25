#include <geo/offset.h>

#include <cmath>
#include <optional>

namespace plnr::geo {

namespace {

// One source segment's offset line: a/b are its endpoints translated by
// distance*shiftDir (dir == the source segment's own unit direction, reused
// by the overlap-cleanup pass as the "source direction" to compare against).
struct OffsetSeg {
    Vec3 a;
    Vec3 b;
    Vec3 dir;
};

// Builds one OffsetSeg per source segment (loop[i]->loop[(i+1)%loop.size()]);
// segCount wraps for closed, else loop.size()-1. Assumes each segment is
// already non-degenerate -- offsetLoop's own guard covers that.
std::vector<OffsetSeg> buildOffsetSegs(const std::vector<Vec3>& loop, const Vec3& planeNormal, double distance,
                                        bool closed) {
    std::vector<OffsetSeg> segs;
    const std::size_t segCount = closed ? loop.size() : loop.size() - 1;
    segs.reserve(segCount);
    for (std::size_t i = 0; i < segCount; ++i) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        const Vec3 dir = normalized(b - a);
        const Vec3 shift = normalized(cross(planeNormal, dir)) * distance;
        segs.push_back(OffsetSeg{a + shift, b + shift, dir});
    }
    return segs;
}

// Intersects two coplanar infinite lines (l1.a,l1.dir)/(l2.a,l2.dir) via the
// standard cross/dot line-intersection formula; nullopt when near-parallel
// -- see offset.h's "near-parallel" join-fallback threshold.
std::optional<Vec3> intersectSegLines(const OffsetSeg& l1, const OffsetSeg& l2, const Vec3& planeNormal) {
    const double denom = dot(cross(l1.dir, l2.dir), planeNormal);
    if (std::fabs(denom) < kEps) return std::nullopt;
    const Vec3 w = l2.a - l1.a;
    const double t = dot(cross(w, l2.dir), planeNormal) / denom;
    return l1.a + l1.dir * t;
}

// Joins a segment list into offset points (shared by the initial build and any
// post-cleanup rebuild). closed: one miter/parallel-fallback join per segment,
// wrapping; open: segs.size()+1 points, with raw translated chain endpoints.
std::vector<Vec3> joinSegsIntoPoints(const std::vector<OffsetSeg>& segs, const Vec3& planeNormal, bool closed) {
    std::vector<Vec3> pts;
    const std::size_t m = segs.size();
    if (m == 0) return pts;

    auto join = [&](const OffsetSeg& prev, const OffsetSeg& curr) {
        const std::optional<Vec3> joined = intersectSegLines(prev, curr, planeNormal);
        // Near-parallel fallback: prev's translated end and curr's translated
        // start coincide (up to float noise) when truly parallel, so their
        // midpoint is the "shared translated point" offset.h's header comment documents.
        return joined ? *joined : (prev.b + curr.a) * 0.5;
    };

    if (closed) {
        pts.reserve(m);
        for (std::size_t i = 0; i < m; ++i) {
            pts.push_back(join(segs[(i + m - 1) % m], segs[i]));
        }
    } else {
        pts.reserve(m + 1);
        pts.push_back(segs.front().a);
        for (std::size_t i = 1; i < m; ++i) {
            pts.push_back(join(segs[i - 1], segs[i]));
        }
        pts.push_back(segs.back().b);
    }
    return pts;
}

}  // namespace

OffsetResult offsetLoop(const std::vector<Vec3>& loop, const Vec3& planeNormal, double distance, bool closed,
                         bool keepOverlaps) {
    OffsetResult result;

    const std::size_t minSourcePoints = closed ? 3 : 2;
    if (loop.size() < minSourcePoints) return result;
    if (std::fabs(distance) < kMergeTol) return result;
    if (length(planeNormal) < kEps) return result;

    const Vec3 normal = normalized(planeNormal);

    // Zero-length source segment guard (including the closing wrap when
    // closed) -- length(a - b), not the free function distance(a, b), since
    // `distance` here already names this function's own parameter.
    const std::size_t segCount = closed ? loop.size() : loop.size() - 1;
    for (std::size_t i = 0; i < segCount; ++i) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        if (length(a - b) < kMergeTol) return result;
    }

    std::vector<OffsetSeg> segs = buildOffsetSegs(loop, normal, distance, closed);
    std::vector<Vec3> pts = joinSegsIntoPoints(segs, normal, closed);

    if (!keepOverlaps) {
        const std::size_t m = segs.size();
        std::vector<OffsetSeg> surviving;
        surviving.reserve(m);
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t nextIdx = closed ? (i + 1) % m : (i + 1);
            const Vec3 realized = pts[nextIdx] - pts[i];
            // Inverted (collapsed by the offset) iff the realized segment
            // now points backward relative to its own source direction.
            if (dot(realized, segs[i].dir) < 0.0) continue;
            surviving.push_back(segs[i]);
        }
        if (surviving.size() != m) {
            segs = std::move(surviving);
            pts = joinSegsIntoPoints(segs, normal, closed);
        }
    }

    const std::size_t minResultPoints = closed ? 3 : 2;
    if (pts.size() < minResultPoints) return result;

    result.points = std::move(pts);
    result.ok = true;
    return result;
}

}  // namespace plnr::geo
