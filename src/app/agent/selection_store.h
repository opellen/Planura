#pragma once

#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kSelectionStoreName = "selection";

// Owns the current entity selection (vertices/edges/faces, by EntityRef).
// Keeps an ordered vector (selection order matters for UI) alongside an
// unordered_set for O(1) membership checks -- the two are kept in sync by
// every mutator. Sends events::SelectionChanged only when a call actually
// changed the set, never on a no-op, per the Ordo role policy.
class SelectionStore : public ordo::core::Agent {
public:
    SelectionStore();

    // Replaces the selection with exactly refs: duplicates removed (first
    // occurrence wins), order preserved. No event if the resulting ordered
    // list is identical to the current one.
    bool replace(std::vector<events::EntityRef> refs);

    // Appends refs not already selected, preserving refs' relative order.
    // Sends SelectionChanged only if at least one ref was newly added.
    bool add(const std::vector<events::EntityRef>& refs);

    // Per ref: removes it if currently selected, else appends it. Sends
    // SelectionChanged only if at least one ref actually changed membership.
    bool toggle(const std::vector<events::EntityRef>& refs);

    // Removes refs that are currently selected; refs not selected are
    // ignored. Sends SelectionChanged only if at least one ref was removed.
    bool subtract(const std::vector<events::EntityRef>& refs);

    // Empties the selection. No event when already empty.
    bool clear();

    // Keeps only the refs for which alive(ref) is true, preserving order.
    // Sends SelectionChanged only if the surviving set differs from the
    // current one.
    bool prune(const std::function<bool(const events::EntityRef&)>& alive);

    const std::vector<events::EntityRef>& items() const;
    bool contains(events::EntityRef ref) const;
    bool empty() const;

private:
    std::vector<events::EntityRef> items_;
    std::unordered_set<events::EntityRef> lookup_;
};

}  // namespace plnr::agent
