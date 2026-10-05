#include <geo/intersect.h>

#include <cmath>

namespace plnr::geo {

std::optional<Vec3> segmentIntersect(Vec3 a1, Vec3 a2, Vec3 b1, Vec3 b2, double tol) {
    const Vec3 u = a2 - a1;
    const Vec3 v = b2 - b1;
    const double lenU = length(u);
    const double lenV = length(v);
    if (lenU < kEps || lenV < kEps) {
        return std::nullopt;  // degenerate segment
    }

    // Unit directions make sc/tc below actual arc-length distances from each
    // segment's own first endpoint -- needed since the interior check below
    // is distance-based (tol is a length).
    const Vec3 uu = u * (1.0 / lenU);
    const Vec3 vv = v * (1.0 / lenV);

    const double b = dot(uu, vv);
    const double denom = 1.0 - b * b;  // == sin^2(angle between the lines)
    if (denom < kEps) {
        return std::nullopt;  // parallel or collinear-overlapping
    }

    const Vec3 w0 = a1 - b1;
    const double d = dot(uu, w0);
    const double e = dot(vv, w0);
    const double sc = (b * e - d) / denom;  // distance along uu from a1
    const double tc = (e - b * d) / denom;  // distance along vv from b1

    const Vec3 p1 = a1 + uu * sc;
    const Vec3 p2 = b1 + vv * tc;
    if (distance(p1, p2) > tol) {
        return std::nullopt;  // lines' closest points too far apart -- non-coplanar
    }

    // Strictly interior to both segments by more than tol -- rejects a
    // shared-endpoint touch and an out-of-span crossing alike (see header).
    if (sc <= tol || sc >= lenU - tol) {
        return std::nullopt;
    }
    if (tc <= tol || tc >= lenV - tol) {
        return std::nullopt;
    }

    return (p1 + p2) * 0.5;
}

std::optional<Vec3> segmentFacePlaneIntersect(Vec3 a, Vec3 b, Vec3 planePoint, Vec3 planeNormal, double tol) {
    const Vec3 dir = b - a;
    const double lenDir = length(dir);
    if (lenDir < kEps) {
        return std::nullopt;  // degenerate segment
    }

    // normalized() returns the zero vector for a near-zero planeNormal,
    // which drives denom to 0 below and falls into the "parallel" nullopt --
    // a degenerate plane normal has no well-defined crossing either.
    const Vec3 n = normalized(planeNormal);
    const double denom = dot(n, dir);
    if (std::fabs(denom) < kEps) {
        return std::nullopt;  // parallel to (or lying within) the plane
    }

    const double t = dot(n, planePoint - a) / denom;
    const double distAlong = t * lenDir;  // real distance from a along the segment
    if (distAlong < -tol || distAlong > lenDir + tol) {
        return std::nullopt;  // crossing falls outside the segment
    }

    return a + dir * t;
}

}  // namespace plnr::geo
