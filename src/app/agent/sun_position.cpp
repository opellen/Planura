#include "agent/sun_position.h"

#include <algorithm>
#include <cmath>

namespace plnr::agent::sun {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr double toRadians(double deg) {
    return deg * kPi / 180.0;
}

constexpr double toDegrees(double rad) {
    return rad * 180.0 / kPi;
}

// Cumulative days before each month starts (non-leap-year), index [month-1]. An out-of-range
// day rolls harmlessly into the next month's range.
constexpr int kDaysBeforeMonth[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

int dayOfYear(int month, int day) {
    const int clampedMonth = std::clamp(month, 1, 12);
    return kDaysBeforeMonth[clampedMonth - 1] + day;
}

// Cooper's equation -- solar declination in degrees for the given
// day-of-year (1-based, ~1-366).
double solarDeclinationDeg(int n) {
    return 23.45 * std::sin(toRadians(360.0 / 365.0 * (284.0 + static_cast<double>(n))));
}

}  // namespace

SolarAngles solarAngles(double latitudeDeg, int month, int day, double hourLocal) {
    const int n = dayOfYear(month, day);
    const double declDeg = solarDeclinationDeg(n);
    const double hourAngleDeg = 15.0 * (hourLocal - 12.0);

    const double latRad = toRadians(latitudeDeg);
    const double declRad = toRadians(declDeg);
    const double hourAngleRad = toRadians(hourAngleDeg);

    // sin(altitude) = sin(lat)sin(decl) + cos(lat)cos(decl)cos(H), clamped into [-1,1] before asin
    // as a defensive guard against float drift (a NaN on rare boundary cases).
    const double sinAlt = std::clamp(std::sin(latRad) * std::sin(declRad) + std::cos(latRad) * std::cos(declRad) *
                                                                                 std::cos(hourAngleRad),
                                      -1.0, 1.0);
    const double altRad = std::asin(sinAlt);
    const double altDeg = toDegrees(altRad);

    // cos(azimuth) = (sin(decl) - sin(lat)sin(altitude)) / (cos(lat)cos(altitude)); vanishes at
    // the poles or zenith/nadir, where azimuth is undefined -- falls back to due south (180deg).
    const double denom = std::cos(latRad) * std::cos(altRad);
    double azimuthDeg = 180.0;
    if (std::fabs(denom) > 1e-9) {
        const double cosAz = std::clamp((std::sin(declRad) - std::sin(latRad) * sinAlt) / denom, -1.0, 1.0);
        const double azRawDeg = toDegrees(std::acos(cosAz));
        // Morning (hour angle < 0): stays on acos's [0,180] (east-through-south) branch.
        // Afternoon: mirrored onto [180,360] (west-through-south) -- resolves acos's two-fold ambiguity.
        azimuthDeg = hourAngleDeg > 0.0 ? 360.0 - azRawDeg : azRawDeg;
    }

    return SolarAngles{altDeg, azimuthDeg};
}

geo::Vec3 sunDirection(const SolarAngles& angles) {
    if (angles.altitudeDeg < 0.0) {
        return geo::Vec3{0.0, 0.0, 0.0};  // night -- see this file's own header comment
    }
    const double altRad = toRadians(angles.altitudeDeg);
    const double azRad = toRadians(angles.azimuthDeg);
    const double cosAlt = std::cos(altRad);
    // East=+X, North=+Y, Up=+Z (this file's own top-comment convention).
    return geo::Vec3{cosAlt * std::sin(azRad), cosAlt * std::cos(azRad), std::sin(altRad)};
}

}  // namespace plnr::agent::sun
