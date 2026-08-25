#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector3D>

#include <geo/pick.h>

#include "camera.h"
#include "material_batch.h"

class QKeyEvent;
class QMouseEvent;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QPainter;
class QWheelEvent;

namespace plnr::viewport {

// InferenceCue::shape values -- mirror plnr::tools::kMarkerNone/Dot/Diamond/
// Square (tool.h) one-for-one, keep in sync.
inline constexpr int kInferenceCueMarkerNone = -1;
inline constexpr int kInferenceCueMarkerDot = 0;
inline constexpr int kInferenceCueMarkerDiamond = 1;
inline constexpr int kInferenceCueMarkerSquare = 2;

// industry-standard OpenGL viewport: static ground grid + axes triad, model
// geometry and overlays fed in by ViewportPresenter, middle-drag orbit/pan and
// wheel zoom on a Camera. Kernel-ignorant: it renders the buffers it is given.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }

    // Overwrites camera_ wholesale and repaints; paintGL reads it fresh every
    // frame, so there is nothing to upload.
    void setCameraState(const QVector3D& target, float azimuthDeg, float elevationDeg, float distance, float fovYDeg,
                         Camera::Projection projection);

    QSize minimumSizeHint() const override;

    // Replaces the ground grid + axes triad's frame: origin plus 3 orthonormal
    // directions (world X/Y/Z by default). Like every other setter here, safe
    // to call before the GL context exists -- cached, uploaded on next paint.
    void setAxesFrame(const QVector3D& origin, const QVector3D& xDir, const QVector3D& yDir, const QVector3D& zDir);

    // Qt-native mirror of events::FaceStyle -- order matches verbatim, keep in sync.
    enum class FaceStyle { Wireframe, HiddenLine, Shaded, ShadedWithTextures, Monochrome, XRay };

    // Face style, edge-style flags, AO toggle/strength and default front/back
    // colors, all read fresh by paintGL. The two colors mirror StyleStore's own
    // fields -- StyleStore is the authority, keep in sync.
    void setStyle(FaceStyle style, bool profiles, bool depthCue, bool backEdges, bool ambientOcclusion,
                  float aoStrength, const QVector3D& defaultFrontColor, const QVector3D& defaultBackColor);

    // Sun-shading/ground-shadow state, read by paintGL every frame. sunDirWorld:
    // unit direction FROM a surface point TOWARD the sun, zero below the horizon
    // (zero disables shading and shadows). light/dark: 0-100 sliders.
    void setShadowState(bool showShadows, bool useSunForShading, const QVector3D& sunDirWorld, float light,
                         float dark);

    // Fog state read by the face/edge fragment shaders. useBackgroundColor
    // picks this widget's own kBackgroundColor over customColor, resolved here
    // into fogColor_. startDistance/endDistance are world units from the eye.
    void setFog(bool enabled, float startDistance, float endDistance, bool useBackgroundColor,
                const QVector3D& customColor);

    // One material's resolved draw color: RGB plus opacity (0-1), keyed by a
    // MaterialRange's front/back material id. An id with no entry (including
    // the 0 sentinel) falls back to the style's default color at opacity 1.
    struct MaterialSwatch {
        QVector3D rgb;
        float opacity = 1.0f;

        // assetHash names this material's texture in textureCache_ (empty =
        // untextured, flat rgb/opacity look). textureBytes is the raw encoded
        // image, decoded lazily on textureFor()'s first miss for this hash.
        std::string assetHash;
        QByteArray textureBytes;
    };

