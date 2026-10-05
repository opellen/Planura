#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <geo/model.h>
#include <geo/vec3.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kSectionStoreName = "sections";

// One section plane: a named plane (point + unit normal) that can be the model's single ACTIVE
// cut (see setActive) and can be independently hidden (its own render only -- a hidden plane's
// cut stays in effect).
struct SectionPlane {
    geo::Id id{};
    std::string name;
    geo::Vec3 point;
    geo::Vec3 normal;
    bool active{};
    bool hidden{};
};

// Owns the model's section-plane set (root context only this MVP). Mutators send
// events::SectionsChanged only on actual change. At most one plane is ever active:
// setActive(id, true) deactivates whatever else was active too, in one SectionsChanged.
class SectionStore : public ordo::core::Agent {
public:
    SectionStore();

    // Adds a new plane through point along normal (normalized here). name empty auto-names
    // ("Section Plane N"). Starts INACTIVE and not hidden. Returns the new plane's id.
    geo::Id addPlane(geo::Vec3 point, geo::Vec3 normal, std::string name);

    // Removes plane id. Unknown id is a no-op. Removing the currently active plane simply clears
    // the cut (no other plane is auto-promoted to active).
    bool removePlane(geo::Id id);

    // Sets plane id's active flag. true: deactivates any other active plane first, then activates
    // id, coalesced into ONE SectionsChanged. false: just clears id's own flag. No-op if unchanged.
    bool setActive(geo::Id id, bool active);

    // Negates plane id's normal, flipping which side of the cut is removed. Unknown id is a no-op.
    bool reverse(geo::Id id);

    // Sets plane id's hidden flag (its own render only; a HIDDEN active plane keeps cutting).
    // Unknown id, or a value equal to the current flag, is a no-op.
    bool setHidden(geo::Id id, bool hidden);

    // The currently active plane, or nullptr if none is active (or the
    // agent has no planes at all).
    const SectionPlane* activePlane() const;

    const std::vector<SectionPlane>& planes() const;

    // -- Restore API -------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Empties planes_ and resets nextId_ back to its ctor value.
    void clearForRestore();

    // Inserts plane verbatim (including normal and active; the single-active-cut invariant is
    // NOT re-checked here). Returns false if plane.id is 0 or already used.
    bool restorePlane(SectionPlane plane);

private:
    SectionPlane* find(geo::Id id);
    const SectionPlane* find(geo::Id id) const;

    std::vector<SectionPlane> planes_;
    geo::Id nextId_ = 1;
};

}  // namespace plnr::agent
