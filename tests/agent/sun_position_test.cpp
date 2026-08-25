#include "agent/sun_position.h"

#include <gtest/gtest.h>

namespace {

using plnr::agent::sun::solarAngles;
using plnr::agent::sun::SolarAngles;
using plnr::agent::sun::sunDirection;

constexpr double kTol = 1e-6;

// Day-of-year 81 (March 22) makes Cooper's equation's declination land
// exactly on a multiple of 360deg -- declination == 0 exactly, a clean
// equinox with no fuzziness needed.
constexpr int kEquinoxMonth = 3;
constexpr int kEquinoxDay = 22;

TEST(SunPositionTest, EquatorEquinoxNoonIsOverhead) {
    const SolarAngles angles = solarAngles(0.0, kEquinoxMonth, kEquinoxDay, 12.0);
    EXPECT_NEAR(angles.altitudeDeg, 90.0, 0.1);
}

TEST(SunPositionTest, NorthernWinterNoonIsLowerThanSummerNoon) {
    // Seoul-ish latitude (agent::kDefaultLatitudeDeg's own value) --
    // northern hemisphere, so a positive declination (summer) sits the sun
    // higher at noon than a negative one (winter).
    constexpr double kLatitudeDeg = 37.57;
    const SolarAngles summerNoon = solarAngles(kLatitudeDeg, 6, 21, 12.0);   // near summer solstice
    const SolarAngles winterNoon = solarAngles(kLatitudeDeg, 12, 21, 12.0);  // near winter solstice
    EXPECT_GT(summerNoon.altitudeDeg, winterNoon.altitudeDeg);
    // Sanity: both still plausible altitudes (not below the horizon at noon
    // for a mid-latitude site).
    EXPECT_GT(summerNoon.altitudeDeg, 0.0);
    EXPECT_GT(winterNoon.altitudeDeg, 0.0);
}

// Equinox sunrise/sunset sits at exactly +-90deg hour angle regardless of
// latitude -- hourLocal 6.0 lands altitude near the horizon and azimuth at
// due east, cross-checked at two latitudes.
TEST(SunPositionTest, EquinoxMorningSunSitsDueEastNearTheHorizon) {
    for (const double lat : {0.0, 37.57}) {
        const SolarAngles angles = solarAngles(lat, kEquinoxMonth, kEquinoxDay, 6.0);
        EXPECT_NEAR(angles.altitudeDeg, 0.0, 0.1) << "lat=" << lat;
        EXPECT_NEAR(angles.azimuthDeg, 90.0, 0.1) << "lat=" << lat;
    }
}

// World-space convention: east=+X, north=+Y, up=+Z. Hand-constructs
// SolarAngles{0,90} directly rather than deriving from the H=-90deg sunrise
// instant, which is numerically UNSTABLE (cos(-90deg)'s sign can flip and
// trip sunDirection's night cutoff).
TEST(SunPositionTest, MorningSunDirectionPointsAlongPositiveX) {
    const SolarAngles angles{0.0, 90.0};
    const plnr::geo::Vec3 dir = sunDirection(angles);
    EXPECT_NEAR(dir.x, 1.0, 0.05);
    EXPECT_NEAR(dir.y, 0.0, 0.05);
    EXPECT_NEAR(dir.z, 0.0, 0.05);
}

// Solar noon on the equinox at the equator: altitude 90 deg (overhead) --
// direction should be (approximately) straight up, +Z, regardless of
// whatever azimuth the (degenerate, sun-at-zenith) computation reports.
TEST(SunPositionTest, OverheadSunDirectionPointsAlongPositiveZ) {
    const SolarAngles angles = solarAngles(0.0, kEquinoxMonth, kEquinoxDay, 12.0);
    const plnr::geo::Vec3 dir = sunDirection(angles);
    EXPECT_NEAR(dir.x, 0.0, 0.05);
    EXPECT_NEAR(dir.y, 0.0, 0.05);
    EXPECT_NEAR(dir.z, 1.0, 0.05);
}

// Midnight on the equinox puts the sun well below the horizon -- altitude
// must read negative, and sunDirection must collapse to the exact zero
// vector (the "no special-case downstream" encoding of night).
TEST(SunPositionTest, NightAltitudeIsNegativeAndDirectionIsZero) {
    const SolarAngles angles = solarAngles(37.57, kEquinoxMonth, kEquinoxDay, 0.0);
    ASSERT_LT(angles.altitudeDeg, 0.0);

    const plnr::geo::Vec3 dir = sunDirection(angles);
    EXPECT_EQ(dir.x, 0.0);
    EXPECT_EQ(dir.y, 0.0);
    EXPECT_EQ(dir.z, 0.0);
}

// sunDirection always returns a unit vector (or the night-time zero vector)
// -- spot-checked at an arbitrary daytime angle pair, not just the axis-
// aligned cases above.
TEST(SunPositionTest, DaytimeDirectionIsUnitLength) {
    const SolarAngles angles = solarAngles(37.57, 6, 15, 15.0);
    ASSERT_GE(angles.altitudeDeg, 0.0);
    const plnr::geo::Vec3 dir = sunDirection(angles);
    const double lenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    EXPECT_NEAR(lenSq, 1.0, kTol);
}

}  // namespace
