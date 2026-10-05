#pragma once

#include <string_view>
#include <vector>

#include <geo/vec3.h>
#include <ordo/core/kernel.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kCameraStoreName = "camera";

// Camera snapshot mirroring viewport Camera 1:1, defaults included (agent/ never depends on io/, so not io::CameraState).
struct CameraState {
    geo::Vec3 target;
    double azimuthDeg{-60.0};
    // Mirrors viewport::Camera's shallow default pitch -- keep in sync.
    double elevationDeg{7.0};
    double distance{25.0};
    double fovYDeg{35.0};
    events::Projection projection{events::Projection::Perspective};
};

// Qt-free. set() dispatches nothing (the sender already shows the change live);
// restoreCamera() is the one exception and always dispatches CameraChanged.
class CameraStore : public ordo::core::Agent {
public:
    CameraStore();

    void set(const CameraState& state);

    const CameraState& state() const;

    // -- Restore API: file loader / new-document default only.
    // Unlike other Agents, DOES dispatch CameraChanged, unconditionally.
    void restoreCamera(const CameraState& state);

    // Dispatches CameraChanged only on a real change, and pushes the pre-change state onto the back stack.
    void setProjection(events::Projection projection);

    // Sets azimuth/elevation only (target/distance carry over). No-op unless az/el or projection change;
    // TwoPoint resets to Perspective, other projections carry over.
    void setStandardView(events::StandardView view);

    // -- View history: session-only two-stack history, not persisted to .plr.
    // set()/setStandardView()/setProjection() push the pre-change state on a real change; previous()/next() never push.

    // Pops the back stack into state_, pushing the current state onto the forward stack; dispatches CameraChanged.
    // No-op when the back stack is empty.
    void previous();

    // Mirror of previous() using the forward stack. No-op when empty.
    void next();

    // True iff previous()/next() would do something (drives the Camera menu's enabled state).
    bool canGoBack() const;
    bool canGoForward() const;

private:
    // Pushes onto backStack_ (capped) and clears forwardStack_. Never called by previous()/next().
    void pushHistoryEntry(const CameraState& preChangeState);

    CameraState state_;

    // Session-only. back: most-recent-last stack previous() pops from; forward mirrors it for next().
    std::vector<CameraState> backStack_;
    std::vector<CameraState> forwardStack_;
};

// CameraNavigated -> CameraStore::set(), then CameraMoved if the state changed.
// An orbit (azimuth/elevation changed) downgrades TwoPoint to Perspective.
class CameraSyncCommand : public ordo::core::Command<events::CameraNavigated> {
public:
    void execute(const events::CameraNavigated& event, ordo::core::CommandContext& context) override;
};

// SetProjectionRequested -> CameraStore::setProjection(). Not undo-wrapped (camera-only).
class SetProjectionCommand : public ordo::core::Command<events::SetProjectionRequested> {
public:
    void execute(const events::SetProjectionRequested& event, ordo::core::CommandContext& context) override;
};

// SetStandardViewRequested -> CameraStore::setStandardView(). Not undo-wrapped.
class SetStandardViewCommand : public ordo::core::Command<events::SetStandardViewRequested> {
public:
    void execute(const events::SetStandardViewRequested& event, ordo::core::CommandContext& context) override;
};

// CameraPreviousRequested -> CameraStore::previous(). Not undo-wrapped.
class CameraPreviousCommand : public ordo::core::Command<events::CameraPreviousRequested> {
public:
    void execute(const events::CameraPreviousRequested& event, ordo::core::CommandContext& context) override;
};

// CameraNextRequested -> CameraStore::next(). Not undo-wrapped.
class CameraNextCommand : public ordo::core::Command<events::CameraNextRequested> {
public:
    void execute(const events::CameraNextRequested& event, ordo::core::CommandContext& context) override;
};

}  // namespace plnr::agent
