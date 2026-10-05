#pragma once

// Design tokens: named colors, spacing/geometry and type-ramp / button QSS fragments.
// docs/design/design-grammar.md (role table, 2.3) is the authority: change the doc first.
// A value shared by 2+ screens belongs here; one screen's own constant stays in that screen.

#include <QColor>
#include <QString>
#include <QtGlobal>

#include "constants/design_fidelity.h"

namespace plnr {
namespace design {

// Surfaces (light theme: lightness marks nearness to the user's work).
inline constexpr const char* kSurface0 = "#FAFAFC";        // window base, splitter gaps, empty editor page
inline constexpr const char* kSurface1 = "#F2F4FD";        // chrome panels: menu/tool bars, rail, side bars, status bar
inline constexpr const char* kSurface2 = "#FFFFFF";        // fields, cards, popups
inline constexpr const char* kSurfaceHover = "#E5E9FC";    // hover state layer, base of selected/checked
inline constexpr const char* kSurfacePressed = "#D0D8FA";  // pressed state layer

// Outline.
inline constexpr const char* kOutline = "#C4CCEF";        // panel/field borders, default
inline constexpr const char* kOutlineStrong = "#B1BEEC";  // section hairlines, dividers, floating-surface border
inline constexpr const char* kOutlineFocus = "#3039C9";   // focus ring -- the only one
inline constexpr const char* kOutlineHover = "#9EAFE9";   // border of a hovered control, scroll-thumb hover

// Text (3-step hierarchy: one visible step between values and labels).
inline constexpr const char* kTextPrimary = "#1C1F33";    // values, body, section titles
inline constexpr const char* kTextSecondary = "#4A4F6E";  // row labels, chevrons, unit suffixes
inline constexpr const char* kTextDisabled = "#8A8FA8";   // disabled, placeholder, empty-list copy
inline constexpr const char* kTextOnAccent = "#FFFFFF";   // text/glyphs on an accent fill -- not a hierarchy step

// Accents.
inline constexpr const char* kAccentBrand = "#2631A8";        // primary button fill
inline constexpr const char* kAccentInteractive = "#3039C9";  // selection bar, focus, active/checked
inline constexpr const char* kAccentPulse = "#4050F0";        // transient emphasis ONLY, never permanent UI
inline constexpr const char* kDanger = "#B4413C";             // destructive, error

// Status dots (dots only, never text or backgrounds).
inline constexpr const char* kStatusLive = "#2A7448";
inline constexpr const char* kStatusIdle = "#8A8FA8";

// Feedback. Text/borders take success/danger/warning, backgrounds the -surface role.
inline constexpr const char* kSuccess = "#2A7448";
inline constexpr const char* kSuccessSurface = "#E5EEE9";
inline constexpr const char* kDangerSurface = "#F6E8E8";
inline constexpr const char* kWarning = "#A0650F";

// Alpha roles below use Qt's #AARRGGBB form (alpha FIRST). Read them through color() for
// QPainter and qss() for style sheets (qss() turns them into rgba()).

// Overlays and state layers.
inline constexpr const char* kSurfaceOverlay = "#EBF2F4FD";        // floating panel over the viewport
inline constexpr const char* kScrim = "#521C1F33";                 // modal progress backdrop
inline constexpr const char* kAccentInteractiveSoft = "#B43039C9";  // drop-zone border, soft emphasis outline
inline constexpr const char* kDropZoneFill = "#293039C9";          // editor-area drop target fill
inline constexpr const char* kStateLayerHover = "#144050F0";       // hover overlay on a base other than surface-1
inline constexpr const char* kStateLayerPressed = "#2E4050F0";     // pressed overlay on a base other than surface-1

// Canvas (viewport environment). Hex here; call sites convert to floats for GL.
inline constexpr const char* kCanvasBackground = "#D3D6E3";   // clear color = ground = fog color when fog follows background
inline constexpr const char* kCanvasSkyHorizon = "#E5E9FC";
inline constexpr const char* kCanvasSkyZenith = "#9EAFE9";

// Canvas overlay colors (viewport_widget.cpp). Float roles feed GL uniforms and vertex colors
// as authored; the float triple is authoritative, never derived from a hex. Hex roles are
// QPainter-only and stay out of kRolePlaceholders (no QSS consumer).
inline constexpr ColorF kCanvasGrid{200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f};  // ground grid lines
inline constexpr ColorF kCanvasAxisLineX{1.0f, 0.0f, 0.0f, 1.0f};         // origin axis, positive X half
inline constexpr ColorF kCanvasAxisLineY{0.0f, 1.0f, 0.0f, 1.0f};         // origin axis, positive Y half
inline constexpr ColorF kCanvasAxisLineZ{0.0f, 0.0f, 1.0f, 1.0f};         // origin axis, positive Z half
inline constexpr ColorF kCanvasAxisLineXNeg{1.0f, 0.65f, 0.65f, 1.0f};    // dashed negative X half
inline constexpr ColorF kCanvasAxisLineYNeg{0.65f, 1.0f, 0.65f, 1.0f};    // dashed negative Y half
inline constexpr ColorF kCanvasAxisLineZNeg{0.65f, 0.65f, 1.0f, 1.0f};    // dashed negative Z half
inline constexpr ColorF kCanvasModelEdge{0.0f, 0.0f, 0.0f, 1.0f};         // model edges: pure black
inline constexpr ColorF kCanvasBackEdge{0.55f, 0.55f, 0.55f, 1.0f};       // back-edges pass; same value as fidelity-cue-guide, separate role
inline constexpr ColorF kCanvasHiddenLineFill{1.0f, 1.0f, 1.0f, 1.0f};    // HiddenLine flat face fill
inline constexpr ColorF kCanvasPreviewMarker{0.2f, 0.75f, 0.2f, 1.0f};    // preview snap marker
inline constexpr ColorF kCanvasDimmedFace{0.85f, 0.85f, 0.85f, 1.0f};     // faces outside the editing context
inline constexpr ColorF kCanvasDimmedEdge{0.65f, 0.65f, 0.65f, 1.0f};     // edges outside the editing context
inline constexpr ColorF kCanvasGuide{0.35f, 0.35f, 0.35f, 1.0f};          // guides (unverified against the reference modeler)
inline constexpr ColorF kCanvasAnnotation{0.15f, 0.15f, 0.15f, 1.0f};     // associated dimensions
inline constexpr ColorF kCanvasAnnotationInvalid{0.86f, 0.20f, 0.18f, 1.0f};  // non-associated dimensions; same floats as fidelity-axis-red, separate role
inline constexpr ColorF kCanvasSection{0.35f, 0.85f, 0.45f, 1.0f};        // section-plane widget (alpha set per state)
inline constexpr ColorF kCanvasSelection{0.13f, 0.45f, 0.90f, 1.0f};      // selected edges/vertices, face-fill stipple, Push/Pull hover
inline constexpr ColorF kCanvasScreenRect{0.2f, 0.2f, 0.2f, 1.0f};        // drag-selection rubber band
inline constexpr ColorF kCanvasGroundShadowInk{0.0f, 0.0f, 0.0f, 1.0f};   // ground-shadow ink; alpha composed at runtime from shadowDark_

inline constexpr const char* kCanvasAnnotationLabelBg = "#EBFFFFFF";      // leader-text label box
inline constexpr const char* kCanvasAnnotationLabelBorder = "#8C8C8C";
inline constexpr const char* kCanvasAnnotationLabelText = "#000000";
inline constexpr const char* kCanvasScreentipBg = "#FFFFE1";              // inference ScreenTip box
inline constexpr const char* kCanvasScreentipBorder = "#786E3C";
inline constexpr const char* kCanvasScreentipText = "#000000";            // separate role from label-text on purpose
inline constexpr const char* kCanvasScreentipWarning = "#C81E1E";         // warning tip: plain red text, no box

// The fidelity palette (kFidelity*) lives in the Qt-free design_fidelity.h.

// Accessors. color() parses both #RRGGBB and #AARRGGBB; qss() hands an opaque token back
// unchanged and spells an alpha token as rgba(), so a style sheet never has to know which
// kind it got.
inline QColor color(const char* token) { return QColor(QString::fromLatin1(token)); }
inline QString qss(const char* token) {
    const QColor c = color(token);
    if (c.alpha() == 255) {
        return QString::fromLatin1(token);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// Style-sheet templates name colors by role ("{surface-2}", "{outline-focus}", ...).
// resolveRoles() fills every placeholder from this table (alpha roles arrive as rgba()).
struct RolePlaceholder {
    const char* placeholder;
    const char* token;
};
inline constexpr RolePlaceholder kRolePlaceholders[] = {
    {"{surface-0}", kSurface0}, {"{surface-1}", kSurface1}, {"{surface-2}", kSurface2},
    {"{surface-hover}", kSurfaceHover}, {"{surface-pressed}", kSurfacePressed},
    {"{outline}", kOutline}, {"{outline-strong}", kOutlineStrong}, {"{outline-focus}", kOutlineFocus},
    {"{outline-hover}", kOutlineHover},
    {"{text-primary}", kTextPrimary}, {"{text-secondary}", kTextSecondary}, {"{text-disabled}", kTextDisabled},
    {"{text-on-accent}", kTextOnAccent},
    {"{accent-brand}", kAccentBrand}, {"{accent-interactive}", kAccentInteractive}, {"{accent-pulse}", kAccentPulse},
    {"{danger}", kDanger},
    {"{status-live}", kStatusLive}, {"{status-idle}", kStatusIdle},
    {"{success}", kSuccess}, {"{success-surface}", kSuccessSurface}, {"{danger-surface}", kDangerSurface},
    {"{warning}", kWarning},
    {"{surface-overlay}", kSurfaceOverlay}, {"{scrim}", kScrim},
    {"{accent-interactive-soft}", kAccentInteractiveSoft}, {"{drop-zone-fill}", kDropZoneFill},
    {"{state-layer-hover}", kStateLayerHover}, {"{state-layer-pressed}", kStateLayerPressed},
    {"{canvas-background}", kCanvasBackground}, {"{canvas-sky-horizon}", kCanvasSkyHorizon},
    {"{canvas-sky-zenith}", kCanvasSkyZenith},
};

inline QString resolveRoles(QString qssTemplate) {
    for (const RolePlaceholder& role : kRolePlaceholders) {
        qssTemplate.replace(QLatin1String(role.placeholder), qss(role.token));
    }
    // A "{name}" left behind is a missing role or a typo. Real QSS rule bodies contain a
    // space or ':', so they are never mistaken for a placeholder.
    for (qsizetype open = qssTemplate.indexOf(QLatin1Char('{')); open >= 0;
         open = qssTemplate.indexOf(QLatin1Char('{'), open + 1)) {
        qsizetype i = open + 1;
        while (i < qssTemplate.size() && (qssTemplate[i].isLower() || qssTemplate[i].isDigit() ||
                                          qssTemplate[i] == QLatin1Char('-'))) {
            ++i;
        }
        if (i > open + 1 && i < qssTemplate.size() && qssTemplate[i] == QLatin1Char('}')) {
            qWarning("design::resolveRoles: unresolved role placeholder %s",
                     qPrintable(qssTemplate.mid(open, i - open + 1)));
        }
    }
    return qssTemplate;
}

// Spacing (4px grid) and the row constants shared by every form/row builder.
inline constexpr int kSpace1 = 4;
inline constexpr int kSpace2 = 8;
inline constexpr int kSpace3 = 12;
inline constexpr int kSpace4 = 16;
inline constexpr int kRowHeight = 28;
// Shared component geometry: one corner radius, one hairline width, a 2px selection bar.
inline constexpr int kRadius = 4;
inline constexpr int kHairline = 1;
inline constexpr int kSelectionBarWidth = 2;
// Fixed label column (about 40% of the default side bar) so the value column starts at the
// same x in every row builder.
inline constexpr int kRowLabelColumnWidth = 96;

// Section-header geometry: the icon is the left anchor, the chevron sits at the far right.
inline constexpr int kSectionIconX = 8;
inline constexpr int kSectionIconSize = 16;
inline constexpr int kSectionTitleX = 34;             // after icon (8 + 16 + 10); 8 when a section has no icon
inline constexpr int kSectionChevronRightInset = 16;  // chevron centered this far from the RIGHT edge
// The header owns the gap: this pad above and below its row.
inline constexpr int kSectionHeaderPad = kSpace2;
// Row-gutter icon buttons: a fixed slot holding an untinted glyph, shared by row actions and
// section-header actions.
inline constexpr int kRowGutterSlot = 24;
inline constexpr int kRowGutterGlyph = 16;

// Type ramp: ready-made QSS property fragments (color + size + weight), not full rules.
// The ramp's one size is also the application's base font.
inline constexpr int kTypeSizePx = 12;
inline const QString kTypeSection =
    resolveRoles(QStringLiteral("color: {text-primary}; font-size: %1px; font-weight: 600;").arg(kTypeSizePx));
inline const QString kTypeLabel =
    resolveRoles(QStringLiteral("color: {text-secondary}; font-size: %1px; font-weight: 400;").arg(kTypeSizePx));
inline const QString kTypeValue =
    resolveRoles(QStringLiteral("color: {text-primary}; font-size: %1px; font-weight: 400;").arg(kTypeSizePx));
inline const QString kTypeMono = resolveRoles(
    QStringLiteral("color: {text-primary}; font-size: %1px; font-weight: 400; font-family: Consolas, monospace;")
        .arg(kTypeSizePx));

// Button classes. Primary is capped at one per screen. Each carries its own :disabled rule
// so it does not fall through to a generic one. Padding is space-1 x space-3.
inline const QString kButtonPadding = QStringLiteral("%1px %2px").arg(kSpace1).arg(kSpace3);

inline const QString kPrimaryButtonStyle =
    resolveRoles(QStringLiteral(
                     "QPushButton { background: {accent-brand}; color: {text-on-accent}; "
                     "border: %1px solid {accent-brand}; border-radius: %2px; padding: %3; font-weight: 600; }"
                     "QPushButton:hover:enabled { background: {accent-interactive}; border-color: {accent-interactive}; }"
                     "QPushButton:pressed:enabled { background: {accent-brand}; border-color: {accent-brand}; }"
                     "QPushButton:disabled { background: {surface-2}; color: {text-disabled}; "
                     "border: %1px solid {surface-2}; }")
                     .arg(kHairline)
                     .arg(kRadius)
                     .arg(kButtonPadding));

inline const QString kSecondaryButtonStyle =
    resolveRoles(QStringLiteral(
                     "QPushButton { background: transparent; color: {text-secondary}; "
                     "border: %1px solid {outline}; border-radius: %2px; padding: %3; font-weight: 400; }"
                     "QPushButton:hover:enabled { background: {surface-hover}; color: {text-primary}; }"
                     "QPushButton:pressed:enabled { background: {surface-pressed}; }"
                     "QPushButton:disabled { background: transparent; color: {text-disabled}; "
                     "border: %1px solid {outline}; }")
                     .arg(kHairline)
                     .arg(kRadius)
                     .arg(kButtonPadding));

// Ghost: no fill, no border; toolbar tools, row gutter and section actions.
inline const QString kGhostButtonStyle =
    resolveRoles(QStringLiteral(
                     "QPushButton { background: transparent; color: {text-secondary}; border: none; "
                     "border-radius: %1px; padding: %2; font-weight: 400; }"
                     "QPushButton:hover:enabled { background: {surface-hover}; color: {text-primary}; }"
                     "QPushButton:pressed:enabled { background: {surface-pressed}; }"
                     "QPushButton:disabled { background: transparent; color: {text-disabled}; }")
                     .arg(kRadius)
                     .arg(kButtonPadding));

}  // namespace design
}  // namespace plnr
