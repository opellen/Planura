#pragma once

#include <string_view>
#include <vector>

#include <geo/entity.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kEditContextStoreName = "editContext";

// Current group/component editing-context path: instance ids from the root, one per nested "enter"; empty = root.
// Push-target validity is EnterContextCommand's job (via GeometryApi), not this Agent's.
class EditContextStore : public ordo::core::Agent {
public:
    EditContextStore();

    // Appends instanceId (one level deeper). Always a change: fires EditContextChanged, returns true.
    bool push(geo::Id instanceId);

    // Exits one level. false (no-op, no event) at root.
    bool pop();

    // Empties the path to root. false (no-op, no event) if already at root.
    bool reset();

    const std::vector<geo::Id>& path() const;
    bool atRoot() const;

private:
    std::vector<geo::Id> path_;
};

}  // namespace plnr::agent
