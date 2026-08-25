#pragma once

#include <geo/vec3.h>

// Pure sun-position math: {latitudeDeg, month, day, hourLocal} -> a world-space sun direction.
// World-space convention: Z-up, +Y=north, +X=east, right-handed ENU.
// MVP approximations: non-leap-year day table, hourLocal is apparent solar time (no timezone
// correction), Cooper's equation for declination (~1 degree accurate).
namespace plnr::agent::sun {

// altitude: degrees above the horizon (negative = night). azimuth: compass degrees clockwise
// from north (0=north, 90=east, 180=south, 270=west).
struct SolarAngles {
    double altitudeDeg{};
    double azimuthDeg{};
};

// Computes the sun's position for latitude (+north/-south) and calendar date/time (month 1-12,
// day 1-31, clamped defensively; hourLocal is apparent solar time).
SolarAngles solarAngles(double latitudeDeg, int month, int day, double hourLocal);

// World-space unit direction from a surface point TOWARD the sun (the L term for Lambertian N.L).
// Exactly {0,0,0} when altitudeDeg < 0 (night), giving ambient-only shading with no night branch.
geo::Vec3 sunDirection(const SolarAngles& angles);

}  // namespace plnr::agent::sun
