#pragma once

#include <string_view>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kStyleStoreName = "style";

// Default front/back face color, copied verbatim from viewport::kFaceFrontColor/kFaceBackColor.
// This agent is the authority now; no setter exists yet (no color-picker UI).
inline constexpr double kDefaultFrontColorR = 1.0;
inline constexpr double kDefaultFrontColorG = 1.0;
inline constexpr double kDefaultFrontColorB = 0.98;
inline constexpr double kDefaultBackColorR = 0.651;
inline constexpr double kDefaultBackColorG = 0.694;
inline constexpr double kDefaultBackColorB = 0.729;

// SSAO multiply-blend strength, 0-1 (`color = mix(1.0, ao, aoStrength)`). Not verified against
// a the reference modeler AO intensity value.
inline constexpr double kDefaultAoStrength = 0.7;

// the reference modeler's default style ships with Profiles ON; DepthCue/BackEdges stay default-off.
// The .plr isAllDefault() omit-gate follows this constant.
inline constexpr bool kDefaultProfiles = true;

// One RGB triple, plain doubles in 0-1 range.
struct StyleColor {
    double r{};
    double g{};
    double b{};

    friend bool operator==(const StyleColor&, const StyleColor&) = default;
};

// Owns the document's rendering style: active FaceStyle mode, three edge-appearance flags,
// default front/back face color. Setters send events::StyleChanged only on actual change.
// View-setting, not document content: dirties the document but is NEVER undo-captured.
class StyleStore : public ordo::core::Agent {
public:
    StyleStore();

    // Sets the active face style. No-op if style already equals the current one.
    bool setFaceStyle(events::FaceStyle style);

    // Sets one edge flag (covers all three EdgeFlag values). No-op if value already matches.
    bool setEdgeFlag(events::EdgeFlag flag, bool value);

    // Sets the viewport SSAO toggle. No-op if value already matches.
    bool setAmbientOcclusion(bool value);

    // Sets the AO multiply-blend strength (0-1, unvalidated). No-op if unchanged.
    bool setAoStrength(double value);

    events::FaceStyle faceStyle() const;
    bool profiles() const;
    bool depthCue() const;
    bool backEdges() const;
    bool ambientOcclusion() const;
    double aoStrength() const;
    const StyleColor& defaultFrontColor() const;
    const StyleColor& defaultBackColor() const;

    // True iff every field is still at its ctor-seeded default -- gates io::writeDocument's
    // "omit the `style` object entirely" case.
    bool isAllDefault() const;

    // -- Restore API -------------------------------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Resets every field to its ctor-seeded default (New Document).
    void clearForRestore();

    // Overwrites faceStyle_ verbatim (the reader's own already-validated value).
    void restoreFaceStyle(events::FaceStyle style);

    // Overwrites all three edge flags verbatim.
    void restoreEdgeFlags(bool profiles, bool depthCue, bool backEdges);

    // Overwrites the AO toggle + strength verbatim.
    void restoreAmbientOcclusion(bool ambientOcclusion, double aoStrength);

    // Overwrites both default colors verbatim. No live setter exists yet
    // (see class comment), but a file can still carry non-default values,
    // so this keeps that path round-trip-safe.
    void restoreColors(StyleColor front, StyleColor back);

private:
    events::FaceStyle faceStyle_{events::FaceStyle::ShadedWithTextures};
    bool profiles_{kDefaultProfiles};
    bool depthCue_{false};
    bool backEdges_{false};
    bool ambientOcclusion_{false};
    double aoStrength_{kDefaultAoStrength};
    StyleColor defaultFrontColor_{kDefaultFrontColorR, kDefaultFrontColorG, kDefaultFrontColorB};
    StyleColor defaultBackColor_{kDefaultBackColorR, kDefaultBackColorG, kDefaultBackColorB};
};

}  // namespace plnr::agent
