#pragma once

#include <string_view>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kShadowStoreName = "shadow";

// Default sun-position seeds. month/day/hourLocal: the reference modeler's Shadows-panel default (11/08,
// 1:30pm = 13.5h), verified against the mirror. latitudeDeg/longitudeDeg: this app's own pick
// (Seoul, 37.5665N/126.9780E), not a the reference modeler mirror fact.
inline constexpr double kDefaultLatitudeDeg = 37.57;
inline constexpr double kDefaultLongitudeDeg = 126.98;
inline constexpr int kDefaultMonth = 11;
inline constexpr int kDefaultDay = 8;
inline constexpr double kDefaultHourLocal = 13.5;

// Light/Dark slider defaults, verified against the same shadows-panel screenshot: the numeric
// fields next to the sliders read "80" and "45" exactly.
inline constexpr double kDefaultLight = 80.0;
inline constexpr double kDefaultDark = 45.0;

// Sun-position face shading is ON out of the box (matches the reference modeler); showShadows stays
// default-off. The .plr isAllDefault() omit-gate follows this constant.
inline constexpr bool kDefaultUseSunForShading = true;

// Owns the document's sun-shading and ground-shadow settings. Setters send events::ShadowsChanged
// only on actual change. VIEW-SETTING: dirties the document but never undo-captured.
// useSunForShading_ is N.L face-shading only (no casting); showShadows_ is independent casting.
// light_/dark_ (0-100, unclamped) scale N.L strength / shadow alpha.
class ShadowStore : public ordo::core::Agent {
public:
    ShadowStore();

    // Sets whether sun-position face shading is active. No-op if unchanged.
    bool setUseSunForShading(bool value);

    // Sets whether ground-plane shadow casting is active. No-op if unchanged.
    bool setShowShadows(bool value);

    // Sets the sun's geographic position (plain degrees, unvalidated). No-op if unchanged.
    bool setPosition(double latitudeDeg, double longitudeDeg);

    // Sets the sun's calendar date/time, taken verbatim (agent::sun::solarAngles clamps
    // defensively downstream). No-op if all three already match.
    bool setDateTime(int month, int day, double hourLocal);

    // Sets the Light slider (0-100, unvalidated). No-op if unchanged.
    bool setLight(double value);

    // Sets the Dark slider (0-100, unvalidated). No-op if unchanged.
    bool setDark(double value);

    bool useSunForShading() const;
    bool showShadows() const;
    double latitudeDeg() const;
    double longitudeDeg() const;
    int month() const;
    int day() const;
    double hourLocal() const;
    double light() const;
    double dark() const;

    // True iff every field is still at its ctor-seeded default -- gates the "omit `shadows`" case.
    bool isAllDefault() const;

    // -- Restore API -- file loader/undo-adjacent paths: plain data manipulation, no notification.

    // Resets every field to its ctor-seeded default (New Document).
    void clearForRestore();

    // Overwrites useSunForShading_ verbatim (the reader's own already-validated value).
    void restoreUseSunForShading(bool value);

    // Overwrites showShadows_ verbatim.
    void restoreShowShadows(bool value);

    // Overwrites latitudeDeg_/longitudeDeg_ verbatim.
    void restorePosition(double latitudeDeg, double longitudeDeg);

    // Overwrites month_/day_/hourLocal_ verbatim.
    void restoreDateTime(int month, int day, double hourLocal);

    // Overwrites light_ verbatim.
    void restoreLight(double value);

    // Overwrites dark_ verbatim.
    void restoreDark(double value);

private:
    bool useSunForShading_{kDefaultUseSunForShading};
    bool showShadows_{false};
    double latitudeDeg_{kDefaultLatitudeDeg};
    double longitudeDeg_{kDefaultLongitudeDeg};
    int month_{kDefaultMonth};
    int day_{kDefaultDay};
    double hourLocal_{kDefaultHourLocal};
    double light_{kDefaultLight};
    double dark_{kDefaultDark};
};

}  // namespace plnr::agent
