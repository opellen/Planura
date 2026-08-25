#include "agent/axes_store.h"

namespace plnr::agent {

AxesStore::AxesStore() : Agent(std::string(kAxesStoreName)) {}

bool AxesStore::set(geo::Vec3 origin, geo::Vec3 primaryDir, geo::Vec3 secondaryHint) {
    const geo::Vec3 x = geo::normalized(primaryDir);
    if (x.x == 0.0 && x.y == 0.0 && x.z == 0.0) {
        return false;  // degenerate primary -- reject rather than agent garbage
    }
    const geo::Vec3 z = geo::normalized(geo::cross(x, secondaryHint));
    if (z.x == 0.0 && z.y == 0.0 && z.z == 0.0) {
        return false;  // secondaryHint parallel to primary -- z undefined
    }
    const geo::Vec3 y = geo::cross(z, x);  // already unit -- z, x are unit and mutually perpendicular

    const Frame proposed{origin, x, y, z};
    if (proposed == frame_) {
        return false;  // no-op: derived result identical to the current frame
    }
    frame_ = proposed;
    send(events::AxesChanged{});
    return true;
}

bool AxesStore::reset() {
    static const Frame kWorldDefault{};
    if (frame_ == kWorldDefault) {
        return false;  // no-op: already at the world default
    }
    frame_ = kWorldDefault;
    send(events::AxesChanged{});
    return true;
}

const Frame& AxesStore::frame() const {
    return frame_;
}

void AxesStore::restoreFrame(const Frame& frame) {
    frame_ = frame;
}

}  // namespace plnr::agent
