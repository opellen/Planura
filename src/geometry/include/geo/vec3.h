#pragma once

#include <cmath>

namespace plnr::geo {

inline constexpr double kMergeTol = 1e-4;  // vertex coincidence (~0.1 mm at 1 unit = 1 m)
inline constexpr double kPlaneTol = 1e-4;  // coplanarity distance
inline constexpr double kEps = 1e-9;       // degenerate guards

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator-(const Vec3& a) {
    return {-a.x, -a.y, -a.z};
}

inline Vec3 operator*(const Vec3& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

inline Vec3 operator*(double s, const Vec3& a) {
    return a * s;
}

inline double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline double lengthSq(const Vec3& a) {
    return dot(a, a);
}

inline double length(const Vec3& a) {
    return std::sqrt(lengthSq(a));
}

// Returns the zero vector when a's length is below kEps (degenerate guard).
inline Vec3 normalized(const Vec3& a) {
    const double len = length(a);
    if (len < kEps) {
        return {0.0, 0.0, 0.0};
    }
    return a * (1.0 / len);
}

inline double distance(const Vec3& a, const Vec3& b) {
    return length(a - b);
}

inline bool almostEqual(const Vec3& a, const Vec3& b, double tol = kMergeTol) {
    return distance(a, b) <= tol;
}

}  // namespace plnr::geo
