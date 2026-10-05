// Math-only tests for plnr::viewport::Camera. Links Qt6::Gui directly (for
// QVector3D/QMatrix4x4) rather than the app or ordo_qt targets, since Camera
// itself has no other dependency.

#include "viewport/camera.h"

#include <cmath>

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <gtest/gtest.h>

namespace {

using plnr::viewport::Camera;
using plnr::viewport::CameraRay;

constexpr float kEpsilon = 1e-3f;

TEST(CameraTest, InitialEyeIsInExpectedOctant) {
    Camera camera;
    // Default: target (0,0,0), azimuth -60 deg, elevation 7 deg (the reference modeler's
    // shallow-pitch default view) -- eye should land in the +X, -Y, +Z
    // octant (front-right, slightly above).
    const QVector3D eye = camera.eye();
    EXPECT_GT(eye.x(), 0.0f);
    EXPECT_LT(eye.y(), 0.0f);
    EXPECT_GT(eye.z(), 0.0f);
}

TEST(CameraTest, OrbitClampsElevationToPlusMinus89) {
    Camera camera;
    camera.orbit(0.0f, 1000.0f);
    EXPECT_NEAR(camera.elevationDeg(), 89.0f, kEpsilon);

    camera.orbit(0.0f, -2000.0f);
    EXPECT_NEAR(camera.elevationDeg(), -89.0f, kEpsilon);
}

TEST(CameraTest, ZoomMultipliesDistanceByPointNinePerStep) {
    Camera camera;
    const float initialDistance = camera.distance();

    camera.zoom(1.0f);
    EXPECT_NEAR(camera.distance(), initialDistance * 0.9f, kEpsilon);

    camera.zoom(1.0f);
    EXPECT_NEAR(camera.distance(), initialDistance * 0.9f * 0.9f, kEpsilon);
}

TEST(CameraTest, ZoomClampsToConfiguredRange) {
    Camera camera;
    camera.zoom(1000.0f);
    EXPECT_NEAR(camera.distance(), 0.05f, kEpsilon);

    camera.zoom(-1000.0f);
    EXPECT_NEAR(camera.distance(), 5000.0f, kEpsilon);
}

TEST(CameraTest, PanMovesTargetPerpendicularToViewDirection) {
    Camera camera;
    const QVector3D oldTarget = camera.target();
    const QVector3D viewDir = (camera.target() - camera.eye()).normalized();

    camera.pan(30.0f, -15.0f, 600.0f);

    const QVector3D delta = camera.target() - oldTarget;
    EXPECT_GT(delta.length(), 0.0f);
    EXPECT_NEAR(QVector3D::dotProduct(delta, viewDir), 0.0f, kEpsilon);
}

TEST(CameraTest, ZoomExtentsCentersTargetOnBoundingBoxCenter) {
    Camera camera;
    const QVector3D bmin(-10.0f, -10.0f, 0.0f);
    const QVector3D bmax(10.0f, 10.0f, 2.0f);

    camera.zoomExtents(bmin, bmax, 16.0f / 9.0f);

    const QVector3D expectedCenter = (bmin + bmax) * 0.5f;
    EXPECT_NEAR(camera.target().x(), expectedCenter.x(), kEpsilon);
    EXPECT_NEAR(camera.target().y(), expectedCenter.y(), kEpsilon);
    EXPECT_NEAR(camera.target().z(), expectedCenter.z(), kEpsilon);
}

TEST(CameraTest, ZoomExtentsFitsBoundingBoxInsideFrustum) {
    Camera camera;
    const QVector3D bmin(-10.0f, -10.0f, 0.0f);
    const QVector3D bmax(10.0f, 10.0f, 2.0f);
    const float aspect = 16.0f / 9.0f;

    camera.zoomExtents(bmin, bmax, aspect);

    const QMatrix4x4 vp = camera.projectionMatrix(aspect) * camera.viewMatrix();

    const QVector3D corners[] = {
        QVector3D(bmin.x(), bmin.y(), bmin.z()),
        QVector3D(bmax.x(), bmax.y(), bmax.z()),
        QVector3D(bmin.x(), bmax.y(), bmin.z()),
        QVector3D(bmax.x(), bmin.y(), bmax.z()),
    };

    for (const QVector3D& corner : corners) {
        const QVector4D clip = vp * QVector4D(corner, 1.0f);
        ASSERT_GT(clip.w(), 0.0f);
        const float ndcX = clip.x() / clip.w();
        const float ndcY = clip.y() / clip.w();
        const float ndcZ = clip.z() / clip.w();
        EXPECT_LE(std::abs(ndcX), 1.0f);
        EXPECT_LE(std::abs(ndcY), 1.0f);
        EXPECT_LE(std::abs(ndcZ), 1.0f);
    }
}

TEST(CameraTest, RayThroughCenterPixelMatchesEyeToTargetDirection) {
    Camera camera;
    constexpr int kViewportW = 800;
    constexpr int kViewportH = 600;

    const CameraRay ray = camera.rayThrough(kViewportW / 2.0, kViewportH / 2.0, kViewportW, kViewportH);
    const QVector3D expectedDir = (camera.target() - camera.eye()).normalized();

    EXPECT_NEAR(ray.dir.x(), expectedDir.x(), kEpsilon);
    EXPECT_NEAR(ray.dir.y(), expectedDir.y(), kEpsilon);
    EXPECT_NEAR(ray.dir.z(), expectedDir.z(), kEpsilon);

    // The near point (ray origin) should also lie on the same eye->target
    // line, in front of the eye.
    const QVector3D toOrigin = (ray.origin - camera.eye()).normalized();
    EXPECT_NEAR(toOrigin.x(), expectedDir.x(), kEpsilon);
    EXPECT_NEAR(toOrigin.y(), expectedDir.y(), kEpsilon);
    EXPECT_NEAR(toOrigin.z(), expectedDir.z(), kEpsilon);
}

TEST(CameraTest, WorldPerPixelScalesLinearlyWithDistance) {
    Camera camera;
    constexpr int kViewportH = 600;

    const double perPixelAt10 = camera.worldPerPixel(10.0, kViewportH);
    const double perPixelAt20 = camera.worldPerPixel(20.0, kViewportH);

    EXPECT_GT(perPixelAt10, 0.0);
    EXPECT_NEAR(perPixelAt20, perPixelAt10 * 2.0, 1e-6);
}

}  // namespace
