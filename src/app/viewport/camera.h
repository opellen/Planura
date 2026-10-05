#pragma once

#include <QMatrix4x4>
#include <QVector3D>

namespace plnr::viewport {

// A world-space ray cast through a screen pixel; dir is unit length.
struct CameraRay {
  QVector3D origin;
  QVector3D dir;
};

// Orbit camera around a target point: Z-up, right-handed, ground plane XY
// (X=red, Y=green, Z=blue/up). Eye = target + spherical offset (azimuth
// around Z, elevation from the ground).
class Camera {
public:
  // Mirrors events::Projection. Entering/leaving TwoPoint is the domain
  // layer's job; this class only renders the mode it is given.
  enum class Projection { Perspective, Parallel, TwoPoint };

  Camera() = default;

  QVector3D target() const { return target_; }
  float azimuthDeg() const { return azimuthDeg_; }
  float elevationDeg() const { return elevationDeg_; }
  float distance() const { return distance_; }
  float fovYDeg() const { return fovYDeg_; }
  Projection projection() const { return projection_; }

  void setProjection(Projection projection) { projection_ = projection; }

  // World-space eye position, derived from target/azimuth/elevation/distance.
  QVector3D eye() const;

  // Perspective/Parallel: eye() looking straight at target_, world +Z up.
  // TwoPoint: the look direction is forced LEVEL (elevation zeroed), so
  // world verticals stay vertical on screen.
  QMatrix4x4 viewMatrix() const;

  // Perspective: fovYDeg_-driven frustum. Parallel: half-height =
  // distance_*tan(fovYDeg_/2), keeping the two modes scale-continuous.
  // TwoPoint: lens-shifts the frustum's vertical window to recenter target_.
  QMatrix4x4 projectionMatrix(float aspect) const;

  // Rotates the view around the target. Elevation is clamped to +-89 deg
  // so the camera never flips over the poles.
  void orbit(float dAzimuthDeg, float dElevationDeg);

  // Translates the target in the camera's right/up plane. Deltas are in
  // screen pixels; viewportHeightPixels converts them to world units so a
  // full-height drag matches the visible height at the target distance.
  void pan(float dxPixels, float dyPixels, float viewportHeightPixels);

  // Zoom TOOL semantics: distance *= 0.9 per step (positive = in), clamped,
  // target_ never moves. Scroll-wheel zoom is zoomToward, not this.
  void zoom(float steps);

  // Scroll-wheel zoom: world point `anchor` stays under the cursor. A
  // similarity transform about anchor -- clamp distance FIRST, then derive
  // the ratio; clamping the ratio would let target_ overshoot.
  void zoomToward(const QVector3D &anchor, float steps);

  // Recenters the target on the box's center and picks a distance that fits
  // the whole box in the current field of view; orientation unchanged.
  void zoomExtents(const QVector3D &bboxMin, const QVector3D &bboxMax,
                   float aspect);

  // Overwrites every field and does NOT re-clamp: input is a previously-valid
  // Camera state (CameraStore/.plr round-trip), not free-form user input.
  void setState(const QVector3D &target, float azimuthDeg, float elevationDeg,
                float distance, float fovYDeg, Projection projection);

  // Ray through screen pixel (px,py), Qt top-left origin: origin on the near
  // plane, dir normalized near-to-far. Valid in all three projection modes.
  CameraRay rayThrough(double px, double py, int viewportW,
                       int viewportH) const;

  // World-space size one screen pixel covers at `distance` from the eye --
  // converts pixel tolerances into zoom-independent world tolerances.
  // KNOWN CONCERN: perspective-shaped; under Parallel it should be constant.
  double worldPerPixel(double distance, int viewportHeight) const;

private:
  QVector3D target_{0.0f, 0.0f, 0.0f};
  float azimuthDeg_ = -60.0f;
  // Shallow default pitch (horizon ~0.39 NDC above center at fovY 35).
  // Keep in sync with agent::CameraState's default.
  float elevationDeg_ = 7.0f;
  float distance_ = 25.0f;
  float fovYDeg_ = 35.0f;
  Projection projection_ = Projection::Perspective;
};

} // namespace plnr::viewport
