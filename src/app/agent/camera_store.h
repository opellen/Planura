#pragma once

#include <string_view>
#include <vector>

#include <geo/vec3.h>
#include <ordo/core/app_kernel.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kCameraStoreName = "camera";

// Camera state snapshot mirroring ViewportWidget's own Camera 1:1, including its defaults.
// Field shape also mirrors plnr::io::CameraState, but agent/ never depends on io/, so this is
// CameraStore's own independent copy.
struct CameraState {
    geo::Vec3 target;
    double azimuthDeg{-60.0};
    // Mirrors viewport::Camera's SU-style shallow default pitch -- keep in sync.
    double elevationDeg{7.0};
    double distance{25.0};
    double fovYDeg{35.0};
    // Reuses events::Projection directly; io::CameraState's own projection field reuses it too.
    events::Projection projection{events::Projection::Perspective};
};

// Owns the viewport camera's current state. Qt-free, passive. set() is the live-navigation path
// and dispatches NOTHING ITSELF (the sender already shows the change live).
// restoreCamera() is the ONE exception: it always dispatches events::CameraChanged.
class CameraStore : public ordo::core::Agent {
public:
    CameraStore();

    void set(const CameraState& state);

    const CameraState& state() const;

    // -- Restore API ---------------------------------------------------------
    // File loader / new-document default only. Unlike every other Agent's Restore API, this
    // DOES dispatch (see class comment) -- always, not conditioned on actual change.
    void restoreCamera(const CameraState& state);

    // SetProjectionCommand's own target -- dispatches events::CameraChanged only on actual change
    // (re-click of the active entry is a no-op), and pushes the pre-change state onto the
    // view-history back stack when it changes something.
    void setProjection(events::Projection projection);

    // SetStandardViewCommand's own target -- sets azimuth/elevation only (target/distance carry
    // over). No-op unless azimuth/elevation or projection actually change; TwoPoint resets to
    // Perspective, every other projection carries over.
    void setStandardView(events::StandardView view);

    // -- View history --------------------------------------------------------
    // Session-only two-stack camera history (the reference modeler Camera > Previous/Next), NOT persisted to
    // .plr. set()/setStandardView()/setProjection() push the pre-change state onto the back stack
    // on a real change; previous()/next() never push -- applying history must never record more.

    // Restores the most recently pushed back-stack entry: pushes the current state onto the
    // forward stack, pops the back stack into state_, dispatches events::CameraChanged.
    // Silent no-op when the back stack is empty.
    void previous();

    // Mirror of previous(): restores the most recently pushed forward-stack entry, pushing the
    // current state onto the back stack. Silent no-op when the forward stack is empty.
    void next();

    // True iff previous()/next() would do something right now -- backs the Camera menu's
    // Previous/Next QActions' enabled state.
    bool canGoBack() const;
    bool canGoForward() const;

private:
    // Pushes preChangeState onto backStack_ (capped) and clears forwardStack_. Never called by
    // previous()/next().
    void pushHistoryEntry(const CameraState& preChangeState);

    CameraState state_;

    // Session-only -- NOT persisted to .plr. back is the most-recent-last stack previous() pops
    // from; forward is next()'s own mirror.
    std::vector<CameraState> backStack_;
    std::vector<CameraState> forwardStack_;
};

// Orchestrates events::CameraNavigated -> CameraStore::set(...), then sends events::CameraMoved
// when that call actually changed the state. Also downgrades TwoPoint back to Perspective on an
// orbit gesture (incoming azimuth/elevation differ from pre-sync).
class CameraSyncCommand : public ordo::core::Command<events::CameraNavigated> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::CameraNavigated& event) override;
};

// Orchestrates events::SetProjectionRequested -> CameraStore::setProjection(...).
// NOT wrapped in UndoCaptureCommand -- camera-only, never undo-worthy.
class SetProjectionCommand : public ordo::core::Command<events::SetProjectionRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetProjectionRequested& event) override;
};

// Orchestrates events::SetStandardViewRequested -> CameraStore::setStandardView(...).
// NOT wrapped in UndoCaptureCommand, same rationale as SetProjectionCommand.
class SetStandardViewCommand : public ordo::core::Command<events::SetStandardViewRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::SetStandardViewRequested& event) override;
};

// Orchestrates events::CameraPreviousRequested -> CameraStore::previous().
// NOT wrapped in UndoCaptureCommand -- camera view history is never an undo step.
class CameraPreviousCommand : public ordo::core::Command<events::CameraPreviousRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::CameraPreviousRequested& event) override;
};

// Mirror of CameraPreviousCommand: orchestrates events::CameraNextRequested -> CameraStore::next().
// Same "thin, not undo-wrapped" shape.
class CameraNextCommand : public ordo::core::Command<events::CameraNextRequested> {
public:
    void execute(ordo::core::AppKernel& kernel, const events::CameraNextRequested& event) override;
};

}  // namespace plnr::agent
