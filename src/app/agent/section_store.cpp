#include "agent/section_store.h"

#include <algorithm>
#include <string>

namespace plnr::agent {

SectionStore::SectionStore() : Agent(std::string(kSectionStoreName)) {}

SectionPlane* SectionStore::find(geo::Id id) {
    for (SectionPlane& p : planes_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const SectionPlane* SectionStore::find(geo::Id id) const {
    for (const SectionPlane& p : planes_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

geo::Id SectionStore::addPlane(geo::Vec3 point, geo::Vec3 normal, std::string name) {
    const geo::Id id = nextId_++;
    if (name.empty()) {
        name = "Section Plane " + std::to_string(id);
    }
    planes_.push_back(SectionPlane{id, std::move(name), point, geo::normalized(normal), /*active=*/false, /*hidden=*/false});
    send(events::SectionsChanged{});
    return id;
}

bool SectionStore::removePlane(geo::Id id) {
    const auto it = std::find_if(planes_.begin(), planes_.end(), [id](const SectionPlane& p) { return p.id == id; });
    if (it == planes_.end()) return false;  // unknown id -- no-op
    planes_.erase(it);
    send(events::SectionsChanged{});
    return true;
}

bool SectionStore::setActive(geo::Id id, bool active) {
    SectionPlane* target = find(id);
    if (target == nullptr) return false;  // unknown id -- no-op

    bool changed = false;
    if (active) {
        // One-active-cut invariant: deactivate every OTHER active plane
        // first, coalesced with target's own activation into a single
        // SectionsChanged below.
        for (SectionPlane& p : planes_) {
            if (p.id != id && p.active) {
                p.active = false;
                changed = true;
            }
        }
        if (!target->active) {
            target->active = true;
            changed = true;
        }
    } else if (target->active) {
        target->active = false;
        changed = true;
    }

    if (!changed) return false;  // already exactly this state -- no-op
    send(events::SectionsChanged{});
    return true;
}

bool SectionStore::reverse(geo::Id id) {
    SectionPlane* target = find(id);
    if (target == nullptr) return false;  // unknown id -- no-op
    target->normal = -target->normal;
    send(events::SectionsChanged{});
    return true;
}

bool SectionStore::setHidden(geo::Id id, bool hidden) {
    SectionPlane* target = find(id);
    if (target == nullptr || target->hidden == hidden) {
        return false;  // unknown id, or already at this value -- no-op
    }
    target->hidden = hidden;
    send(events::SectionsChanged{});
    return true;
}

const SectionPlane* SectionStore::activePlane() const {
    for (const SectionPlane& p : planes_) {
        if (p.active) return &p;
    }
    return nullptr;
}

const std::vector<SectionPlane>& SectionStore::planes() const {
    return planes_;
}

void SectionStore::clearForRestore() {
    planes_.clear();
    nextId_ = 1;
}

bool SectionStore::restorePlane(SectionPlane plane) {
    if (plane.id == 0 || find(plane.id) != nullptr) {
        return false;  // invalid id, or already used by a plane already restored
    }
    if (plane.id >= nextId_) {
        nextId_ = plane.id + 1;
    }
    planes_.push_back(std::move(plane));
    return true;
}

}  // namespace plnr::agent
