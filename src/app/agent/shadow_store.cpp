#include "agent/shadow_store.h"

namespace plnr::agent {

ShadowStore::ShadowStore() : Agent(std::string(kShadowStoreName)) {}

bool ShadowStore::setUseSunForShading(bool value) {
    if (useSunForShading_ == value) {
        return false;  // already this value -- no-op
    }
    useSunForShading_ = value;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::setShowShadows(bool value) {
    if (showShadows_ == value) {
        return false;  // already this value -- no-op
    }
    showShadows_ = value;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::setPosition(double latitudeDeg, double longitudeDeg) {
    if (latitudeDeg_ == latitudeDeg && longitudeDeg_ == longitudeDeg) {
        return false;  // already this position -- no-op
    }
    latitudeDeg_ = latitudeDeg;
    longitudeDeg_ = longitudeDeg;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::setDateTime(int month, int day, double hourLocal) {
    if (month_ == month && day_ == day && hourLocal_ == hourLocal) {
        return false;  // already this date/time -- no-op
    }
    month_ = month;
    day_ = day;
    hourLocal_ = hourLocal;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::setLight(double value) {
    if (light_ == value) {
        return false;  // already this value -- no-op
    }
    light_ = value;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::setDark(double value) {
    if (dark_ == value) {
        return false;  // already this value -- no-op
    }
    dark_ = value;
    send(events::ShadowsChanged{});
    return true;
}

bool ShadowStore::useSunForShading() const {
    return useSunForShading_;
}

bool ShadowStore::showShadows() const {
    return showShadows_;
}

double ShadowStore::latitudeDeg() const {
    return latitudeDeg_;
}

double ShadowStore::longitudeDeg() const {
    return longitudeDeg_;
}

int ShadowStore::month() const {
    return month_;
}

int ShadowStore::day() const {
    return day_;
}

double ShadowStore::hourLocal() const {
    return hourLocal_;
}

double ShadowStore::light() const {
    return light_;
}

double ShadowStore::dark() const {
    return dark_;
}

bool ShadowStore::isAllDefault() const {
    return useSunForShading_ == kDefaultUseSunForShading && !showShadows_ && latitudeDeg_ == kDefaultLatitudeDeg &&
           longitudeDeg_ == kDefaultLongitudeDeg && month_ == kDefaultMonth && day_ == kDefaultDay &&
           hourLocal_ == kDefaultHourLocal && light_ == kDefaultLight && dark_ == kDefaultDark;
}

void ShadowStore::clearForRestore() {
    useSunForShading_ = kDefaultUseSunForShading;
    showShadows_ = false;
    latitudeDeg_ = kDefaultLatitudeDeg;
    longitudeDeg_ = kDefaultLongitudeDeg;
    month_ = kDefaultMonth;
    day_ = kDefaultDay;
    hourLocal_ = kDefaultHourLocal;
    light_ = kDefaultLight;
    dark_ = kDefaultDark;
}

void ShadowStore::restoreUseSunForShading(bool value) {
    useSunForShading_ = value;
}

void ShadowStore::restoreShowShadows(bool value) {
    showShadows_ = value;
}

void ShadowStore::restorePosition(double latitudeDeg, double longitudeDeg) {
    latitudeDeg_ = latitudeDeg;
    longitudeDeg_ = longitudeDeg;
}

void ShadowStore::restoreDateTime(int month, int day, double hourLocal) {
    month_ = month;
    day_ = day;
    hourLocal_ = hourLocal;
}

void ShadowStore::restoreLight(double value) {
    light_ = value;
}

void ShadowStore::restoreDark(double value) {
    dark_ = value;
}

}  // namespace plnr::agent
