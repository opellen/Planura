#include "agent/edit_context_store.h"

#include <utility>

#include "agent/events.h"

namespace plnr::agent {

EditContextStore::EditContextStore() : Agent(std::string(kEditContextStoreName)) {}

bool EditContextStore::push(geo::Id instanceId) {
    path_.push_back(instanceId);
    send(events::EditContextChanged{});
    return true;
}

bool EditContextStore::pop() {
    if (path_.empty()) {
        return false;  // already at root -- no-op, no event
    }
    path_.pop_back();
    send(events::EditContextChanged{});
    return true;
}

bool EditContextStore::reset() {
    if (path_.empty()) {
        return false;  // already at root -- no-op, no event
    }
    path_.clear();
    send(events::EditContextChanged{});
    return true;
}

const std::vector<geo::Id>& EditContextStore::path() const {
    return path_;
}

bool EditContextStore::atRoot() const {
    return path_.empty();
}

}  // namespace plnr::agent