    // Model geometry: edgeVerts (GL_LINES) and faceTris (GL_TRIANGLES, wound
    // per Face.normal, reordered into contiguous per-material runs), plus its
    // draw ranges. transparentRanges arrives UNORDERED, sorted on next upload.
    void setModelGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris,
                           std::vector<MaterialRange> opaqueRanges, std::vector<MaterialRange> transparentRanges,
                           std::unordered_map<geo::Id, MaterialSwatch> materialSwatches);

    // Model-edge classification buffers: profileVerts holds every profile-weight
    // edge, bandVerts 3 eye-distance bands of the Interior edges (0 = nearest/
    // thickest). Always recomputed; paintGL picks buckets at draw time.
    void setEdgeStyleGeometry(std::vector<float> profileVerts, std::array<std::vector<float>, 3> bandVerts);

    // Geometry OUTSIDE the current editing context, drawn light-gray INSTEAD
    // of through the normal model buffers -- never in both. ({}, {}) clears.
    void setDimmedGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris);

    // Guide overlay: one flat GL_LINES buffer carrying both guide lines'
    // pre-dashed segments and guide points' cross markers.
    void setGuideGeometry(std::vector<float> lineVerts);

    // One text label for the annotation overlay's QPainter pass. screenFixed
    // picks the live position field: true -> screenPos in widget pixels, false
    // -> worldPos, projected every frame. leader adds a box and leader line.
    struct AnnotationLabel {
        bool screenFixed{};
        QVector3D worldPos;
        QPointF screenPos;
        QString text;
        bool leader{};
    };

    // Annotation overlay: associated dimensions in normalLineVerts, non-
    // associated ones in invalidLineVerts, never both. labels is the
    // QPainter-drawn text pass -- CPU-only, no GPU upload.
    void setAnnotationGeometry(std::vector<float> normalLineVerts, std::vector<float> invalidLineVerts,
                                std::vector<AnnotationLabel> labels);

    // Section-plane overlay: rectangle-plus-corner-grip geometry for every
    // non-hidden plane. activeLineVerts draws at stronger alpha.
    void setSectionGeometry(std::vector<float> activeLineVerts, std::vector<float> inactiveLineVerts);

    // Section cut: model faces/edges only (overlays stay unclipped) discard
    // fragments strictly on the POSITIVE side, dot(p,normal) > dot(point,normal)
    // -- normal points at the half the cut REMOVES, and need not be normalized.
    struct ClipPlane {
        QVector3D normal;
        QVector3D point;
    };
    void setClipPlane(std::optional<ClipPlane> plane);

    // One batch of preview-overlay line segments (xyz per vertex, GL_LINES)
    // plus its own straight-RGBA draw color. Mirrors
    // plnr::tools::ToolContext::PreviewBatch one-for-one.
    struct PreviewBatch {
        std::vector<float> lineVerts;
        float r, g, b, a;
    };

    // Temporary tool-feedback overlay (rubber-band/protractor lines, each batch
    // its own color) plus a snap marker. ({}, nullopt) clears everything.
    void setPreviewBatches(std::vector<PreviewBatch> batches, std::optional<QVector3D> marker);

    // Qt-native mirror of plnr::tools::InferenceCue. traceFrom, when set, draws
    // a dashed world-space line from traceFrom to pos in (tr,tg,tb,ta).
    struct InferenceCue {
        QVector3D pos;
        int shape = kInferenceCueMarkerDot;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        QString screenTip;
        bool warning = false;
        std::optional<QVector3D> traceFrom;
        float tr = 0.0f, tg = 0.0f, tb = 0.0f, ta = 1.0f;
    };

    // Typed inference-cue overlay (nullopt clears): a QPainter marker glyph at
    // cue's projected screen position plus its ScreenTip text -- light-yellow
    // box, or red text and no marker when cue->warning.
    void setInferenceCue(std::optional<InferenceCue> cue);

    // Selection highlight, blue: edgeVerts GL_LINES, faceTris GL_TRIANGLES
    // (drawn unculled, filled with a screen-space dot stipple over the still-
    // visible face), pointVerts GL_POINTS. All-empty clears.
    void setSelectionGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris, std::vector<float> pointVerts);

    // Push/Pull target-face highlight (empty clears): the same blue stipple
    // pass and color as the selection face fill, for a hovered face.
    void setHoverFaceTris(std::vector<float> faceTris);

    // 2D drag-selection rubber band: a/b are opposite corners in widget pixels,
    // drawn in NDC with an identity MVP. Either nullopt clears it.
    void setScreenRect(std::optional<QPointF> a, std::optional<QPointF> b);

    // World-space picking ray through pos, in the widget-pixel space
    // QMouseEvent reports (Qt top-left origin).
    geo::Ray makeRay(QPointF pos) const;

    // Callback wheelEvent() consults for the scroll-wheel zoom anchor: takes
    // the widget-pixel cursor position, returns the world point to zoom
    // toward, or nullopt to decline (center-zoom camera_.zoom() fallback).
    void setZoomAnchorResolver(std::function<std::optional<QVector3D>(const QPointF&)> resolver);

    // Overlay buffer sizes and dirty flags, for the debug bridge's
    // overlay_stats query.
    struct OverlayStats {
        int selectionEdgeVertexCount{};
        int selectionFaceVertexCount{};
        int selectionPointVertexCount{};
        int pendingSelectionEdgeFloats{};
        int pendingSelectionFaceFloats{};
        int pendingSelectionPointFloats{};
        bool selectionDirty{};
        bool screenRectSet{};
        // paintGL invocation counter + the vertex count it last drew with --
        // tells a stale grabFramebuffer from a draw that ran on empty buffers.
        int paintCount{};
        int lastPaintSelectionEdgeVertexCount{};
        // First two cached selection-edge vertices, xyz each; zeros when absent.
        std::array<float, 6> firstSelectionEdgeVerts{};
    };
    OverlayStats overlayStats() const;

    // Pick tolerances (vertex/edge) in world units that stay a constant pixel
    // size on screen -- Camera::worldPerPixel() at `distance` from the eye.
    geo::PickOptions tolerancesAt(double distance) const;

