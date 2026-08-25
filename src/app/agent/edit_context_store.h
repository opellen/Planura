#pragma once

#include <string_view>
#include <vector>

#include <geo/entity.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kEditContextStoreName = "editContext";

// Owns the current group/component editing-context path: an instance-id
// chain from the root, one entry per nested "enter" -- empty means root
// (default). Mirrors SelectionStore's Agent discipline (event only on
// actual change). Push-target validity is NOT this Agent's job -- that's
// EnterContextCommand's, via GeometryApi.
class EditContextStore : public ordo::core::Agent {
public:
    EditContextStore();

    // Appends instanceId to the path, entering one level deeper. Every call
    // is a genuine change (the path always grows by one entry regardless of
    // the value), so this always fires EditContextChanged and returns true.
    bool push(geo::Id instanceId);

    // Pops the last entry off the path, exiting one level. false (no-op, no
    // event) if already at the root context.
    bool pop();

    // Empties the path back to root. false (no-op, no event) if already at
    // root.
    bool reset();

    const std::vector<geo::Id>& path() const;
    bool atRoot() const;

private:
    std::vector<geo::Id> path_;
};

}  // namespace plnr::agent
