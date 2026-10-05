#include "camera.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QVector4D>
#include <QtMath>

namespace plnr::viewport {

namespace {

constexpr float kMinDistance = 0.05f;
constexpr float kMaxDistance = 5000.0f;
constexpr float kMaxElevationDeg = 89.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 10000.0f;
constexpr float kZoomFactorPerStep = 0.9f;
// Headroom so zoomExtents keeps box corners off the frustum edges.
constexpr float kZoomExtentsMargin = 1.05f;

// Direction from target to eye for a given azimuth/elevation (unit length,
// distance-independent). Azimuth rotates around +Z starting from +X toward
// +Y; elevation tilts up from the XY ground plane.
QVector3D eyeDirection(float azimuthDeg, float elevationDeg) {
  const float az = qDegreesToRadians(azimuthDeg);
  const float el = qDegreesToRadians(elevationDeg);
  const float cosEl = std::cos(el);
  return QVector3D(cosEl * std::cos(az), cosEl * std::sin(az), std::sin(el));
}

// Camera-space right/up basis for an orientation (target-independent).
void basisVectors(float azimuthDeg, float elevationDeg, QVector3D &outRight,
                  QVector3D &outUp) {
  const QVector3D forward =
      -eyeDirection(azimuthDeg, elevationDeg); // eye -> target
  const QVector3D worldUp(0.0f, 0.0f, 1.0f);
  outRight = QVector3D::crossProduct(forward, worldUp).normalized();
  outUp = QVector3D::crossProduct(outRight, forward).normalized();
}

} // namespace

QVector3D Camera::eye() const {
  return target_ + eyeDirection(azimuthDeg_, elevationDeg_) * distance_;
}

QMatrix4x4 Camera::viewMatrix() const {
  QMatrix4x4 m;
  if (projection_ == Projection::TwoPoint) {
    // eye() is unchanged; only the look direction is forced level.
    const QVector3D eyePos = eye();
    const QVector3D levelForward = -eyeDirection(azimuthDeg_, 0.0f);
    m.lookAt(eyePos, eyePos + levelForward, QVector3D(0.0f, 0.0f, 1.0f));
  } else {
    m.lookAt(eye(), target_, QVector3D(0.0f, 0.0f, 1.0f));
  }
  return m;
}

QMatrix4x4 Camera::projectionMatrix(float aspect) const {
  QMatrix4x4 m;
  if (projection_ == Projection::Parallel) {
    const float halfHeight =
        distance_ * std::tan(qDegreesToRadians(fovYDeg_) * 0.5f);
    const float halfWidth = halfHeight * aspect;
    m.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, kNearPlane,
            kFarPlane);
  } else if (projection_ == Projection::TwoPoint) {
    // Lens-shift frustum: only the vertical window shifts.
    const float halfHeight =
        kNearPlane * std::tan(qDegreesToRadians(fovYDeg_) * 0.5f);
    const float halfWidth = halfHeight * aspect;
    const float shiftAtNear =
        kNearPlane * std::tan(qDegreesToRadians(elevationDeg_));
    m.frustum(-halfWidth, halfWidth, -halfHeight - shiftAtNear,
              halfHeight - shiftAtNear, kNearPlane, kFarPlane);
  } else {
    m.perspective(fovYDeg_, aspect, kNearPlane, kFarPlane);
  }
  return m;
}

void Camera::orbit(float dAzimuthDeg, float dElevationDeg) {
  azimuthDeg_ += dAzimuthDeg;
  elevationDeg_ = std::clamp(elevationDeg_ + dElevationDeg, -kMaxElevationDeg,
                             kMaxElevationDeg);
}

void Camera::pan(float dxPixels, float dyPixels, float viewportHeightPixels) {
  if (viewportHeightPixels <= 0.0f)
    return;

  QVector3D right, up;
  basisVectors(azimuthDeg_, elevationDeg_, right, up);

  // Full viewport height = visible world height at the target plane, so the
  // point under the cursor tracks the cursor.
  const float visibleHeight =
      2.0f * distance_ * std::tan(qDegreesToRadians(fovYDeg_) * 0.5f);
  const float scale = visibleHeight / viewportHeightPixels;

  target_ += (-right * dxPixels + up * dyPixels) * scale;
}

void Camera::zoom(float steps) {
  distance_ = std::clamp(distance_ * std::pow(kZoomFactorPerStep, steps),
                         kMinDistance, kMaxDistance);
}

