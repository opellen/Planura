#include "agent/style_store.h"

namespace plnr::agent {

StyleStore::StyleStore() : Agent(std::string(kStyleStoreName)) {}

bool StyleStore::setFaceStyle(events::FaceStyle style) {
    if (faceStyle_ == style) {
        return false;  // already this style -- no-op
    }
    faceStyle_ = style;
    send(events::StyleChanged{});
    return true;
}

bool StyleStore::setEdgeFlag(events::EdgeFlag flag, bool value) {
    bool* target = nullptr;
    switch (flag) {
        case events::EdgeFlag::Profiles:
            target = &profiles_;
            break;
        case events::EdgeFlag::DepthCue:
            target = &depthCue_;
            break;
        case events::EdgeFlag::BackEdges:
            target = &backEdges_;
            break;
    }
    if (target == nullptr || *target == value) {
        return false;  // unreachable flag value, or already at this value -- no-op
    }
    *target = value;
    send(events::StyleChanged{});
    return true;
}

bool StyleStore::setAmbientOcclusion(bool value) {
    if (ambientOcclusion_ == value) {
        return false;  // already at this value -- no-op
    }
    ambientOcclusion_ = value;
    send(events::StyleChanged{});
    return true;
}

bool StyleStore::setAoStrength(double value) {
    if (aoStrength_ == value) {
        return false;  // already at this value -- no-op
    }
    aoStrength_ = value;
    send(events::StyleChanged{});
    return true;
}

events::FaceStyle StyleStore::faceStyle() const {
    return faceStyle_;
}

bool StyleStore::profiles() const {
    return profiles_;
}

bool StyleStore::depthCue() const {
    return depthCue_;
}

bool StyleStore::backEdges() const {
    return backEdges_;
}

bool StyleStore::ambientOcclusion() const {
    return ambientOcclusion_;
}

double StyleStore::aoStrength() const {
    return aoStrength_;
}

const StyleColor& StyleStore::defaultFrontColor() const {
    return defaultFrontColor_;
}

const StyleColor& StyleStore::defaultBackColor() const {
    return defaultBackColor_;
}

bool StyleStore::isAllDefault() const {
    static const StyleColor kFrontDefault{kDefaultFrontColorR, kDefaultFrontColorG, kDefaultFrontColorB};
    static const StyleColor kBackDefault{kDefaultBackColorR, kDefaultBackColorG, kDefaultBackColorB};
    return faceStyle_ == events::FaceStyle::ShadedWithTextures && profiles_ == kDefaultProfiles && !depthCue_ && !backEdges_ &&
           !ambientOcclusion_ && aoStrength_ == kDefaultAoStrength && defaultFrontColor_ == kFrontDefault &&
           defaultBackColor_ == kBackDefault;
}

void StyleStore::clearForRestore() {
    faceStyle_ = events::FaceStyle::ShadedWithTextures;
    profiles_ = kDefaultProfiles;
    depthCue_ = false;
    backEdges_ = false;
    ambientOcclusion_ = false;
    aoStrength_ = kDefaultAoStrength;
    defaultFrontColor_ = StyleColor{kDefaultFrontColorR, kDefaultFrontColorG, kDefaultFrontColorB};
    defaultBackColor_ = StyleColor{kDefaultBackColorR, kDefaultBackColorG, kDefaultBackColorB};
}

void StyleStore::restoreFaceStyle(events::FaceStyle style) {
    faceStyle_ = style;
}

void StyleStore::restoreEdgeFlags(bool profiles, bool depthCue, bool backEdges) {
    profiles_ = profiles;
    depthCue_ = depthCue;
    backEdges_ = backEdges;
}

void StyleStore::restoreAmbientOcclusion(bool ambientOcclusion, double aoStrength) {
    ambientOcclusion_ = ambientOcclusion;
    aoStrength_ = aoStrength;
}

void StyleStore::restoreColors(StyleColor front, StyleColor back) {
    defaultFrontColor_ = front;
    defaultBackColor_ = back;
}

}  // namespace plnr::agent
