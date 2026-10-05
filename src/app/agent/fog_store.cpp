#include "agent/fog_store.h"

namespace plnr::agent {

FogStore::FogStore() : Agent(std::string(kFogStoreName)) {}

bool FogStore::setEnabled(bool value) {
    if (enabled_ == value) {
        return false;  // already this value -- no-op
    }
    enabled_ = value;
    send(events::FogChanged{});
    return true;
}

bool FogStore::setRange(double startDistance, double endDistance) {
    if (startDistance_ == startDistance && endDistance_ == endDistance) {
        return false;  // already this range -- no-op
    }
    startDistance_ = startDistance;
    endDistance_ = endDistance;
    send(events::FogChanged{});
    return true;
}

bool FogStore::setUseBackgroundColor(bool value) {
    if (useBackgroundColor_ == value) {
        return false;  // already this value -- no-op
    }
    useBackgroundColor_ = value;
    send(events::FogChanged{});
    return true;
}

bool FogStore::enabled() const {
    return enabled_;
}

double FogStore::startDistance() const {
    return startDistance_;
}

double FogStore::endDistance() const {
    return endDistance_;
}

bool FogStore::useBackgroundColor() const {
    return useBackgroundColor_;
}

double FogStore::colorR() const {
    return colorR_;
}

double FogStore::colorG() const {
    return colorG_;
}

double FogStore::colorB() const {
    return colorB_;
}

bool FogStore::isAllDefault() const {
    return !enabled_ && startDistance_ == kDefaultFogStartDistance && endDistance_ == kDefaultFogEndDistance &&
           useBackgroundColor_ && colorR_ == 0.5 && colorG_ == 0.5 && colorB_ == 0.5;
}

void FogStore::clearForRestore() {
    enabled_ = false;
    startDistance_ = kDefaultFogStartDistance;
    endDistance_ = kDefaultFogEndDistance;
    useBackgroundColor_ = true;
    colorR_ = 0.5;
    colorG_ = 0.5;
    colorB_ = 0.5;
}

void FogStore::restoreEnabled(bool value) {
    enabled_ = value;
}

void FogStore::restoreRange(double startDistance, double endDistance) {
    startDistance_ = startDistance;
    endDistance_ = endDistance;
}

void FogStore::restoreUseBackgroundColor(bool value) {
    useBackgroundColor_ = value;
}

void FogStore::restoreColor(double r, double g, double b) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
}

}  // namespace plnr::agent
