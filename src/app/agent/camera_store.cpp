#include "agent/camera_store.h"

#include <cmath>
#include <cstddef>
#include <string>

namespace plnr::agent {

namespace {

// Session-only view-history cap; the oldest entry is evicted past it.
constexpr std::size_t kHistoryCap = 50;

// Pushes value, evicting the OLDEST entry once past kHistoryCap.
void pushCapped(std::vector<CameraState>& stack, const CameraState& value) {
    stack.push_back(value);
    if (stack.size() > kHistoryCap) stack.erase(stack.begin());
}

// Exact equality: both sides are copies or fresh states, no arithmetic in between.
bool cameraStateEqual(const CameraState& a, const CameraState& b) {
    return a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z &&
           a.azimuthDeg == b.azimuthDeg && a.elevationDeg == b.elevationDeg && a.distance == b.distance &&
           a.fovYDeg == b.fovYDeg && a.projection == b.projection;
}

// Azimuth/elevation per StandardView, re-derived from viewport::Camera's convention (agent/ never depends on viewport/).
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
// Mirrors viewport::Camera's kMaxElevationDeg; stand-in for exact +-90 on Top/Bottom.
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
    return {45.0, std::atan(1.0 / std::sqrt(2.0)) * kRadToDeg};
}

}  // namespace

CameraStore::CameraStore() : Agent(std::string(kCameraStoreName)) {}

void CameraStore::set(const CameraState& state) {
    // A real move completes a view change: push the PRE-change state, only if it changed.
    if (!cameraStateEqual(state_, state)) pushHistoryEntry(state_);
    state_ = state;
    // Silent: no event.
}

const CameraState& CameraStore::state() const {
    return state_;
}

void CameraStore::restoreCamera(const CameraState& state) {
    state_ = state;
    send(events::CameraChanged{});
}

void CameraStore::setProjection(events::Projection projection) {
    if (state_.projection == projection) return;
    pushHistoryEntry(state_);
    state_.projection = projection;
    send(events::CameraChanged{});
}

void CameraStore::setStandardView(events::StandardView view) {
    const AzEl angles = standardViewAngles(view);
    // Computed before the no-op check so matching az/el can't mask a TwoPoint->Perspective downgrade.
    const events::Projection newProjection =
        state_.projection == events::Projection::TwoPoint ? events::Projection::Perspective : state_.projection;
    if (state_.azimuthDeg == angles.azimuthDeg && state_.elevationDeg == angles.elevationDeg &&
        state_.projection == newProjection) {
        return;
    }
    pushHistoryEntry(state_);
    state_.azimuthDeg = angles.azimuthDeg;
    state_.elevationDeg = angles.elevationDeg;
    state_.projection = newProjection;
    // target/distance carry over unchanged.
    send(events::CameraChanged{});
}

void CameraStore::previous() {
    if (backStack_.empty()) return;
    // Not pushHistoryEntry(): that would also clear forwardStack_.
    pushCapped(forwardStack_, state_);
    state_ = backStack_.back();
    backStack_.pop_back();
    send(events::CameraChanged{});
}

void CameraStore::next() {
    if (forwardStack_.empty()) return;
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

void CameraSyncCommand::execute(const events::CameraNavigated& event, ordo::core::CommandContext& context) {
    auto camera = context.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;

    const CameraState before = camera->state();

    // TwoPoint exits to Perspective on orbit (az/el are the only fields an orbit changes).
    const bool orbited = event.azimuthDeg != before.azimuthDeg || event.elevationDeg != before.elevationDeg;
    const events::Projection afterProjection =
        (before.projection == events::Projection::TwoPoint && orbited) ? events::Projection::Perspective
                                                                         : before.projection;
    const CameraState after{event.target, event.azimuthDeg, event.elevationDeg, event.distance, event.fovYDeg,
                             afterProjection};
    camera->set(after);

    // set() sends nothing; a projection downgrade must fire CameraChanged so the viewport's live Camera drops TwoPoint.
    if (afterProjection != before.projection) {
        context.send(events::CameraChanged{});
    }

    // CameraMoved triggers edge-style reclassification; fires only if the camera actually moved.
    if (before.target.x != after.target.x || before.target.y != after.target.y || before.target.z != after.target.z ||
        before.azimuthDeg != after.azimuthDeg || before.elevationDeg != after.elevationDeg ||
        before.distance != after.distance || before.fovYDeg != after.fovYDeg) {
        context.send(events::CameraMoved{});
    }
}

void SetProjectionCommand::execute(const events::SetProjectionRequested& event, ordo::core::CommandContext& context) {
    auto camera = context.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;
    camera->setProjection(event.projection);
}

void SetStandardViewCommand::execute(const events::SetStandardViewRequested& event, ordo::core::CommandContext& context) {
    auto camera = context.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;
    camera->setStandardView(event.view);
}

void CameraPreviousCommand::execute(const events::CameraPreviousRequested& /*event*/, ordo::core::CommandContext& context) {
    auto camera = context.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;
    camera->previous();
}

void CameraNextCommand::execute(const events::CameraNextRequested& /*event*/, ordo::core::CommandContext& context) {
    auto camera = context.agentAs<CameraStore>(kCameraStoreName);
    if (!camera) return;
    camera->next();
}

}  // namespace plnr::agent
