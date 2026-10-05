#pragma once

#include <string_view>

#include <geo/vec3.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kAxesStoreName = "axes";

// The model's drawing-axes frame: an origin plus 3 always-unit-length, always-orthonormal,
// right-handed directions (xDir cross yDir == zDir). xDir/yDir/zDir are ALWAYS "red/green/blue"
// respectively, regardless of which way they point in world space once relocated.
struct Frame {
    geo::Vec3 origin;
    geo::Vec3 xDir{1.0, 0.0, 0.0};
    geo::Vec3 yDir{0.0, 1.0, 0.0};
    geo::Vec3 zDir{0.0, 0.0, 1.0};
};

// Exact (non-tolerant) equality -- geo::Vec3 has no operator== of its own. Used only by
// set()/reset()'s no-op check, where both sides come from the same deterministic formula.
inline bool operator==(const Frame& a, const Frame& b) {
    auto veq = [](const geo::Vec3& x, const geo::Vec3& y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return veq(a.origin, b.origin) && veq(a.xDir, b.xDir) && veq(a.yDir, b.yDir) && veq(a.zDir, b.zDir);
}

// Owns the model's current drawing-axes frame; event only on actual change. set()
// re-orthonormalizes: x'=normalize(primaryDir), z'=normalize(cross(x',secondaryHint)),
// y'=cross(z',x'); degenerate input is a no-op.
class AxesStore : public ordo::core::Agent {
public:
    AxesStore();

    // Sets the frame per the construction rule above. No-op when the derived result is degenerate
    // or exactly equal to the current frame (comparing the derived result, not the raw proposal).
    bool set(geo::Vec3 origin, geo::Vec3 primaryDir, geo::Vec3 secondaryHint);

    // Resets to the world default frame (origin zero, xDir/yDir/zDir = world X/Y/Z). No-op if
    // already at the default.
    bool reset();

    const Frame& frame() const;

    // -- Restore API -------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Overwrites frame_ verbatim -- unlike set(), does NOT re-derive an orthonormal result
    // (re-deriving would reintroduce float noise, breaking round-trip).
    void restoreFrame(const Frame& frame);

private:
    Frame frame_;
};

}  // namespace plnr::agent
