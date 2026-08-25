#pragma once

#include <string_view>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kFogStoreName = "fog";

// Fog range defaults: the mirror's fog sliders are relative to the current camera view, but this
// agent uses a fixed world-space distance instead. Picked to be visually demonstrable at this
// app's default camera distance (25.0) and ground-grid scale (+-10 units).
inline constexpr double kDefaultFogStartDistance = 5.0;
inline constexpr double kDefaultFogEndDistance = 40.0;

// Owns the document's fog settings. Setters send events::FogChanged only on actual change.
// startDistance/endDistance are a fixed world-space pair (not camera-relative); colorR/G/B are
// reserved, no setter yet.
class FogStore : public ordo::core::Agent {
public:
    FogStore();

    // Sets whether fog is displayed. No-op if value already matches enabled_.
    bool setEnabled(bool value);

    // Sets the start/end fog distances (world units from the eye). No range validation.
    bool setRange(double startDistance, double endDistance);

    // Sets whether the fog color follows the viewport's own background color vs. the reserved
    // custom color fields.
    bool setUseBackgroundColor(bool value);

    bool enabled() const;
    double startDistance() const;
    double endDistance() const;
    bool useBackgroundColor() const;
    double colorR() const;
    double colorG() const;
    double colorB() const;

    // True iff every field is still at its ctor-seeded default -- gates io::writeDocument's
    // "omit the `fog` object entirely" case.
    bool isAllDefault() const;

    // -- Restore API -- file loader/undo-adjacent paths: plain data manipulation, no notification.

    // Resets every field to its ctor-seeded default (New Document).
    void clearForRestore();

    // Overwrites enabled_ verbatim (the reader's own already-validated value).
    void restoreEnabled(bool value);

    // Overwrites startDistance_/endDistance_ verbatim.
    void restoreRange(double startDistance, double endDistance);

    // Overwrites useBackgroundColor_ verbatim.
    void restoreUseBackgroundColor(bool value);

    // Overwrites colorR_/colorG_/colorB_ verbatim. No live setter exists yet, but a file can
    // still carry non-default values, so this keeps that path round-trip-safe.
    void restoreColor(double r, double g, double b);

private:
    bool enabled_{false};
    double startDistance_{kDefaultFogStartDistance};
    double endDistance_{kDefaultFogEndDistance};
    bool useBackgroundColor_{true};
    double colorR_{0.5};
    double colorG_{0.5};
    double colorB_{0.5};
};

}  // namespace plnr::agent
