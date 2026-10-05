#pragma once

// Fidelity palette, split out of design_tokens.h so Qt-free headers (tools/tool.h) can use
// the floats without pulling QtGui. Includes nothing. docs/design/design-grammar.md is the
// authority (section 2.9).

namespace plnr {
namespace design {

// Fidelity palette: the reference modeler-verified constants, exempt from theming. The float triple is
// authoritative; never derive it from a hex. Not QSS placeholders.
struct FidelityColor {
    float r, g, b, a;
};
using ColorF = FidelityColor;
inline constexpr FidelityColor kFidelityAxisRed{0.86f, 0.20f, 0.18f, 1.0f};
inline constexpr FidelityColor kFidelityAxisGreen{0.10f, 0.62f, 0.19f, 1.0f};
inline constexpr FidelityColor kFidelityAxisBlue{0.16f, 0.32f, 0.75f, 1.0f};
inline constexpr FidelityColor kFidelityCueEndpoint{0.10f, 0.62f, 0.19f, 1.0f};
inline constexpr FidelityColor kFidelityCueMidpoint{0.20f, 0.80f, 0.80f, 1.0f};
inline constexpr FidelityColor kFidelityCueOnFace{0.16f, 0.32f, 0.75f, 1.0f};
inline constexpr FidelityColor kFidelityCueFromPoint{0.05f, 0.10f, 0.35f, 1.0f};
inline constexpr FidelityColor kFidelityLockMarker{0.86f, 0.20f, 0.18f, 1.0f};
inline constexpr FidelityColor kFidelityCueOnEdge{0.86f, 0.20f, 0.18f, 1.0f};
inline constexpr FidelityColor kFidelityCueIntersection{0.86f, 0.20f, 0.18f, 1.0f};
inline constexpr FidelityColor kFidelityCueGuide{0.55f, 0.55f, 0.55f, 1.0f};

// Canvas colors read from Qt-free code (tools/*.cpp, viewport_widget.h member defaults), so
// they live here rather than in design_tokens.h. Same rules as above: the floats are authoritative.
inline constexpr ColorF kCanvasProportionTrace{0.13f, 0.45f, 0.90f, 1.0f};  // Square/Golden Section dashed trace
inline constexpr ColorF kCanvasScaleBbox{0.95f, 0.85f, 0.10f, 1.0f};        // scale tool bounding-box wireframe
inline constexpr ColorF kCanvasSectionCandidate{0.35f, 0.85f, 0.45f, 0.4f};  // section-plane candidate fill (unverified RGBA)
inline constexpr ColorF kCanvasStyleFrontDefault{1.0f, 1.0f, 0.98f, 1.0f};  // default front-face color, mirrors StyleStore
inline constexpr ColorF kCanvasStyleBackDefault{0.651f, 0.694f, 0.729f, 1.0f};  // default back-face color, mirrors StyleStore
inline constexpr ColorF kCanvasFogDefault{230.0f / 255.0f, 235.0f / 255.0f, 240.0f / 255.0f, 1.0f};  // default custom fog color, mirrors FogStore

}  // namespace design
}  // namespace plnr
