#include <geo/shapes.h>

#include <cmath>

namespace plnr::geo {

namespace {

inline constexpr double kPi = 3.14159265358979323846;

// Some unit vector perpendicular to n (n assumed already normalized and
// nonzero) -- picks whichever world axis is least parallel to n so the
// cross product below can't degenerate.
Vec3 arbitraryPerpendicular(const Vec3& n) {
    const Vec3 axis = std::fabs(n.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    return normalized(cross(n, axis));
}

// Wraps an angle (radians) into [0, 2*pi).
double wrapAngle(double angle) {
    double a = std::fmod(angle, 2.0 * kPi);
    if (a < 0.0) {
        a += 2.0 * kPi;
    }
    return a;
}

// Signed sweep from 0 to `endAngle` (both already wrapped into [0, 2*pi))
// that passes through `throughAngle` -- picks CCW vs CW so the swept range
// actually contains the angle that must lie on the arc.
double sweepThrough(double endAngle, double throughAngle) {
    return throughAngle <= endAngle + kEps ? endAngle : endAngle - 2.0 * kPi;
}

// Samples segments+1 points around `center` at radius `r`, in the orthonormal
// plane basis (u, v), starting at angle 0 (== center + r*u) and sweeping
// through the signed angle `sweep`.
std::vector<Vec3> sampleArc(const Vec3& center, double r, const Vec3& u, const Vec3& v, double sweep, int segments) {
    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = sweep * static_cast<double>(i) / static_cast<double>(segments);
        points.push_back(center + (u * std::cos(angle) + v * std::sin(angle)) * r);
    }
    return points;
}

}  // namespace

std::vector<Vec3> regularPolygonPoints(Vec3 center, double radius, Vec3 normal, int segments, Vec3 startDir,
                                        bool circumscribed) {
    if (segments < 3 || radius <= kMergeTol || length(normal) < kEps) {
        return {};
    }

    const Vec3 n = normalized(normal);

    // Project startDir into the plane and normalize into the basis's u axis;
    // fall back to an arbitrary perpendicular when the projection is
    // degenerate (startDir zero, or parallel to normal).
    const Vec3 proj = startDir - n * dot(startDir, n);
    const double projLen = length(proj);
    const Vec3 u = projLen < kEps ? arbitraryPerpendicular(n) : proj * (1.0 / projLen);
    const Vec3 v = normalized(cross(n, u));

    const double r = circumscribed ? radius / std::cos(kPi / segments) : radius;

    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double angle = 2.0 * kPi * i / segments;
        points.push_back(center + (u * std::cos(angle) + v * std::sin(angle)) * r);
    }
    return points;
}

std::vector<Vec3> arcPoints2Point(Vec3 a, Vec3 b, double bulge, Vec3 normal, int segments) {
    const Vec3 chord = b - a;
    const double chordLen = length(chord);
    const double s = std::fabs(bulge);
    if (chordLen <= kMergeTol || s <= kMergeTol || segments < 1 || length(normal) < kEps) {
        return {};
    }

    const Vec3 n = normalized(normal);
    const Vec3 chordDir = chord * (1.0 / chordLen);
    const Vec3 bulgeCross = cross(n, chordDir);
    if (length(bulgeCross) < kEps) {
        return {};  // normal parallel to the chord -- no plane to bulge into
    }

    const Vec3 bulgeAxis = normalized(bulgeCross);
    const Vec3 bulgeDir = bulge >= 0.0 ? bulgeAxis : -bulgeAxis;

    // Sagitta math: r = (h^2 + s^2) / (2*s); the center sits (r - s) from the
    // chord midpoint, away from the bulge (negative once s > h, the major-arc
    // case, which flips the center to the bulge's own side).
    const double h = chordLen * 0.5;
    const double r = (h * h + s * s) / (2.0 * s);

    const Vec3 mid = (a + b) * 0.5;
    const Vec3 center = mid - bulgeDir * (r - s);

    const Vec3 u = normalized(a - center);
    const Vec3 v = normalized(cross(n, u));

    const double endAngle = wrapAngle(std::atan2(dot(b - center, v), dot(b - center, u)));
    // The bulge point sits at center + r*bulgeDir (by construction above), so
    // its angle in the (u, v) basis is just bulgeDir's own direction.
    const double bulgeAngle = wrapAngle(std::atan2(dot(bulgeDir, v), dot(bulgeDir, u)));
    const double sweep = sweepThrough(endAngle, bulgeAngle);

    return sampleArc(center, r, u, v, sweep, segments);
}

std::vector<Vec3> arcPointsCenter(Vec3 center, Vec3 startPoint, double sweepRad, Vec3 normal, int segments) {
    const Vec3 v0 = startPoint - center;
    const double radius = length(v0);
    const double sweepAbs = std::fabs(sweepRad);
    if (radius <= kMergeTol || length(normal) < kEps || sweepAbs < kEps || sweepAbs > 2.0 * kPi + kEps ||
        segments < 1) {
        return {};
    }

    const Vec3 n = normalized(normal);

    // A full-turn sweep would put the last point exactly on the first;
    // return `segments` points (no duplicate) instead of segments+1 -- see
    // the full-sweep convention documented on the declaration.
    const bool isFullSweep = std::fabs(sweepAbs - 2.0 * kPi) <= kEps;
    const int count = isFullSweep ? segments : segments + 1;

    std::vector<Vec3> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double angle = sweepRad * static_cast<double>(i) / static_cast<double>(segments);
        const double c = std::cos(angle);
        const double sn = std::sin(angle);
        // Rodrigues' rotation formula: rotate v0 by `angle` around unit axis n.
        const Vec3 rotated = v0 * c + cross(n, v0) * sn + n * (dot(n, v0) * (1.0 - c));
        points.push_back(center + rotated);
    }
    return points;
}

std::vector<Vec3> arcPoints3Point(Vec3 p1, Vec3 p2, Vec3 p3, int segments) {
    if (segments < 1 || distance(p1, p2) <= kMergeTol || distance(p2, p3) <= kMergeTol ||
        distance(p1, p3) <= kMergeTol) {
        return {};
    }

    const Vec3 e1 = p2 - p1;
    const Vec3 e2 = p3 - p1;
    const Vec3 w = cross(e1, e2);
    const double wLenSq = lengthSq(w);
    // Collinearity guard, scale-independent: wLenSq / (|e1|^2 * |e2|^2) is
    // sin^2 of the angle between e1 and e2; reject once that angle is within
    // kEps of zero (or pi) -- no unique circle through three collinear points.
    if (wLenSq <= kEps * kEps * lengthSq(e1) * lengthSq(e2)) {
        return {};
    }

    // Circumcenter of the triangle, relative to p1 (standard vector formula).
    const double denom = 2.0 * wLenSq;
    const Vec3 center = p1 + (cross(w, e1) * lengthSq(e2) + cross(e2, w) * lengthSq(e1)) * (1.0 / denom);
    const double r = distance(center, p1);

    const Vec3 n = normalized(w);
    const Vec3 u = normalized(p1 - center);
    const Vec3 v = normalized(cross(n, u));

    const double endAngle = wrapAngle(std::atan2(dot(p3 - center, v), dot(p3 - center, u)));
    const double throughAngle = wrapAngle(std::atan2(dot(p2 - center, v), dot(p2 - center, u)));
    const double sweep = sweepThrough(endAngle, throughAngle);

    return sampleArc(center, r, u, v, sweep, segments);
}

}  // namespace plnr::geo