signals:
    // Forwarded pointer/keyboard input for tools to consume: raw Qt event
    // data, not app-domain types.
    void pointerPressed(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void pointerMoved(QPointF pos, Qt::KeyboardModifiers modifiers);
    void pointerReleased(QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    // modifiers carries Ctrl/Shift on a key press, as with pointer input --
    // e.g. Circle/Polygon's segment +/- adjustment needs Ctrl held.
    void keyPressed(int key, Qt::KeyboardModifiers modifiers);
    // A right-button press, or the second press of a rapid right double-click;
    // pos is widget-pixel. Separate from pointerPressed: no tool routing or
    // clickCount involved, just "build a menu for what's under pos".
    void contextMenuRequested(QPointF pos);

    // Emitted at the END of a navigation gesture -- an orbit/pan drag's release
    // or each wheel zoom step -- never per mouse-move, which would flood the
    // kernel with an event per drag pixel.
    void cameraNavigated(QVector3D target, float azimuthDeg, float elevationDeg, float distance, float fovYDeg);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    // Qt routes the second press of a double-click here instead of firing a
    // second mousePressEvent, and ToolController's clickCount synthesis needs
    // both presses. Same guard logic and signal as mousePressEvent.
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildSceneGeometry();
    float aspectRatio() const;
    // Uploads if axesFrameDirty_. Unlike the other upload* methods, this one
    // also REGENERATES the CPU-side vertex data (the frame is 4 vectors).
    void uploadAxesGeometryIfDirty();
    // Uploads pendingEdgeVerts_/pendingFaceTris_ if modelGeometryDirty_, then
    // clears the flag. Called at the top of paintGL(); no-op when unchanged.
    void uploadModelGeometryIfDirty();
    // Same, for the edge-style buffers, plus a CPU expansion of profile and
    // bands 0/1 into thick-quad vertices. Band 2 has no thick counterpart.
    void uploadEdgeStyleGeometryIfDirty();
    // Cached QOpenGLTexture for hash, created from bytes (GL_REPEAT,
    // linear+mipmap) on a miss. nullptr if bytes is empty or undecodable.
    QOpenGLTexture* textureFor(const std::string& hash, const QByteArray& bytes);
    // Same, for the dimmed-geometry overlay buffers.
    void uploadDimmedGeometryIfDirty();
    // Same, for the guide-line/guide-point overlay buffer.
    void uploadGuideGeometryIfDirty();
    // Same, for the annotation overlay's two line buffers.
    void uploadAnnotationGeometryIfDirty();
    // Same, for the section-plane overlay's two line buffers.
    void uploadSectionGeometryIfDirty();
    // Same, for the preview overlay; also resizes previewBatches_ to match
    // pendingPreviewBatches_.
    void uploadPreviewIfDirty();
    // Same, for the selection-highlight overlay buffers.
    void uploadSelectionIfDirty();
    // Same, for the Push/Pull hover-face overlay buffer.
    void uploadHoverFaceIfDirty();
    // Same, for the screen rect -- rebuilds its 4 NDC verts from
    // pendingScreenRectA_/B_.
    void uploadScreenRectIfDirty();

    // (Re)creates every AO GL object at the widget's pixel size, deleting the
    // old ones first so repeated resizes never leak. w/h clamped to >= 1.
    void recreateAoTargets(int w, int h);

    // Runs the four-pass SSAO pipeline and multiply-composites it onto the
    // default framebuffer. Must run after model edges but before any overlay:
    // AO darkens faces/edges, never overlays. Leaves flatColorProgram_ BOUND
    // with uMvp restored -- paintGL's call site relies on that.
    void renderAmbientOcclusion(const QMatrix4x4& view, const QMatrix4x4& projection);

    // Projects a world point to widget-pixel coordinates. nullopt when behind
    // the eye; deliberately NOT clipped to the viewport rect, so a cue or tip
    // just past the edge still draws.
    std::optional<QPointF> projectToScreen(const QVector3D& world) const;

    // Rebuilds and uploads the cue trace line's dash segments (world-space,
    // sized to ~8px on screen at any zoom). Called every frame a trace is
    // active; too cheap to warrant a dirty flag.
    void uploadCueTraceIfActive();

    // Draws inferenceCue_'s marker glyph + ScreenTip; painter must already be active.
    void paintInferenceCue(QPainter& painter, const InferenceCue& cue) const;

    // Draws every pendingAnnotationLabels_ entry; painter must already be active.
    void paintAnnotationLabels(QPainter& painter) const;

    Camera camera_;

    // Grid/axes triad: per-vertex-color line shader (uMvp + aColor).
    // GL_DYNAMIC_DRAW -- setAxesFrame can replace the frame at runtime.
    QOpenGLShaderProgram* program_ = nullptr;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertexCount_ = 0;

    // Pending grid/axes frame: world X/Y/Z by default, matching AxesStore, so
    // a run before the first rebuildAxes() still renders the world axes.
    QVector3D pendingAxesOrigin_{0.0f, 0.0f, 0.0f};
    QVector3D pendingAxesXDir_{1.0f, 0.0f, 0.0f};
    QVector3D pendingAxesYDir_{0.0f, 1.0f, 0.0f};
    QVector3D pendingAxesZDir_{0.0f, 0.0f, 1.0f};
    bool axesFrameDirty_ = false;

    // Model geometry (edges + face fills): uniform-color shader (uMvp +
    // uColor), position-only vertex layout, GL_DYNAMIC_DRAW.
    QOpenGLShaderProgram* flatColorProgram_ = nullptr;

    // Textured face shader: pos loc 0 + uv loc 1, plus the same clip-plane
    // uniforms flatColorProgram_ carries. Used for one range-side draw when
    // that side's material is textured; shares vaoFaces_/vboFaces_ (stride 5).
    QOpenGLShaderProgram* texturedProgram_ = nullptr;

    // Back-edges stipple: the flat position-only vertex stage plus a fragment
    // shader discarding in a screen-space checkerboard -- core profile has no
    // GL line stipple.
    QOpenGLShaderProgram* backEdgeProgram_ = nullptr;

    // Selection/hover-face stipple: the flat fragment shader plus one dot-grid
    // discard, fog/clip unchanged. One program and one uColor serve both the
    // selected-face fill and the Push/Pull hover overlay.
    QOpenGLShaderProgram* stippleProgram_ = nullptr;

    // Fullscreen sky/background gradient -- the frame's first draw, before the grid.
    QOpenGLShaderProgram* skyProgram_ = nullptr;

    // Thick-line quad expansion: builds line thickness as real screen-space
    // quad geometry rather than a widened GL_LINES primitive.
    QOpenGLShaderProgram* thickLineProgram_ = nullptr;

    // Ground-shadow shader: flattens each face vertex onto z=0 along the sun
    // direction BEFORE the camera MVP, then fills translucently under a
    // stencil mask so each pixel darkens once. Draws from vaoFaces_.
    QOpenGLShaderProgram* shadowProgram_ = nullptr;

    // The FULL, unclassified edge set -- used ONLY by the back-edges pass.
    // Classified/banded rendering uses the separate buffers below.
    unsigned int vaoModelEdges_ = 0;
    unsigned int vboModelEdges_ = 0;
    int modelEdgeVertexCount_ = 0;

    // Edge-style classification buffers -- see setEdgeStyleGeometry.
    unsigned int vaoProfileEdges_ = 0;
    unsigned int vboProfileEdges_ = 0;
    int profileEdgeVertexCount_ = 0;

    std::array<unsigned int, 3> vaoBandEdges_{};
    std::array<unsigned int, 3> vboBandEdges_{};
    std::array<int, 3> bandEdgeVertexCount_{};

    // Thick-line quad geometry, CPU-expanded from the pending edge verts: 6
    // unindexed vertices per input segment, each {thisEnd(3f), otherEnd(3f),
    // side(1f: +-1)}, stride 7. Only profile and bands 0/1 have one.
    unsigned int vaoProfileEdgesThick_ = 0;
    unsigned int vboProfileEdgesThick_ = 0;
    int profileEdgeThickVertexCount_ = 0;

    std::array<unsigned int, 2> vaoBandEdgesThick_{};
    std::array<unsigned int, 2> vboBandEdgesThick_{};
    std::array<int, 2> bandEdgeThickVertexCount_{};

    std::vector<float> pendingProfileEdgeVerts_;
    std::array<std::vector<float>, 3> pendingBandEdgeVerts_;
    bool edgeStyleGeometryDirty_ = false;

    unsigned int vaoFaces_ = 0;
    unsigned int vboFaces_ = 0;
    int faceVertexCount_ = 0;

    std::vector<float> pendingEdgeVerts_;
    std::vector<float> pendingFaceTris_;
    bool modelGeometryDirty_ = false;

    // GL texture cache keyed by asset hash, filled lazily by textureFor().
    // Never evicted: assets are content-addressed, so a hit is always correct,
    // at the cost of unbounded growth over a long session.
    std::unordered_map<std::string, std::unique_ptr<QOpenGLTexture>> textureCache_;

    // Material-batched face draw ranges. pending*_ are the setModelGeometry
    // inputs, on the same modelGeometryDirty_ flag as pendingFaceTris_;
    // transparentRanges_ is sorted back-to-front during upload, not copied.
    std::vector<MaterialRange> pendingOpaqueRanges_;
    std::vector<MaterialRange> pendingTransparentRanges_;
    std::unordered_map<geo::Id, MaterialSwatch> pendingMaterialSwatches_;

    std::vector<MaterialRange> opaqueRanges_;
    std::vector<MaterialRange> transparentRanges_;
    std::unordered_map<geo::Id, MaterialSwatch> materialSwatches_;

    // Editing-context dimmed overlay: geometry outside the current context,
    // light-gray, drawn FIRST, before the normal model geometry.
    unsigned int vaoDimmedEdges_ = 0;
    unsigned int vboDimmedEdges_ = 0;
    int dimmedEdgeVertexCount_ = 0;

    unsigned int vaoDimmedFaces_ = 0;
    unsigned int vboDimmedFaces_ = 0;
    int dimmedFaceVertexCount_ = 0;

    std::vector<float> pendingDimmedEdgeVerts_;
    std::vector<float> pendingDimmedFaceTris_;
    bool dimmedGeometryDirty_ = false;

    // Guide-line/guide-point overlay: one position-only GL_LINES buffer
    // holding both dashed line segments and point-cross segments.
    unsigned int vaoGuides_ = 0;
    unsigned int vboGuides_ = 0;
    int guideVertexCount_ = 0;

    std::vector<float> pendingGuideVerts_;
    bool guideGeometryDirty_ = false;

    // Annotation overlay: two position-only GL_LINES buffers -- associated
    // dimension geometry and non-associated, in different colors. Text labels
    // are not GPU geometry; see pendingAnnotationLabels_ below.
    unsigned int vaoAnnotationLines_ = 0;
    unsigned int vboAnnotationLines_ = 0;
    int annotationLineVertexCount_ = 0;

    unsigned int vaoAnnotationLinesInvalid_ = 0;
    unsigned int vboAnnotationLinesInvalid_ = 0;
    int annotationLineInvalidVertexCount_ = 0;

    std::vector<float> pendingAnnotationLineVerts_;
    std::vector<float> pendingAnnotationLineInvalidVerts_;
    bool annotationGeometryDirty_ = false;

    // CPU-side only, QPainter-drawn every frame like inferenceCue_.
    std::vector<AnnotationLabel> pendingAnnotationLabels_;

    // Section-plane overlay: active- and inactive-plane geometry, drawn via
    // previewLineProgram_ (straight-alpha blend, active stronger).
    unsigned int vaoSectionActive_ = 0;
    unsigned int vboSectionActive_ = 0;
    int sectionActiveVertexCount_ = 0;

    unsigned int vaoSectionInactive_ = 0;
    unsigned int vboSectionInactive_ = 0;
    int sectionInactiveVertexCount_ = 0;

    std::vector<float> pendingSectionActiveVerts_;
    std::vector<float> pendingSectionInactiveVerts_;
    bool sectionGeometryDirty_ = false;

    // Section-plane clip state -- see setClipPlane.
    std::optional<ClipPlane> clipPlane_;

    // Active style state -- see setStyle. Defaults match agent::StyleStore's
    // ctor defaults; the two color literals mirror its kDefaultFrontColorR/G/B
    // and kDefaultBackColorR/G/B (style_store.h) -- keep them in sync.
    FaceStyle style_ = FaceStyle::ShadedWithTextures;
    QVector3D styleFrontColor_{1.0f, 1.0f, 0.98f};
    QVector3D styleBackColor_{0.651f, 0.694f, 0.729f};

    // Edge-style flags, default false to match agent::StyleStore. Pure
    // draw-time state: toggling them never touches the classification buffers.
    bool profiles_ = false;
    bool depthCue_ = false;
    bool backEdges_ = false;

    // Sun-shading/ground-shadow state -- see setShadowState. Defaults match
    // agent::ShadowStore's ctor defaults.
    bool useSunForShading_ = false;
    bool showShadows_ = false;
    QVector3D sunDirection_{0.0f, 0.0f, 0.0f};
    // Light/Dark sliders, 0-100 -- defaults match agent::kDefaultLight/
    // kDefaultDark (shadow_store.h), keep in sync.
    float shadowLight_ = 80.0f;
    float shadowDark_ = 45.0f;

    // Fog state -- see setFog. Defaults match agent::FogStore's ctor defaults.
    bool fogEnabled_ = false;
    float fogStartDistance_ = 5.0f;
    float fogEndDistance_ = 40.0f;
    QVector3D fogColor_{230.0f / 255.0f, 235.0f / 255.0f, 240.0f / 255.0f};

    // AO state -- see setStyle. Defaults match agent::StyleStore's ctor
    // defaults (kDefaultAoStrength, style_store.h).
    bool ambientOcclusion_ = false;
    float aoStrength_ = 0.7f;

    // AO pipeline programs -- see renderAmbientOcclusion.
    QOpenGLShaderProgram* aoGeomProgram_ = nullptr;       // Pass A: depth + view-space normal
    QOpenGLShaderProgram* aoEstimateProgram_ = nullptr;   // Pass B: hemisphere-kernel occlusion estimate
    QOpenGLShaderProgram* aoBlurProgram_ = nullptr;       // Pass C: 4x4 box blur
    QOpenGLShaderProgram* aoCompositeProgram_ = nullptr;  // Pass D: multiply-blend composite

    // Pass A targets, FULL window resolution, non-MSAA. aoDepthTex_ is
    // GL_DEPTH_COMPONENT24 and NEAREST-filtered: depth must never blend across
    // a silhouette edge. aoNormalTex_ is GL_RGB16F -- view normals span -1..1.
    unsigned int aoGeomFbo_ = 0;
    unsigned int aoDepthTex_ = 0;
    unsigned int aoNormalTex_ = 0;

    // Pass B/C targets, HALF window resolution (each dimension floor-divided
    // by 2, min 1). aoRawTex_ holds Pass B's raw occlusion estimate (GL_R8, a
    // single 0-1 scalar); aoBlurTex_ holds Pass C's blur, which Pass D samples.
    unsigned int aoEstimateFbo_ = 0;
    unsigned int aoRawTex_ = 0;
    unsigned int aoBlurFbo_ = 0;
    unsigned int aoBlurTex_ = 0;

    // Current pixel size of the targets above, so renderAmbientOcclusion() can
    // size its glViewport calls. Updated by recreateAoTargets().
    int aoFullWidth_ = 0;
    int aoFullHeight_ = 0;
    int aoHalfWidth_ = 0;
    int aoHalfHeight_ = 0;

    // Shared fullscreen-triangle geometry: one oversized triangle covering the
    // NDC square with no diagonal seam ((-1,-1),(3,-1),(-1,3)). Created once in
    // initializeGL; the content never changes, so there is no dirty flag.
    unsigned int vaoFullscreenTri_ = 0;
    unsigned int vboFullscreenTri_ = 0;

    // Snap-marker shader for the preview overlay: core-profile GL_POINTS
    // sizing needs the vertex shader to write gl_PointSize (with
    // GL_PROGRAM_POINT_SIZE), which flatColorProgram_ does not.
    QOpenGLShaderProgram* pointProgram_ = nullptr;

    // Preview-batch line shader: same position-only layout as
    // flatColorProgram_, but with a vec4 (straight-alpha) uColor.
    QOpenGLShaderProgram* previewLineProgram_ = nullptr;

    // GPU-side state for one uploaded preview batch. uploadPreviewIfDirty()
    // grows/shrinks the vector to match the pending list rather than pooling.
    struct PreviewBatchGL {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int vertexCount = 0;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    };
    std::vector<PreviewBatchGL> previewBatches_;

    unsigned int vaoPreviewMarker_ = 0;
    unsigned int vboPreviewMarker_ = 0;
    bool previewMarkerSet_ = false;

    std::vector<PreviewBatch> pendingPreviewBatches_;
    std::optional<QVector3D> pendingPreviewMarker_;
    bool previewDirty_ = false;

    // Typed inference-cue overlay: CPU-side only (marker and ScreenTip are
    // QPainter-drawn), except the trace-line dash buffer below, which is real
    // GL geometry drawn through previewLineProgram_.
    std::optional<InferenceCue> inferenceCue_;
    unsigned int vaoCueTrace_ = 0;
    unsigned int vboCueTrace_ = 0;
    int cueTraceVertexCount_ = 0;

    // Selection-highlight overlay: edges and face fill reuse flatColorProgram_
    // (fill unculled, one pass); points reuse pointProgram_. Coincides exactly
    // in depth with the model geometry -- see paintGL's GL_LEQUAL bracketing.
    unsigned int vaoSelectionFaces_ = 0;
    unsigned int vboSelectionFaces_ = 0;
    int selectionFaceVertexCount_ = 0;

    unsigned int vaoSelectionEdges_ = 0;
    unsigned int vboSelectionEdges_ = 0;
    int selectionEdgeVertexCount_ = 0;

    unsigned int vaoSelectionPoints_ = 0;
    unsigned int vboSelectionPoints_ = 0;
    int selectionPointVertexCount_ = 0;

    std::vector<float> pendingSelectionEdgeVerts_;
    std::vector<float> pendingSelectionFaceTris_;
    std::vector<float> pendingSelectionPointVerts_;
    bool selectionDirty_ = false;

    // Push/Pull hover-face overlay, drawn through the same stipple pass and
    // uColor as the selection-face fill but on its own VAO/VBO: hover changes
    // every pointer-move, selection only on a SelectRequested.
    unsigned int vaoHoverFaces_ = 0;
    unsigned int vboHoverFaces_ = 0;
    int hoverFaceVertexCount_ = 0;

    std::vector<float> pendingHoverFaceTris_;
    bool hoverFaceDirty_ = false;

    // Drag-selection rubber band: drawn in NDC (identity MVP, depth test off)
    // at the very end of paintGL, as a GL_LINE_LOOP through flatColorProgram_.
    unsigned int vaoScreenRect_ = 0;
    unsigned int vboScreenRect_ = 0;
    bool screenRectSet_ = false;

    std::optional<QPointF> pendingScreenRectA_;
    std::optional<QPointF> pendingScreenRectB_;
    bool screenRectDirty_ = false;

    // Middle-drag navigation: which mode is active (mutually exclusive) plus
    // the last mouse position, for per-move deltas.
    bool orbiting_ = false;
    bool panning_ = false;
    QPoint lastMousePos_;

    // See setZoomAnchorResolver. Unset until ToolController::onRegister
    // installs it; wheelEvent treats unset the same as a nullopt result.
    std::function<std::optional<QVector3D>(const QPointF&)> zoomAnchorResolver_;

    // Diagnostics for OverlayStats (debug bridge).
    int paintCount_ = 0;
    int lastPaintSelectionEdgeVertexCount_ = 0;
};

}  // namespace plnr::viewport
