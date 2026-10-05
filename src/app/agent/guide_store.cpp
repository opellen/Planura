#include "agent/guide_store.h"

#include <algorithm>
#include <string>

namespace plnr::agent {

GuideStore::GuideStore() : Agent(std::string(kGuideStoreName)) {}

Guide* GuideStore::find(geo::Id id) {
    for (Guide& g : guides_) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

const Guide* GuideStore::find(geo::Id id) const {
    for (const Guide& g : guides_) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

geo::Id GuideStore::addGuideLine(geo::Vec3 point, geo::Vec3 dir) {
    const geo::Id id = nextId_++;
    guides_.push_back(Guide{id, /*isLine=*/true, point, geo::normalized(dir), /*hidden=*/false});
    send(events::GuidesChanged{});
    return id;
}

geo::Id GuideStore::addGuidePoint(geo::Vec3 pos) {
    const geo::Id id = nextId_++;
    guides_.push_back(Guide{id, /*isLine=*/false, pos, geo::Vec3{}, /*hidden=*/false});
    send(events::GuidesChanged{});
    return id;
}

bool GuideStore::erase(geo::Id id) {
    const auto it = std::find_if(guides_.begin(), guides_.end(), [id](const Guide& g) { return g.id == id; });
    if (it == guides_.end()) return false;  // unknown id -- no-op
    guides_.erase(it);
    send(events::GuidesChanged{});
    return true;
}

bool GuideStore::setHidden(geo::Id id, bool hidden) {
    Guide* g = find(id);
    if (g == nullptr || g->hidden == hidden) {
        return false;  // unknown id, or already at this value -- no-op
    }
    g->hidden = hidden;
    send(events::GuidesChanged{});
    return true;
}

bool GuideStore::hidden(geo::Id id) const {
    const Guide* g = find(id);
    return g != nullptr && g->hidden;
}

bool GuideStore::deleteAll() {
    if (guides_.empty()) return false;  // no-op
    guides_.clear();
    send(events::GuidesChanged{});
    return true;
}

const std::vector<Guide>& GuideStore::guides() const {
    return guides_;
}

std::vector<geo::GuideLineData> GuideStore::lineView() const {
    std::vector<geo::GuideLineData> out;
    for (const Guide& g : guides_) {
        if (g.isLine && !g.hidden) {
            out.push_back(geo::GuideLineData{g.point, g.dir, g.id});
        }
    }
    return out;
}

std::vector<geo::GuidePointData> GuideStore::pointView() const {
    std::vector<geo::GuidePointData> out;
    for (const Guide& g : guides_) {
        if (!g.isLine && !g.hidden) {
            out.push_back(geo::GuidePointData{g.point, g.id});
        }
    }
    return out;
}

void GuideStore::clearForRestore() {
    guides_.clear();
    nextId_ = 1;
}

bool GuideStore::restoreGuide(Guide guide) {
    if (guide.id == 0 || find(guide.id) != nullptr) {
        return false;  // invalid id, or already used by a guide already restored
    }
    if (guide.id >= nextId_) {
        nextId_ = guide.id + 1;
    }
    guides_.push_back(std::move(guide));
    return true;
}

}  // namespace plnr::agent