void Camera::zoomToward(const QVector3D &anchor, float steps) {
  const float f = std::pow(kZoomFactorPerStep, steps);
  // Clamp the DISTANCE first, then derive the ratio: clamping f directly lets
  // target_ slide past anchor once distance_ hits its limit.
  const float fClamped =
      std::clamp(distance_ * f, kMinDistance, kMaxDistance) / distance_;
  target_ = anchor + (target_ - anchor) * fClamped;
  distance_ *= fClamped;
}

void Camera::zoomExtents(const QVector3D &bboxMin, const QVector3D &bboxMax,
                         float aspect) {
  target_ = (bboxMin + bboxMax) * 0.5f;

  QVector3D right, up;
  basisVectors(azimuthDeg_, elevationDeg_, right, up);
  const QVector3D eyeDir =
      eyeDirection(azimuthDeg_, elevationDeg_); // target -> eye

  // Orientation fixed: for each corner, find the distance putting it exactly
  // on the frustum edge, take the max. Depth-from-eye (distance_ - dot(offset,
  // eyeDir)) varies by corner, so a plain bounding-radius fit would miss it.
  const std::array<QVector3D, 8> corners = {
      QVector3D(bboxMin.x(), bboxMin.y(), bboxMin.z()),
      QVector3D(bboxMax.x(), bboxMin.y(), bboxMin.z()),
      QVector3D(bboxMin.x(), bboxMax.y(), bboxMin.z()),
      QVector3D(bboxMax.x(), bboxMax.y(), bboxMin.z()),
      QVector3D(bboxMin.x(), bboxMin.y(), bboxMax.z()),
      QVector3D(bboxMax.x(), bboxMin.y(), bboxMax.z()),
      QVector3D(bboxMin.x(), bboxMax.y(), bboxMax.z()),
      QVector3D(bboxMax.x(), bboxMax.y(), bboxMax.z()),
  };

  const float tanHalfFovY = std::tan(qDegreesToRadians(fovYDeg_) * 0.5f);
  const float tanHalfFovX = tanHalfFovY * aspect;

  float requiredDistance = 0.0f;
  for (const QVector3D &corner : corners) {
    const QVector3D offset = corner - target_;
    const float depthOffset = QVector3D::dotProduct(offset, eyeDir);
    const float rightComp = std::abs(QVector3D::dotProduct(offset, right));
    const float upComp = std::abs(QVector3D::dotProduct(offset, up));
    if (tanHalfFovX > 0.0f)
      requiredDistance =
          std::max(requiredDistance, depthOffset + rightComp / tanHalfFovX);
    if (tanHalfFovY > 0.0f)
      requiredDistance =
          std::max(requiredDistance, depthOffset + upComp / tanHalfFovY);
  }

  distance_ = std::clamp(requiredDistance * kZoomExtentsMargin, kMinDistance,
                         kMaxDistance);
}

void Camera::setState(const QVector3D &target, float azimuthDeg,
                      float elevationDeg, float distance, float fovYDeg,
                      Projection projection) {
  target_ = target;
  azimuthDeg_ = azimuthDeg;
  elevationDeg_ = elevationDeg;
  distance_ = distance;
  fovYDeg_ = fovYDeg;
  projection_ = projection;
}

CameraRay Camera::rayThrough(double px, double py, int viewportW,
                             int viewportH) const {
  if (viewportW <= 0 || viewportH <= 0) {
    // Degenerate viewport: eye->target ray instead of dividing by zero.
    return CameraRay{eye(), (target_ - eye()).normalized()};
  }

  // Widget pixels (Qt top-left origin) -> NDC (bottom-left origin, [-1,1]).
  const float ndcX = static_cast<float>(2.0 * px / viewportW - 1.0);
  const float ndcY = static_cast<float>(1.0 - 2.0 * py / viewportH);

  const float aspect =
      static_cast<float>(viewportW) / static_cast<float>(viewportH);
  const QMatrix4x4 invViewProj =
      (projectionMatrix(aspect) * viewMatrix()).inverted();

  QVector4D nearClip = invViewProj * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
  QVector4D farClip = invViewProj * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
  if (qFuzzyIsNull(nearClip.w()) || qFuzzyIsNull(farClip.w())) {
    return CameraRay{eye(), (target_ - eye()).normalized()};
  }
  nearClip /= nearClip.w();
  farClip /= farClip.w();

  const QVector3D origin = nearClip.toVector3D();
  const QVector3D dir = (farClip.toVector3D() - origin).normalized();
  return CameraRay{origin, dir};
}

double Camera::worldPerPixel(double distance, int viewportHeight) const {
  if (viewportHeight <= 0)
    return 0.0;
  const double tanHalfFovY =
      std::tan(qDegreesToRadians(static_cast<double>(fovYDeg_)) * 0.5);
  return 2.0 * distance * tanHalfFovY / viewportHeight;
}

} // namespace plnr::viewport
