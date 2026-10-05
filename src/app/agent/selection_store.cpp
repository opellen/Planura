#include "agent/selection_store.h"

#include <algorithm>

namespace plnr::agent {

SelectionStore::SelectionStore() : Agent(std::string(kSelectionStoreName)) {}

bool SelectionStore::replace(std::vector<events::EntityRef> refs) {
    std::vector<events::EntityRef> deduped;
    std::unordered_set<events::EntityRef> seen;
    deduped.reserve(refs.size());
    for (const events::EntityRef& ref : refs) {
        if (seen.insert(ref).second) {
            deduped.push_back(ref);
        }
    }

    if (deduped == items_) {
        // Resulting ordered list is identical to the current selection --
        // silent no-op event-wise.
        return false;
    }

    items_ = std::move(deduped);
    lookup_ = std::move(seen);
    send(events::SelectionChanged{});
    return true;
}

bool SelectionStore::add(const std::vector<events::EntityRef>& refs) {
    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        if (lookup_.insert(ref).second) {
            items_.push_back(ref);
            changed = true;
        }
    }
    if (changed) {
        send(events::SelectionChanged{});
    }
    return changed;
}

bool SelectionStore::toggle(const std::vector<events::EntityRef>& refs) {
    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        auto it = lookup_.find(ref);
        if (it != lookup_.end()) {
            lookup_.erase(it);
            items_.erase(std::remove(items_.begin(), items_.end(), ref), items_.end());
        } else {
            lookup_.insert(ref);
            items_.push_back(ref);
        }
        changed = true;
    }
    if (changed) {
        send(events::SelectionChanged{});
    }
    return changed;
}

bool SelectionStore::subtract(const std::vector<events::EntityRef>& refs) {
    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        auto it = lookup_.find(ref);
        if (it != lookup_.end()) {
            lookup_.erase(it);
            items_.erase(std::remove(items_.begin(), items_.end(), ref), items_.end());
            changed = true;
        }
    }
    if (changed) {
        send(events::SelectionChanged{});
    }
    return changed;
}

bool SelectionStore::clear() {
    if (items_.empty()) {
        return false;
    }
    items_.clear();
    lookup_.clear();
    send(events::SelectionChanged{});
    return true;
}

bool SelectionStore::prune(const std::function<bool(const events::EntityRef&)>& alive) {
    std::vector<events::EntityRef> kept;
    kept.reserve(items_.size());
    for (const events::EntityRef& ref : items_) {
        if (alive(ref)) {
            kept.push_back(ref);
        }
    }

    if (kept == items_) {
        // Nothing was pruned -- silent no-op event-wise.
        return false;
    }

    items_ = std::move(kept);
    lookup_ = std::unordered_set<events::EntityRef>(items_.begin(), items_.end());
    send(events::SelectionChanged{});
    return true;
}

const std::vector<events::EntityRef>& SelectionStore::items() const {
    return items_;
}

bool SelectionStore::contains(events::EntityRef ref) const {
    return lookup_.find(ref) != lookup_.end();
}

bool SelectionStore::empty() const {
    return items_.empty();
}

}  // namespace plnr::agent
