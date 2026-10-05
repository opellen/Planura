#include "agent/sun_position.h"

#include <gtest/gtest.h>

namespace {

using plnr::agent::sun::solarAngles;
using plnr::agent::sun::SolarAngles;
using plnr::agent::sun::sunDirection;

constexpr double kTol = 1e-6;

// Day-of-year 81 (March 22): Cooper's declination is exactly 0, a clean equinox.
constexpr int kEquinoxMonth = 3;
constexpr int kEquinoxDay = 22;

TEST(SunPositionTest, EquatorEquinoxNoonIsOverhead) {
  const SolarAngles angles = solarAngles(0.0, kEquinoxMonth, kEquinoxDay, 12.0);
  EXPECT_NEAR(angles.altitudeDeg, 90.0, 0.1);
}

TEST(SunPositionTest, NorthernWinterNoonIsLowerThanSummerNoon) {
  // Seoul-ish latitude (agent::kDefaultLatitudeDeg); northern, so summer noon sun is higher than winter.
  constexpr double kLatitudeDeg = 37.57;
  const SolarAngles summerNoon =
      solarAngles(kLatitudeDeg, 6, 21, 12.0); // near summer solstice
  const SolarAngles winterNoon =
      solarAngles(kLatitudeDeg, 12, 21, 12.0); // near winter solstice
  EXPECT_GT(summerNoon.altitudeDeg, winterNoon.altitudeDeg);
  // Sanity: both altitudes are plausible (above the horizon at noon).
  EXPECT_GT(summerNoon.altitudeDeg, 0.0);
  EXPECT_GT(winterNoon.altitudeDeg, 0.0);
}

// Equinox sunrise/sunset is at +-90deg hour angle at any latitude: hourLocal 6.0 gives near-horizon
// altitude and due-east azimuth (checked at two latitudes).
TEST(SunPositionTest, EquinoxMorningSunSitsDueEastNearTheHorizon) {
  for (const double lat : {0.0, 37.57}) {
    const SolarAngles angles =
        solarAngles(lat, kEquinoxMonth, kEquinoxDay, 6.0);
    EXPECT_NEAR(angles.altitudeDeg, 0.0, 0.1) << "lat=" << lat;
    EXPECT_NEAR(angles.azimuthDeg, 90.0, 0.1) << "lat=" << lat;
  }
}

// World-space convention: east=+X, north=+Y, up=+Z. Uses SolarAngles{0,90} directly: the H=-90deg
// sunrise instant is numerically unstable (cos(-90deg) sign can flip into sunDirection's night cutoff).
TEST(SunPositionTest, MorningSunDirectionPointsAlongPositiveX) {
  const SolarAngles angles{0.0, 90.0};
  const plnr::geo::Vec3 dir = sunDirection(angles);
  EXPECT_NEAR(dir.x, 1.0, 0.05);
  EXPECT_NEAR(dir.y, 0.0, 0.05);
  EXPECT_NEAR(dir.z, 0.0, 0.05);
}

// Equinox solar noon at the equator: sun overhead, direction ~ +Z regardless of azimuth.
TEST(SunPositionTest, OverheadSunDirectionPointsAlongPositiveZ) {
  const SolarAngles angles = solarAngles(0.0, kEquinoxMonth, kEquinoxDay, 12.0);
  const plnr::geo::Vec3 dir = sunDirection(angles);
  EXPECT_NEAR(dir.x, 0.0, 0.05);
  EXPECT_NEAR(dir.y, 0.0, 0.05);
  EXPECT_NEAR(dir.z, 1.0, 0.05);
}

// Equinox midnight: altitude negative, sunDirection is the exact zero vector (night encoding).
TEST(SunPositionTest, NightAltitudeIsNegativeAndDirectionIsZero) {
  const SolarAngles angles =
      solarAngles(37.57, kEquinoxMonth, kEquinoxDay, 0.0);
  ASSERT_LT(angles.altitudeDeg, 0.0);

  const plnr::geo::Vec3 dir = sunDirection(angles);
  EXPECT_EQ(dir.x, 0.0);
  EXPECT_EQ(dir.y, 0.0);
  EXPECT_EQ(dir.z, 0.0);
}

// sunDirection is a unit vector (or zero at night), spot-checked at an arbitrary daytime angle.
TEST(SunPositionTest, DaytimeDirectionIsUnitLength) {
  const SolarAngles angles = solarAngles(37.57, 6, 15, 15.0);
  ASSERT_GE(angles.altitudeDeg, 0.0);
  const plnr::geo::Vec3 dir = sunDirection(angles);
  const double lenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
  EXPECT_NEAR(lenSq, 1.0, kTol);
}

} // namespace
