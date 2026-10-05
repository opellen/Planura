#pragma once

#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kSelectionStoreName = "selection";

// Current entity selection (EntityRef): an ordered vector (order matters for UI) plus an unordered_set for O(1)
// lookup, kept in sync by every mutator. SelectionChanged fires only on an actual change.
class SelectionStore : public ordo::core::Agent {
public:
    SelectionStore();

    // Replaces with exactly refs: duplicates removed (first wins), order kept. No event if the list is unchanged.
    bool replace(std::vector<events::EntityRef> refs);

    // Appends refs not already selected, in order. Event only if one was added.
    bool add(const std::vector<events::EntityRef>& refs);

    // Per ref: remove if selected, else append. Event only if membership changed.
    bool toggle(const std::vector<events::EntityRef>& refs);

    // Removes selected refs; others are ignored. Event only if one was removed.
    bool subtract(const std::vector<events::EntityRef>& refs);

    // Empties the selection. No event when already empty.
    bool clear();

    // Keeps refs where alive(ref), preserving order. Event only if the set changed.
    bool prune(const std::function<bool(const events::EntityRef&)>& alive);

    const std::vector<events::EntityRef>& items() const;
    bool contains(events::EntityRef ref) const;
    bool empty() const;

private:
    std::vector<events::EntityRef> items_;
    std::unordered_set<events::EntityRef> lookup_;
};

}  // namespace plnr::agent
