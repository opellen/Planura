#include "agent/camera_store.h"

#include <cmath>
#include <cstddef>
#include <string>

namespace plnr::agent {

namespace {

// Session-only view-history cap -- once a push would exceed it, the
// oldest entry (stack front) is evicted. Just "a round, generous number",
// unlike UndoStore's own persisted-forever cap (100).
constexpr std::size_t kHistoryCap = 50;

// Pushes value onto stack, evicting the OLDEST entry once that would
// exceed kHistoryCap -- shared by pushHistoryEntry() and by previous()/
// next()'s own opposite-stack push.
void pushCapped(std::vector<CameraState>& stack, const CameraState& value) {
    stack.push_back(value);
    if (stack.size() > kHistoryCap) stack.erase(stack.begin());
}

// Exact field-by-field equality -- both sides are always either a literal
// copy of state_ or a freshly-built CameraState with no intervening
// arithmetic, so exact comparison is safe here.
bool cameraStateEqual(const CameraState& a, const CameraState& b) {
    return a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z &&
           a.azimuthDeg == b.azimuthDeg && a.elevationDeg == b.elevationDeg && a.distance == b.distance &&
           a.fovYDeg == b.fovYDeg && a.projection == b.projection;
}

// Per-events::StandardView azimuth/elevation, independently re-derived
// from viewport::Camera's own eyeDirection/basisVectors convention
// (agent/ never depends on viewport/).
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
// Mirrors viewport::Camera's own kMaxElevationDeg orbit-clamp bound
// (viewport/camera.cpp) -- see this block's own comment above for why Top/
// Bottom need a safe stand-in for exact +-90.
constexpr double kSafeElevationDeg = 89.0;

struct AzEl {
    double azimuthDeg;
    double elevationDeg;
};

AzEl standardViewAngles(events::StandardView view) {
    switch (view) {
        case events::StandardView::Top:
            return {-90.0, kSafeElevationDeg};
        case events::StandardView::Bottom:
            return {-90.0, -kSafeElevationDeg};
        case events::StandardView::Front:
            return {-90.0, 0.0};
        case events::StandardView::Back:
            return {90.0, 0.0};
        case events::StandardView::Left:
            return {180.0, 0.0};
        case events::StandardView::Right:
            return {0.0, 0.0};
        case events::StandardView::Iso:
            return {45.0, std::atan(1.0 / std::sqrt(2.0)) * kRadToDeg};
    }
    return {45.0, std::atan(1.0 / std::sqrt(2.0)) * kRadToDeg};  // unreachable -- StandardView is exhaustively handled above
}

}  // namespace

CameraStore::CameraStore() : Agent(std::string(kCameraStoreName)) {}

void CameraStore::set(const CameraState& state) {
    // A sync that actually moves the camera completes a view change, so
    // the PRE-change state is worth a history entry -- gated on actual
    // change so a gesture ending back where it started doesn't pollute history.
    if (!cameraStateEqual(state_, state)) pushHistoryEntry(state_);
    state_ = state;
    // No event -- see this class's own header comment.
}

const CameraState& CameraStore::state() const {
    return state_;
}

void CameraStore::restoreCamera(const CameraState& state) {
    state_ = state;
    send(events::CameraChanged{});
}

void CameraStore::setProjection(events::Projection projection) {
    if (state_.projection == projection) return;  // no-op, same discipline as StyleStore::setFaceStyle
    pushHistoryEntry(state_);  // projection toggle completes a view change too
    state_.projection = projection;
    send(events::CameraChanged{});
}

void CameraStore::setStandardView(events::StandardView view) {
    const AzEl angles = standardViewAngles(view);
    // Computed BEFORE the no-op check below so the rare coincidence of
    // az/el already matching the target view doesn't mask a still-needed
    // TwoPoint->Perspective downgrade.
    const events::Projection newProjection =
        state_.projection == events::Projection::TwoPoint ? events::Projection::Perspective : state_.projection;
    if (state_.azimuthDeg == angles.azimuthDeg && state_.elevationDeg == angles.elevationDeg &&
        state_.projection == newProjection) {
        return;  // no-op, same discipline as setProjection() above
    }
    pushHistoryEntry(state_);  // pre-change snapshot
    state_.azimuthDeg = angles.azimuthDeg;
    state_.elevationDeg = angles.elevationDeg;
    state_.projection = newProjection;
    // target_/distance_ carry over unchanged -- see
    // events::SetStandardViewRequested's own comment.
    send(events::CameraChanged{});
}

void CameraStore::previous() {
    if (backStack_.empty()) return;  // silent no-op, see this method's own header comment
    // Deliberately NOT pushHistoryEntry(state_) -- that would also clear
    // forwardStack_, corrupting the stack this call is about to pop from.
    // Pushes the current state directly onto forwardStack_ instead.
    pushCapped(forwardStack_, state_);
    state_ = backStack_.back();
    backStack_.pop_back();
    send(events::CameraChanged{});
}

void CameraStore::next() {
    if (forwardStack_.empty()) return;  // silent no-op
    pushCapped(backStack_, state_);
    state_ = forwardStack_.back();
    forwardStack_.pop_back();
    send(events::CameraChanged{});
}

bool CameraStore::canGoBack() const {
    return !backStack_.empty();
}

bool CameraStore::canGoForward() const {
    return !forwardStack_.empty();
}

void CameraStore::pushHistoryEntry(const CameraState& preChangeState) {
    pushCapped(backStack_, preChangeState);
    forwardStack_.clear();
}

void CameraSyncCommand::execute(ordo::core::AppKernel& kernel, const events::CameraNavigated& event) {
    auto camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;  // No CameraStore registered on this kernel.

    const CameraState before = camera->state();

    // TwoPoint's "no third vanishing point" read depends on the CURRENT
    // azimuth/elevation -- orbiting re-introduces it, so it exits back to
    // Perspective. azimuthDeg/elevationDeg are the ONLY fields an orbit changes.
    const bool orbited = event.azimuthDeg != before.azimuthDeg || event.elevationDeg != before.elevationDeg;
    const events::Projection afterProjection =
        (before.projection == events::Projection::TwoPoint && orbited) ? events::Projection::Perspective
                                                                         : before.projection;
    const CameraState after{event.target, event.azimuthDeg, event.elevationDeg, event.distance, event.fovYDeg,
                             afterProjection};
    camera->set(after);

    // set() itself dispatches nothing -- correct for an ordinary sync. A
    // projection DOWNGRADE is different: the viewport's own live Camera
    // still thinks it's TwoPoint, so CameraChanged fires here to correct it.
    if (afterProjection != before.projection) {
        kernel.send(events::CameraChanged{});
    }

    // CameraMoved is ViewportPresenter's recompute trigger for edge-style
    // classification -- fired only when the gesture actually moved the
    // camera. Compared HERE so set()'s "dispatches nothing" stays untouched.
    if (before.target.x != after.target.x || before.target.y != after.target.y || before.target.z != after.target.z ||
        before.azimuthDeg != after.azimuthDeg || before.elevationDeg != after.elevationDeg ||
        before.distance != after.distance || before.fovYDeg != after.fovYDeg) {
        kernel.send(events::CameraMoved{});
    }
}

void SetProjectionCommand::execute(ordo::core::AppKernel& kernel, const events::SetProjectionRequested& event) {
    auto camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;  // No CameraStore registered on this kernel.
    camera->setProjection(event.projection);
}

void SetStandardViewCommand::execute(ordo::core::AppKernel& kernel, const events::SetStandardViewRequested& event) {
    auto camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;  // No CameraStore registered on this kernel.
    camera->setStandardView(event.view);
}

void CameraPreviousCommand::execute(ordo::core::AppKernel& kernel, const events::CameraPreviousRequested& /*event*/) {
    auto camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;  // No CameraStore registered on this kernel.
    camera->previous();
}

void CameraNextCommand::execute(ordo::core::AppKernel& kernel, const events::CameraNextRequested& /*event*/) {
    auto camera = kernel.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;  // No CameraStore registered on this kernel.
    camera->next();
}

}  // namespace plnr::agent
