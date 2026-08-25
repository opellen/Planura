#include "viewport_presenter.h"

#include <array>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <algorithm>

#include <QAction>
#include <QByteArray>

#include <geo/infer.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/triangulate.h>
#include <geo/vec3.h>

#include "agent/annotation_store.h"
#include "render_log.h"
#include "agent/asset_repository.h"
#include "agent/axes_store.h"
#include "agent/camera_store.h"
#include "agent/edit_context_store.h"
#include "agent/fog_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/selection_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/sun_position.h"
#include "agent/tag_store.h"
#include "viewport/edge_style.h"
#include "viewport/geo_convert.h"
#include "viewport/material_batch.h"
#include "viewport/viewport_widget.h"

namespace plnr::ui {

namespace {

// events::FaceStyle -> viewport::ViewportWidget::FaceStyle: a literal
// switch (not a static_cast), so the two enums don't need to stay numerically aligned.
viewport::ViewportWidget::FaceStyle toViewportFaceStyle(events::FaceStyle style) {
    switch (style) {
        case events::FaceStyle::Wireframe:
            return viewport::ViewportWidget::FaceStyle::Wireframe;
        case events::FaceStyle::HiddenLine:
            return viewport::ViewportWidget::FaceStyle::HiddenLine;
        case events::FaceStyle::Shaded:
            return viewport::ViewportWidget::FaceStyle::Shaded;
        case events::FaceStyle::ShadedWithTextures:
            return viewport::ViewportWidget::FaceStyle::ShadedWithTextures;
        case events::FaceStyle::Monochrome:
            return viewport::ViewportWidget::FaceStyle::Monochrome;
        case events::FaceStyle::XRay:
            return viewport::ViewportWidget::FaceStyle::XRay;
    }
    return viewport::ViewportWidget::FaceStyle::ShadedWithTextures;  // unreachable -- FaceStyle is exhaustively handled above
}

// events::Projection -> viewport::Camera::Projection: same literal-switch
// shape as toViewportFaceStyle above.
viewport::Camera::Projection toViewportProjection(events::Projection projection) {
    switch (projection) {
        case events::Projection::Perspective:
            return viewport::Camera::Projection::Perspective;
        case events::Projection::Parallel:
            return viewport::Camera::Projection::Parallel;
        case events::Projection::TwoPoint:
            return viewport::Camera::Projection::TwoPoint;
    }
    return viewport::Camera::Projection::Perspective;  // unreachable -- Projection is exhaustively handled above
}

void appendVertex(std::vector<float>& out, const geo::Vec3& p) {
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
}

// Material-batched normal face buffer's vertex layout: pos(3)+uv(2)+normal(3)
// interleaved, vs appendVertex's position-only 3 floats (edges and the
// dimmed overlay, which are never shaded). uv is {0,0} for an untextured
// face. normal is the face's world-space unit normal, flat per triangle
// (every Face in this kernel is planar, so flat and smooth shading coincide).
void appendVertexUvNormal(std::vector<float>& out, const geo::Vec3& p, const viewport::Uv& uv, const geo::Vec3& normal) {
    out.push_back(static_cast<float>(p.x));
    out.push_back(static_cast<float>(p.y));
    out.push_back(static_cast<float>(p.z));
    out.push_back(static_cast<float>(uv.u));
    out.push_back(static_cast<float>(uv.v));
    out.push_back(static_cast<float>(normal.x));
    out.push_back(static_cast<float>(normal.y));
    out.push_back(static_cast<float>(normal.z));
}

// A guide line is conceptually infinite; kGuideLineExtent is the MVP
// approximation (fixed span around the origin, since this app has no
// live-extent concept yet). kGuideDashLen/kGuideGapLen bake the dashed
// look into vertex data (core-profile GL has no line stipple).
constexpr double kGuideLineExtent = 1000.0;
constexpr double kGuideDashLen = 0.5;
constexpr double kGuideGapLen = 0.5;

// Half-length of a guide-point cross marker's arms, in world units --
// fixed size (unlike the snap marker/selection points' constant-pixel
// GL_POINTS), so it scales with zoom (MVP simplification).
constexpr double kGuideCrossArm = 0.15;

void appendDashedSegment(std::vector<float>& out, const geo::Vec3& from, const geo::Vec3& to) {
    const geo::Vec3 dir = to - from;
    const double totalLen = geo::length(dir);
    if (totalLen < geo::kEps) return;
    const geo::Vec3 unit = dir * (1.0 / totalLen);

    for (double t = 0.0; t < totalLen; t += kGuideDashLen + kGuideGapLen) {
        const double segEnd = std::min(t + kGuideDashLen, totalLen);
        appendVertex(out, from + unit * t);
        appendVertex(out, from + unit * segEnd);
    }
}

void appendGuidePointCross(std::vector<float>& out, const geo::Vec3& p) {
    appendVertex(out, p - geo::Vec3{kGuideCrossArm, 0.0, 0.0});
    appendVertex(out, p + geo::Vec3{kGuideCrossArm, 0.0, 0.0});
    appendVertex(out, p - geo::Vec3{0.0, kGuideCrossArm, 0.0});
    appendVertex(out, p + geo::Vec3{0.0, kGuideCrossArm, 0.0});
    appendVertex(out, p - geo::Vec3{0.0, 0.0, kGuideCrossArm});
    appendVertex(out, p + geo::Vec3{0.0, 0.0, kGuideCrossArm});
}

void appendSegment(std::vector<float>& out, const geo::Vec3& a, const geo::Vec3& b) {
    appendVertex(out, a);
    appendVertex(out, b);
}

// An Instance's own front-slot material (0 if unpainted or unregistered) --
// what nextInheritedMaterialId needs for this instance's children.
// backMaterialId is always unused for an Instance ref.
geo::Id instanceOwnFrontMaterial(const agent::MaterialRepository* materialStore, geo::Id instanceId) {
    if (!materialStore) return 0;
    const agent::MaterialAssignment* own =
        materialStore->assignment(events::EntityRef{geo::EntityKind::Instance, instanceId});
    return own ? own->frontMaterialId : geo::Id{0};
}

// Same fixed tick half-length as DimensionTool's live preview (duplicated,
// not shared -- tool .cpp files stay self-contained).
constexpr double kDimensionTickHalfLen = 0.1;

// Same 2-decimal, no-unit-suffix formatting every tool defines locally for
// itself (e.g. TapeMeasureTool::formatExact) -- this app has no
// unit-system concept anywhere else.
std::string formatDistance(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Section-plane overlay geometry, duplicated from tools::SectionPlaneTool's
// own helpers rather than shared -- ui can't depend on tools, and tool.h
// can't depend on this file either, so each side keeps its own small copy.

// An arbitrary but deterministic orthonormal in-plane basis (u, v) for
// `normal` -- same helper-axis-by-z-threshold idiom as
// TapeMeasureTool::defaultPerpDir, generalized to a full 2D basis.
std::pair<geo::Vec3, geo::Vec3> sectionPlaneBasis(const geo::Vec3& normal) {
    const geo::Vec3 helper = std::fabs(normal.z) > 0.9 ? geo::Vec3{1.0, 0.0, 0.0} : geo::Vec3{0.0, 0.0, 1.0};
    const geo::Vec3 uRaw = geo::cross(helper, normal);
    const double uLen = geo::length(uRaw);
    const geo::Vec3 u = uLen > geo::kEps ? uRaw * (1.0 / uLen) : geo::Vec3{1.0, 0.0, 0.0};
    const geo::Vec3 v = geo::cross(normal, u);
    return {u, v};
}

// Bounding box of every vertex currently in model. valid is false for a
// null/empty model.
struct SectionBbox {
    geo::Vec3 min, max;
    bool valid{};
};

SectionBbox sectionModelBbox(const geo::Model* model) {
    SectionBbox box;
    if (!model) return box;
    bool first = true;
    for (const auto& [id, v] : model->vertices()) {
        (void)id;
        if (first) {
            box.min = box.max = v.pos;
            first = false;
        } else {
            box.min.x = std::min(box.min.x, v.pos.x);
            box.min.y = std::min(box.min.y, v.pos.y);
            box.min.z = std::min(box.min.z, v.pos.z);
            box.max.x = std::max(box.max.x, v.pos.x);
            box.max.y = std::max(box.max.y, v.pos.y);
            box.max.z = std::max(box.max.z, v.pos.z);
        }
    }
    box.valid = !first;
    return box;
}

// Same margin formula as FlipTool::margin() (20% of the bbox diagonal, floored at 0.3 world units).
double sectionMargin(const SectionBbox& box) {
    constexpr double kFraction = 0.2;
    constexpr double kMinMargin = 0.3;
    if (!box.valid) return kMinMargin;
    return std::max(geo::distance(box.min, box.max) * kFraction, kMinMargin);
}

// Half the bbox diagonal plus sectionMargin() -- a uniform half-extent,
// since an arbitrary section-plane orientation has no simple per-axis bbox
// projection (unlike FlipTool's three cardinal planes).
double sectionPlaneHalfExtent(const SectionBbox& box) {
    constexpr double kEmptyModelHalfExtent = 3.0;
    if (!box.valid) return kEmptyModelHalfExtent + sectionMargin(box);
    return geo::distance(box.min, box.max) * 0.5 + sectionMargin(box);
}

// Outlined rectangle + small L-bracket corner-grip ticks -- same shape (and
// same UNVERIFIED-look caveat) as tools::SectionPlaneTool's own
// appendPlaneRectangle, duplicated here for the committed-plane render.
void appendSectionPlaneRectangle(std::vector<float>& verts, const geo::Vec3& center, const geo::Vec3& u,
                                  const geo::Vec3& v, double half) {
    const geo::Vec3 uOff = u * half;
    const geo::Vec3 vOff = v * half;
    const geo::Vec3 c1 = center + uOff + vOff;
    const geo::Vec3 c2 = center - uOff + vOff;
    const geo::Vec3 c3 = center - uOff - vOff;
    const geo::Vec3 c4 = center + uOff - vOff;
    appendSegment(verts, c1, c2);
    appendSegment(verts, c2, c3);
    appendSegment(verts, c3, c4);
    appendSegment(verts, c4, c1);

    constexpr double kGripFraction = 0.12;
    const double grip = half * kGripFraction;
    auto appendGrip = [&](const geo::Vec3& corner, const geo::Vec3& intoU, const geo::Vec3& intoV) {
        appendSegment(verts, corner, corner + intoU * grip);
        appendSegment(verts, corner, corner + intoV * grip);
    };
    appendGrip(c1, -u, -v);
    appendGrip(c2, u, -v);
    appendGrip(c3, u, v);
    appendGrip(c4, -u, v);
}

}  // namespace

ViewportPresenter::ViewportPresenter(ordo::core::AppKernel& kernel, viewport::ViewportWidget* viewport,
                                      QAction* perspectiveAction, QAction* parallelProjectionAction,
                                      QAction* twoPointAction, QAction* previousAction, QAction* nextAction)
    : Presenter(kernel, QStringLiteral("ViewportPresenter"), viewport),
      viewport_(viewport),
      perspectiveAction_(perspectiveAction),
      parallelProjectionAction_(parallelProjectionAction),
      twoPointAction_(twoPointAction),
      previousAction_(previousAction),
      nextAction_(nextAction) {}

void ViewportPresenter::onRegister() {
    subscribe<events::GeometryChanged>(&ViewportPresenter::onGeometryChanged);
    // MoveTool's live move-ghost -- see onMoveGhostUpdated's own comment.
    subscribe<events::MoveGhostUpdated>(&ViewportPresenter::onMoveGhostUpdated);
    // PushPullTool's live extrusion ghost -- see onExtrudeGhostUpdated's
    // own comment.
    subscribe<events::ExtrudeGhostUpdated>(&ViewportPresenter::onExtrudeGhostUpdated);
    subscribe<events::SelectionChanged>(&ViewportPresenter::onSelectionChanged);
    subscribe<events::EditContextChanged>(&ViewportPresenter::onEditContextChanged);
    subscribe<events::GuidesChanged>(&ViewportPresenter::onGuidesChanged);
    subscribe<events::AxesChanged>(&ViewportPresenter::onAxesChanged);
    subscribe<events::AnnotationsChanged>(&ViewportPresenter::onAnnotationsChanged);
    subscribe<events::SectionsChanged>(&ViewportPresenter::onSectionsChanged);
    subscribe<events::CameraChanged>(&ViewportPresenter::onCameraChanged);
    // rebuild() re-resolves every face's material via MaterialRepository, so
    // MaterialsChanged needs the same full rebuild as GeometryChanged.
    subscribe<events::MaterialsChanged>(&ViewportPresenter::onMaterialsChanged);
    // No re-walk needed, just a plain widget-state push (see
    // onStyleChanged's own comment).
    subscribe<events::StyleChanged>(&ViewportPresenter::onStyleChanged);
    // Same "no re-walk, just a plain widget-state push" shape as
    // StyleChanged just above (see onShadowsChanged's own comment).
    subscribe<events::ShadowsChanged>(&ViewportPresenter::onShadowsChanged);
    // Same shape, for FogStore (see onFogChanged's own comment).
    subscribe<events::FogChanged>(&ViewportPresenter::onFogChanged);
    // The lighter edge-style recompute trigger -- re-classifies the cached
    // adjacency against the new eye, no scene re-walk (see onCameraMoved's
    // own comment).
    subscribe<events::CameraMoved>(&ViewportPresenter::onCameraMoved);
    // The one signal-based (not kernel-event) connection this presenter
    // makes: ViewportWidget stays kernel-ignorant, so this is the seam
    // that turns its Qt signal into a CameraNavigated send.
    connect(viewport_, &viewport::ViewportWidget::cameraNavigated, this, &ViewportPresenter::onCameraNavigated);

    rebuild();  // covers geometry/selection/context/section planes/materials that existed before this presenter registered
    rebuildGuides();  // covers guides that existed before this presenter registered
    rebuildAxes();  // covers an axes frame that existed before this presenter registered
    rebuildAnnotations();  // covers annotations that existed before this presenter registered
    pullCamera();  // covers a CameraStore state that existed before this presenter registered
    pullStyle();   // covers a StyleStore state that existed before this presenter registered
    pullShadows(); // covers a ShadowStore state that existed before this presenter registered
    pullFog();     // covers a FogStore state that existed before this presenter registered
}

void ViewportPresenter::onGeometryChanged(const events::GeometryChanged& /*event*/) {
    // Any real mutation invalidates an in-progress move-ghost preview.
    moveGhostVertexIds_.clear();
    // A real mutation likewise invalidates an in-progress extrusion ghost
    // (the commit's own GeometryChanged arrives alongside PushPullTool's
    // reset() clear, same self-cleaning shape as the move ghost above).
    extrudeGhostFaceId_ = geo::kInvalidId;
    renderLog(QStringLiteral("geometryChanged: ghosts cleared"));
    rebuild();  // rebuild()'s own tail already re-runs rebuildSelection()
}

void ViewportPresenter::onMoveGhostUpdated(const events::MoveGhostUpdated& event) {
    // Agents (vertexIds, delta), then re-runs the same root-geometry
    // rebuild GeometryChanged triggers -- appendModel substitutes
    // pos + moveGhostDelta_ for every id in moveGhostVertexIds_.
    moveGhostVertexIds_.clear();
    moveGhostVertexIds_.insert(event.vertexIds.begin(), event.vertexIds.end());
    moveGhostDelta_ = event.delta;
    rebuild();
}

void ViewportPresenter::onExtrudeGhostUpdated(const events::ExtrudeGhostUpdated& event) {
    // Same agent-then-full-rebuild shape as onMoveGhostUpdated above --
    // appendExtrudeGhost (called from rebuild) synthesizes the prism
    // triangles when extrudeGhostFaceId_ is live.
    extrudeGhostFaceId_ = event.faceId;
    extrudeGhostDistance_ = event.distance;
    renderLog(QStringLiteral("extrudeGhost: face=%1 dist=%2")
                  .arg(event.faceId)
                  .arg(event.distance));
    rebuild();
}

void ViewportPresenter::onSelectionChanged(const events::SelectionChanged& /*event*/) {
    rebuildSelection();
}

void ViewportPresenter::onEditContextChanged(const events::EditContextChanged& /*event*/) {
    // The editing context changed -- a full rebuild is needed (not just
    // rebuildSelection()) since which geometry lands in the
    // normal vs. dimmed buffers depends on the current context path.
    rebuild();
}

void ViewportPresenter::onGuidesChanged(const events::GuidesChanged& /*event*/) {
    rebuildGuides();
}

void ViewportPresenter::onAxesChanged(const events::AxesChanged& /*event*/) {
    rebuildAxes();
}

void ViewportPresenter::onAnnotationsChanged(const events::AnnotationsChanged& /*event*/) {
    rebuildAnnotations();
}

void ViewportPresenter::onSectionsChanged(const events::SectionsChanged& /*event*/) {
    rebuildSections();
}

void ViewportPresenter::onMaterialsChanged(const events::MaterialsChanged& /*event*/) {
    // Any material create/edit/paint can change a face's resolved color,
    // so this needs the same full rebuild() as the geometry/edit-context paths.
    rebuild();
}

void ViewportPresenter::onStyleChanged(const events::StyleChanged& /*event*/) {
    pullStyle();
}

void ViewportPresenter::onShadowsChanged(const events::ShadowsChanged& /*event*/) {
    pullShadows();
}

void ViewportPresenter::onFogChanged(const events::FogChanged& /*event*/) {
    pullFog();
}

void ViewportPresenter::appendModel(const geo::Model& model, const geo::Transform& xf, bool applyVisibility,
                                     const agent::GeometryApi* geometryStore, const agent::TagStore* tagStore,
                                     const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                                     std::vector<float>& edgeVerts, std::vector<float>& faceTris,
                                     std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                                     std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const {
    // Ghosts a root-model vertex position: while a non-copy MoveTool drag is
    // in progress, positions in moveGhostVertexIds_ render translated by
    // moveGhostDelta_ instead of their committed value (a partially-attached
    // face/edge stretches, which is correct). Gated on applyVisibility, true
    // only for the one appendModel call that reads scene.root().model --
    // moveEntity is root-only, so nested Definitions are never ghosted.
    const auto ghostPos = [this, applyVisibility](geo::Id vertexId, const geo::Vec3& pos) {
        if (applyVisibility && moveGhostVertexIds_.count(vertexId)) return pos + moveGhostDelta_;
        return pos;
    };

    edgeVerts.reserve(edgeVerts.size() + model.edges().size() * 6);
    for (const auto& [id, edge] : model.edges()) {
        if (applyVisibility) {
            const events::EntityRef ref{geo::EntityKind::Edge, id};
            if (geometryStore->isHidden(ref)) continue;                        // hidden edges don't render
            if (tagStore && !tagStore->isEntityVisible(ref)) continue;         // tag-invisible edges don't render
        }
        const geo::HalfEdge* he0 = model.halfEdge(edge.halfEdges[0]);
        const geo::HalfEdge* he1 = model.halfEdge(edge.halfEdges[1]);
        if (!he0 || !he1) continue;
        const geo::Vertex* v0 = model.vertex(he0->origin);
        const geo::Vertex* v1 = model.vertex(he1->origin);
        if (!v0 || !v1) continue;
        const geo::Vec3 p0 = xf.apply(ghostPos(he0->origin, v0->pos));
        const geo::Vec3 p1 = xf.apply(ghostPos(he1->origin, v1->pos));
        appendVertex(edgeVerts, p0);
        appendVertex(edgeVerts, p1);

        // Each half-edge's own face (kInvalidId -> nullopt, the wire side)
        // gives this edge's two candidate adjacent-face normals, transformed
        // to world space (direction only) since classifyEdge compares against a world-space eye.
        if (edgeAdjacency) {
            std::optional<geo::Vec3> normalA;
            std::optional<geo::Vec3> normalB;
            if (he0->face != geo::kInvalidId) {
                if (const geo::Face* faceA = model.face(he0->face)) normalA = xf.applyVector(faceA->normal);
            }
            if (he1->face != geo::kInvalidId) {
                if (const geo::Face* faceB = model.face(he1->face)) normalB = xf.applyVector(faceB->normal);
            }
            edgeAdjacency->push_back(viewport::EdgeAdjacency{p0, p1, normalA, normalB});
        }
    }

    for (const auto& [id, face] : model.faces()) {
        if (applyVisibility) {
            const events::EntityRef ref{geo::EntityKind::Face, id};
            if (geometryStore->isHidden(ref)) continue;                        // hidden faces don't render
            if (tagStore && !tagStore->isEntityVisible(ref)) continue;         // tag-invisible faces don't render
        }
        const std::vector<geo::Id> tris = geo::triangulate(model, id);
        // The material-batched normal buffer (faceMaterialKeys non-null) is
        // 8 floats/vertex (pos+uv+normal); the dimmed overlay stays 3 (position-only).
        faceTris.reserve(faceTris.size() + tris.size() * (faceMaterialKeys ? 8 : 3));

        // This face's resolved material pair, computed once per face;
        // skipped when faceMaterialKeys is null.
        geo::Id resolvedFront = 0;
        geo::Id resolvedBack = 0;
        // This face's UV basis + tile size, resolved once per face from the
        // resolved FRONT-slot material, only when faceMaterialKeys is
        // non-null and that material is textured. Otherwise uvBasis/textured
        // stay default, so every vertex gets {0,0}.
        viewport::FaceUvBasis uvBasis{};
        double tileW = 1.0;
        double tileH = 1.0;
        bool textured = false;
        // This face's own per-face UV transform (front slot only; never
        // inherited -- Position Texture only ever targets a Face's own
        // slot), applied only when non-identity. hasUvTransform stays false otherwise (cheap common-case path).
        bool hasUvTransform = false;
        viewport::UvTransform uvTransform;
        // This face's world-space unit normal (direction-only transform,
        // then normalized -- a degenerate input safely yields the zero
        // vector). Computed once per face, flat per triangle (exact, not
        // approximate, since every Face in this kernel is planar). Left at
        // zero when faceMaterialKeys is null (dimmed overlay, never shaded).
        geo::Vec3 worldNormal{};
        if (faceMaterialKeys) {
            const agent::MaterialAssignment* own =
                materialStore ? materialStore->assignment(events::EntityRef{geo::EntityKind::Face, id}) : nullptr;
            const geo::Id ownFront = own ? own->frontMaterialId : geo::Id{0};
            const geo::Id ownBack = own ? own->backMaterialId : geo::Id{0};
            const viewport::ResolvedFaceMaterial resolved =
                viewport::resolveFaceMaterial(ownFront, ownBack, inheritedMaterialId);
            resolvedFront = resolved.frontMaterialId;
            resolvedBack = resolved.backMaterialId;

            worldNormal = geo::normalized(xf.applyVector(face.normal));

            const agent::Material* frontMaterial =
                materialStore && resolvedFront != 0 ? materialStore->material(resolvedFront) : nullptr;
            textured = frontMaterial && !frontMaterial->assetHash.empty();
            if (textured) {
                // worldNormal above is this exact same vector (already
                // computed, no need to re-derive it from
                // xf.applyVector(face.normal) a second time).
                uvBasis = viewport::faceUvBasis(worldNormal);
                tileW = frontMaterial->tileW;
                tileH = frontMaterial->tileH;
                if (own && !events::isIdentityUvTransform(own->uvTransform)) {
                    hasUvTransform = true;
                    uvTransform = viewport::UvTransform{own->uvTransform.offsetU, own->uvTransform.offsetV,
                                                         own->uvTransform.rotationRad, own->uvTransform.scaleU,
                                                         own->uvTransform.scaleV};
                }
            }
        }

        // Triangle-at-a-time so a missing vertex (defensive, shouldn't
        // happen) skips the whole triangle -- faceMaterialKeys' entries
        // must stay 1:1 with faceTris' per-triangle groups for rebuild()'s bucketing to slice correctly.
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const geo::Vertex* v0 = model.vertex(tris[i]);
            const geo::Vertex* v1 = model.vertex(tris[i + 1]);
            const geo::Vertex* v2 = model.vertex(tris[i + 2]);
            if (!v0 || !v1 || !v2) continue;
            const geo::Vec3 p0 = xf.apply(ghostPos(tris[i], v0->pos));
            const geo::Vec3 p1 = xf.apply(ghostPos(tris[i + 1], v1->pos));
            const geo::Vec3 p2 = xf.apply(ghostPos(tris[i + 2], v2->pos));
            if (faceMaterialKeys) {
                // p0/p1/p2 already reflect ghostPos's substitution; UV
                // generation and triangleCentroid just read the
                // (possibly-ghosted) position. hasUvTransform gates the
                // extra applyUvTransform call (cheap no-op path otherwise).
                const viewport::Uv uv0 = textured ? viewport::faceVertexUv(uvBasis, p0, tileW, tileH) : viewport::Uv{};
                const viewport::Uv uv1 = textured ? viewport::faceVertexUv(uvBasis, p1, tileW, tileH) : viewport::Uv{};
                const viewport::Uv uv2 = textured ? viewport::faceVertexUv(uvBasis, p2, tileW, tileH) : viewport::Uv{};
                // worldNormal is the SAME value for all three vertices of
                // this triangle (flat shading, see appendVertexUvNormal's
                // own comment).
                appendVertexUvNormal(faceTris, p0, hasUvTransform ? viewport::applyUvTransform(uv0, uvTransform) : uv0,
                                      worldNormal);
                appendVertexUvNormal(faceTris, p1, hasUvTransform ? viewport::applyUvTransform(uv1, uvTransform) : uv1,
                                      worldNormal);
                appendVertexUvNormal(faceTris, p2, hasUvTransform ? viewport::applyUvTransform(uv2, uvTransform) : uv2,
                                      worldNormal);
                faceMaterialKeys->push_back({resolvedFront, resolvedBack, viewport::triangleCentroid(p0, p1, p2)});
            } else {
                appendVertex(faceTris, p0);
                appendVertex(faceTris, p1);
                appendVertex(faceTris, p2);
            }
        }
    }
}

void ViewportPresenter::appendExtrudeGhost(const geo::Model& model, std::vector<float>& faceTris,
                                            std::vector<viewport::TriangleMaterialKey>& faceMaterialKeys) const {
    const geo::Face* face = model.face(extrudeGhostFaceId_);
    if (face == nullptr || std::fabs(extrudeGhostDistance_) < geo::kEps) {
        return;
    }
    const std::vector<geo::Id> loop = model.faceVertexLoop(extrudeGhostFaceId_);
    const std::size_t n = loop.size();
    if (n < 3) {
        return;
    }
    const geo::Vec3 normal = face->normal;
    const double dirSign = (extrudeGhostDistance_ > 0.0) ? 1.0 : -1.0;
    const geo::Vec3 offset = normal * extrudeGhostDistance_;

    std::vector<geo::Vec3> base(n);
    for (std::size_t i = 0; i < n; ++i) {
        const geo::Vertex* v = model.vertex(loop[i]);
        if (!v) return;  // defensive: mid-mutation inconsistency -- skip the whole ghost
        base[i] = v->pos;
    }

    const auto emitTri = [&faceTris, &faceMaterialKeys](const geo::Vec3& p0, const geo::Vec3& p1,
                                                          const geo::Vec3& p2, const geo::Vec3& nrm) {
        appendVertexUvNormal(faceTris, p0, viewport::Uv{}, nrm);
        appendVertexUvNormal(faceTris, p1, viewport::Uv{}, nrm);
        appendVertexUvNormal(faceTris, p2, viewport::Uv{}, nrm);
        faceMaterialKeys.push_back({geo::Id{0}, geo::Id{0}, viewport::triangleCentroid(p0, p1, p2)});
    };

    // Side quads: {b0,b1,t1,t0} is the positive-case outward winding; its
    // geometric normal times dirSign stays outward for a negative drag, and
    // the triangle order flips alongside (Model::extrudeFace's own rule).
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const geo::Vec3& b0 = base[i];
        const geo::Vec3& b1 = base[j];
        const geo::Vec3 t0 = b0 + offset;
        const geo::Vec3 t1 = b1 + offset;
        const geo::Vec3 sideNormal = geo::normalized(geo::cross(b1 - b0, t0 - b0)) * dirSign;
        if (dirSign > 0.0) {
            emitTri(b0, b1, t1, sideNormal);
            emitTri(b0, t1, t0, sideNormal);
        } else {
            emitTri(t0, t1, b1, sideNormal);
            emitTri(t0, b1, b0, sideNormal);
        }
    }

    // Cap: the base face's triangulation, offset to the far plane; its
    // winding matches `normal` as-is for a positive pull, swapped for a negative push.
    const std::vector<geo::Id> tris = geo::triangulate(model, extrudeGhostFaceId_);
    const geo::Vec3 capNormal = normal * dirSign;
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
        const geo::Vertex* v0 = model.vertex(tris[i]);
        const geo::Vertex* v1 = model.vertex(tris[i + 1]);
        const geo::Vertex* v2 = model.vertex(tris[i + 2]);
        if (!v0 || !v1 || !v2) continue;
        const geo::Vec3 p0 = v0->pos + offset;
        const geo::Vec3 p1 = v1->pos + offset;
        const geo::Vec3 p2 = v2->pos + offset;
        if (dirSign > 0.0) {
            emitTri(p0, p1, p2, capNormal);
        } else {
            emitTri(p0, p2, p1, capNormal);
        }
    }
}

void ViewportPresenter::appendSubtreeInto(const geo::Scene& scene, const geo::Definition& def,
                                           const geo::Transform& xf, const agent::MaterialRepository* materialStore,
                                           geo::Id inheritedMaterialId, std::vector<float>& edgeOut,
                                           std::vector<float>& faceOut,
                                           std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                                           std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const {
    appendModel(def.model, xf, /*applyVisibility=*/false, nullptr, nullptr, materialStore, inheritedMaterialId,
                edgeOut, faceOut, faceMaterialKeys, edgeAdjacency);
    for (const geo::Instance& inst : def.children) {
        const geo::Definition* childDef = scene.definition(inst.definitionId);
        if (!childDef) continue;  // defensive: shouldn't happen -- Scene keeps this id valid
        const geo::Id childInherited =
            viewport::nextInheritedMaterialId(inheritedMaterialId, instanceOwnFrontMaterial(materialStore, inst.id));
        appendSubtreeInto(scene, *childDef, xf.composed(inst.transform), materialStore, childInherited, edgeOut,
                           faceOut, faceMaterialKeys, edgeAdjacency);
    }
}

void ViewportPresenter::appendRouted(const geo::Scene& scene, const geo::Definition& def, const geo::Transform& xf,
                                      bool isRoot, const agent::GeometryApi* geometryStore,
                                      const agent::TagStore* tagStore, const agent::MaterialRepository* materialStore,
                                      geo::Id inheritedMaterialId, const std::vector<geo::Id>& remainingPath,
                                      std::vector<float>& normalEdges, std::vector<float>& normalFaces,
                                      std::vector<viewport::TriangleMaterialKey>& normalFaceMaterialKeys,
                                      std::vector<viewport::EdgeAdjacency>& normalEdgeAdjacency,
                                      std::vector<float>& dimmedEdges, std::vector<float>& dimmedFaces) const {
    const bool inContext = remainingPath.empty();
    std::vector<float>& edgeOut = inContext ? normalEdges : dimmedEdges;
    std::vector<float>& faceOut = inContext ? normalFaces : dimmedFaces;
    // Material batching only matters for the normal buffers -- the dimmed
    // overlay stays flat single-color, so faceMaterialKeys is null whenever
    // this call routes to dimmedFaces.
    std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys = inContext ? &normalFaceMaterialKeys : nullptr;
    // Edge-style classification follows the SAME in-context-only rule.
    std::vector<viewport::EdgeAdjacency>* edgeAdjacency = inContext ? &normalEdgeAdjacency : nullptr;

    appendModel(def.model, xf, /*applyVisibility=*/isRoot, geometryStore, tagStore, materialStore,
                inheritedMaterialId, edgeOut, faceOut, faceMaterialKeys, edgeAdjacency);

    const geo::Id nextId = inContext ? geo::kInvalidId : remainingPath.front();
    for (const geo::Instance& inst : def.children) {
        const geo::Definition* childDef = scene.definition(inst.definitionId);
        if (!childDef) continue;  // defensive: shouldn't happen -- Scene keeps this id valid

        // A root-child Instance is itself an EntityRef; hidden/tag-invisible
        // skips it entirely. Only applies at this top level -- a nested
        // instance has no independent hidden/tag identity.
        if (isRoot) {
            const events::EntityRef instRef{geo::EntityKind::Instance, inst.id};
            if (geometryStore->isHidden(instRef)) continue;
            if (tagStore && !tagStore->isEntityVisible(instRef)) continue;
        }

        const geo::Transform composed = xf.composed(inst.transform);
        // Threaded through every branch, including the off-context one -- a
        // routing instance can still have descendants that render normally.
        const geo::Id childInherited =
            viewport::nextInheritedMaterialId(inheritedMaterialId, instanceOwnFrontMaterial(materialStore, inst.id));
        if (!inContext && inst.id == nextId) {
            // The next hop on the current editing context's chain -- keep
            // routing (the tail may still branch further, or land fully in
            // context once remainingPath is exhausted).
            const std::vector<geo::Id> rest(remainingPath.begin() + 1, remainingPath.end());
            appendRouted(scene, *childDef, composed, /*isRoot=*/false, nullptr, nullptr, materialStore,
                         childInherited, rest, normalEdges, normalFaces, normalFaceMaterialKeys, normalEdgeAdjacency,
                         dimmedEdges, dimmedFaces);
        } else {
            // Off the context chain entirely (or already fully IN context,
            // in which case every descendant lands in the same bucket this
            // whole call is using) -- no further per-child routing needed.
            appendSubtreeInto(scene, *childDef, composed, materialStore, childInherited, edgeOut, faceOut,
                               faceMaterialKeys, edgeAdjacency);
        }
    }
}

void ViewportPresenter::rebuild() {
    auto agent = kernel().agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) {
        viewport_->setModelGeometry({}, {}, {}, {}, {});
        viewport_->setDimmedGeometry({}, {});
        // No model: clear the cache too, so a stale adjacency vector can't
        // outlive the geometry it was derived from.
        cachedEdgeAdjacency_.clear();
        viewport_->setEdgeStyleGeometry({}, {});
        return;
    }
    // TagStore may be absent from this kernel -- tag visibility is simply
    // not filtered in that case (mirroring the other visibility-
    // composition sites in tool_controller.cpp/selection_commands.cpp).
    auto tagStore = kernel().agentAs<agent::TagStore>(agent::kTagStoreName);
    // EditContextStore may be absent too -- treated as "always at root".
    auto editContextStore = kernel().agentAs<agent::EditContextStore>(agent::kEditContextStoreName);
    const std::vector<geo::Id> contextPath = editContextStore ? editContextStore->path() : std::vector<geo::Id>{};
    // MaterialRepository may be absent too -- every face then resolves to
    // material id 0 (today's kFaceFrontColor/kFaceBackColor look), same
    // "optional Agent" pattern as tagStore/editContextStore above.
    auto materialStore = kernel().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
    // AssetRepository may be absent too -- addSwatch just leaves textureBytes
    // empty then; the widget's textureFor() falls back to the flat look with no bytes to decode.
    auto assetStore = kernel().agentAs<agent::AssetRepository>(agent::kAssetRepositoryName);

    std::vector<float> normalEdgeVerts;
    std::vector<float> normalFaceTrisWalkOrder;
    std::vector<float> dimmedEdgeVerts;
    std::vector<float> dimmedFaceTris;
    std::vector<viewport::TriangleMaterialKey> faceMaterialKeys;
    // Collected in lock-step with normalEdgeVerts (see appendModel's own
    // comment) -- moved into cachedEdgeAdjacency_ below once the walk
    // finishes.
    std::vector<viewport::EdgeAdjacency> normalEdgeAdjacency;

    const geo::Scene& scene = agent->scene();
    // Walks the whole scene (composing each instance's transform onto its
    // parent's), routing the branch matching contextPath into the normal
    // buffers and everything else into dimmed. At root context, everything
    // lands in normal and dimmed stays empty.
    appendRouted(scene, scene.root(), geo::Transform::identity(), /*isRoot=*/true, agent.get(), tagStore.get(),
                 materialStore.get(), /*inheritedMaterialId=*/geo::Id{0}, contextPath, normalEdgeVerts,
                 normalFaceTrisWalkOrder, faceMaterialKeys, normalEdgeAdjacency, dimmedEdgeVerts, dimmedFaceTris);

    // The live PushPull prism rides on top of the ordinary walk (root
    // model only -- PushPull's own mutation scope) -- see
    // appendExtrudeGhost's own comment.
    if (extrudeGhostFaceId_ != geo::kInvalidId) {
        appendExtrudeGhost(agent->model(), normalFaceTrisWalkOrder, faceMaterialKeys);
    }

    // Buckets the walk's per-triangle material keys into contiguous
    // per-material draw ranges, then permutes the raw per-triangle vertex
    // data into that same order so each range's {first,count} slices the
    // face VBO directly. first/count stay vertex-count units;
    // kFloatsPerTriangle is 8 floats/vertex * 3.
    const viewport::BucketResult bucketed = viewport::bucketTrianglesByMaterial(faceMaterialKeys);
    constexpr std::size_t kFloatsPerTriangle = 24;
    std::vector<float> normalFaceTris(normalFaceTrisWalkOrder.size());
    for (std::size_t outPos = 0; outPos < bucketed.order.size(); ++outPos) {
        const std::size_t srcTri = bucketed.order[outPos];
        std::copy_n(normalFaceTrisWalkOrder.begin() + static_cast<std::ptrdiff_t>(srcTri * kFloatsPerTriangle),
                    kFloatsPerTriangle, normalFaceTris.begin() + static_cast<std::ptrdiff_t>(outPos * kFloatsPerTriangle));
    }

    // Splits opaque/transparent by each range's front-material opacity;
    // an unresolvable id reads as fully opaque.
    const agent::MaterialRepository* materialStorePtr = materialStore.get();
    const auto materialOpacity = [materialStorePtr](geo::Id id) -> double {
        if (id == 0 || !materialStorePtr) return 1.0;
        const agent::Material* m = materialStorePtr->material(id);
        return m ? m->opacity : 1.0;
    };
    const viewport::PartitionedRanges partitioned = viewport::partitionRangesByOpacity(bucketed.ranges, materialOpacity);

    // Resolved-color table for every referenced material id -- ViewportWidget
    // stays Agent-ignorant, so it gets plain color/texture data only.
    std::unordered_map<geo::Id, viewport::ViewportWidget::MaterialSwatch> materialSwatches;
    const agent::AssetRepository* assetStorePtr = assetStore.get();
    const auto addSwatch = [&](geo::Id id) {
        if (id == 0 || !materialStorePtr || materialSwatches.count(id)) return;
        const agent::Material* m = materialStorePtr->material(id);
        if (!m) return;
        viewport::ViewportWidget::MaterialSwatch swatch;
        swatch.rgb = QVector3D(static_cast<float>(m->r), static_cast<float>(m->g), static_cast<float>(m->b));
        swatch.opacity = static_cast<float>(m->opacity);
        // Hands the widget texture bytes directly rather than a hash it
        // would need AssetRepository to resolve. A dangling assetHash or missing
        // AssetRepository leaves textureBytes empty; the widget falls back to the flat look.
        if (!m->assetHash.empty()) {
            swatch.assetHash = m->assetHash;
            const agent::Asset* asset = assetStorePtr ? assetStorePtr->get(m->assetHash) : nullptr;
            if (asset) {
                swatch.textureBytes = QByteArray(asset->bytes.data(), static_cast<qsizetype>(asset->bytes.size()));
            }
        }
        materialSwatches[id] = std::move(swatch);
    };
    for (const viewport::MaterialRange& range : partitioned.opaque) {
        addSwatch(range.frontMaterialId);
        addSwatch(range.backMaterialId);
    }
    for (const viewport::MaterialRange& range : partitioned.transparent) {
        addSwatch(range.frontMaterialId);
        addSwatch(range.backMaterialId);
    }

    // One line per rebuild -- enough to see the ghost lifecycle and buffer
    // sizes the window was actually asked to draw (24 floats per triangle).
    renderLog(QStringLiteral("rebuild: tris=%1 edgeFloats=%2 ghostFace=%3 ghostDist=%4 moveGhostVerts=%5")
                  .arg(normalFaceTris.size() / 24)
                  .arg(normalEdgeVerts.size())
                  .arg(extrudeGhostFaceId_)
                  .arg(extrudeGhostDistance_)
                  .arg(moveGhostVertexIds_.size()));

    viewport_->setModelGeometry(std::move(normalEdgeVerts), std::move(normalFaceTris), partitioned.opaque,
                                 partitioned.transparent, std::move(materialSwatches));
    viewport_->setDimmedGeometry(std::move(dimmedEdgeVerts), std::move(dimmedFaceTris));

    // Caches the walk's per-edge adjacency for recomputeEdgeStyle()'s
    // lighter path, then runs it now against the eye position current at this rebuild.
    cachedEdgeAdjacency_ = std::move(normalEdgeAdjacency);
    recomputeEdgeStyle();

    rebuildSelection();
    // A section plane's own rectangle is sized against the model's bbox
    // (see rebuildSections' own comment), so a geometry edit needs to
    // re-derive that sizing too, not just a SectionsChanged event.
    rebuildSections();
}

void ViewportPresenter::rebuildSelection() {
    auto geometryStore = kernel().agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    auto selectionStore = kernel().agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
    if (!geometryStore || !selectionStore) {
        viewport_->setSelectionGeometry({}, {}, {});
        return;
    }

    const geo::Model& model = geometryStore->model();

    // The selection overlay follows an in-progress MoveTool ghost --
    // otherwise the highlight stays at pre-drag positions while the model
    // renders displaced. Root-model only; the Instance branch below can't
    // be targeted by a root-only move.
    const auto ghostPos = [this](geo::Id vertexId, const geo::Vec3& pos) {
        if (moveGhostVertexIds_.count(vertexId)) return pos + moveGhostDelta_;
        return pos;
    };

    std::vector<float> edgeVerts;
    std::vector<float> faceTris;
    std::vector<float> pointVerts;

    for (const events::EntityRef& ref : selectionStore->items()) {
        // Defensive: SetHiddenCommand subtracts hidden refs from the
        // selection on hide, so this shouldn't normally see one, but skip it
        // anyway rather than trust that invariant here too.
        if (geometryStore->isHidden(ref)) continue;
        switch (ref.kind) {
            case geo::EntityKind::Vertex: {
                const geo::Vertex* v = model.vertex(ref.id);
                if (v) appendVertex(pointVerts, ghostPos(ref.id, v->pos));
                break;
            }
            case geo::EntityKind::Edge: {
                const geo::Edge* edge = model.edge(ref.id);
                if (!edge) break;
                const geo::HalfEdge* he0 = model.halfEdge(edge->halfEdges[0]);
                const geo::HalfEdge* he1 = model.halfEdge(edge->halfEdges[1]);
                if (!he0 || !he1) break;
                const geo::Vertex* v0 = model.vertex(he0->origin);
                const geo::Vertex* v1 = model.vertex(he1->origin);
                if (!v0 || !v1) break;
                appendVertex(edgeVerts, ghostPos(he0->origin, v0->pos));
                appendVertex(edgeVerts, ghostPos(he1->origin, v1->pos));
                break;
            }
            case geo::EntityKind::Face: {
                const std::vector<geo::Id> tris = geo::triangulate(model, ref.id);
                faceTris.reserve(faceTris.size() + tris.size() * 3);
                for (geo::Id vertexId : tris) {
                    const geo::Vertex* v = model.vertex(vertexId);
                    if (!v) continue;
                    appendVertex(faceTris, ghostPos(vertexId, v->pos));
                }
                break;
            }
            case geo::EntityKind::Instance: {
                // Renders a selected Instance as its Definition's edges,
                // recursively through nested groups/components, in world
                // space. Edges only -- the selection-blue wireframe already
                // conveys selection without re-triangulating faces.
                const geo::Scene& scene = geometryStore->scene();
                const geo::Instance* inst = scene.findInstance(geo::kRootDefinitionId, ref.id);
                if (!inst) break;
                const geo::Definition* def = scene.definition(inst->definitionId);
                if (!def) break;

                const std::function<void(const geo::Definition&, const geo::Transform&)> appendEdges =
                    [&](const geo::Definition& d, const geo::Transform& xf) {
                        for (const auto& [edgeId, edge] : d.model.edges()) {
                            (void)edgeId;
                            const geo::HalfEdge* he0 = d.model.halfEdge(edge.halfEdges[0]);
                            const geo::HalfEdge* he1 = d.model.halfEdge(edge.halfEdges[1]);
                            if (!he0 || !he1) continue;
                            const geo::Vertex* v0 = d.model.vertex(he0->origin);
                            const geo::Vertex* v1 = d.model.vertex(he1->origin);
                            if (!v0 || !v1) continue;
                            appendVertex(edgeVerts, xf.apply(v0->pos));
                            appendVertex(edgeVerts, xf.apply(v1->pos));
                        }
                        for (const geo::Instance& child : d.children) {
                            const geo::Definition* childDef = scene.definition(child.definitionId);
                            if (!childDef) continue;
                            appendEdges(*childDef, xf.composed(child.transform));
                        }
                    };
                appendEdges(*def, inst->transform);
                break;
            }
        }
    }

    viewport_->setSelectionGeometry(std::move(edgeVerts), std::move(faceTris), std::move(pointVerts));
}

void ViewportPresenter::rebuildGuides() {
    auto agent = kernel().agentAs<agent::GuideStore>(agent::kGuideStoreName);
    if (!agent) {
        viewport_->setGuideGeometry({});
        return;
    }

    // lineView()/pointView() are already visible-only, so no extra hidden-check is needed here.
    std::vector<float> lineVerts;
    for (const geo::GuideLineData& line : agent->lineView()) {
        const geo::Vec3 from = line.point - line.dir * kGuideLineExtent;
        const geo::Vec3 to = line.point + line.dir * kGuideLineExtent;
        appendDashedSegment(lineVerts, from, to);
    }
    for (const geo::GuidePointData& pt : agent->pointView()) {
        appendGuidePointCross(lineVerts, pt.pos);
    }

    viewport_->setGuideGeometry(std::move(lineVerts));
}

void ViewportPresenter::rebuildAxes() {
    auto agent = kernel().agentAs<agent::AxesStore>(agent::kAxesStoreName);
    if (!agent) {
        // AxesStore not registered -- world default, matching the
        // originally-hardcoded grid/axes look exactly.
        viewport_->setAxesFrame(QVector3D(0.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f),
                                 QVector3D(0.0f, 0.0f, 1.0f));
        return;
    }
    const agent::Frame& f = agent->frame();
    viewport_->setAxesFrame(viewport::toQt(f.origin), viewport::toQt(f.xDir), viewport::toQt(f.yDir),
                             viewport::toQt(f.zDir));
}

void ViewportPresenter::onCameraChanged(const events::CameraChanged& /*event*/) {
    pullCamera();
    // See this method's own header comment -- defensive, cheap (no scene
    // re-walk).
    recomputeEdgeStyle();
}

void ViewportPresenter::onCameraMoved(const events::CameraMoved& /*event*/) {
    // See this method's own header comment for why this is the lighter
    // recompute path rather than a full rebuild().
    recomputeEdgeStyle();
}

void ViewportPresenter::onCameraNavigated(QVector3D target, float azimuthDeg, float elevationDeg, float distance,
                                           float fovYDeg) {
    kernel().send(events::CameraNavigated{viewport::toGeo(target), azimuthDeg, elevationDeg, distance, fovYDeg});
}

void ViewportPresenter::pullCamera() {
    auto agent = kernel().agentAs<agent::CameraStore>(agent::kCameraStoreName);
    if (!agent) return;  // No CameraStore registered -- leave the viewport's own hardcoded default in place.
    const agent::CameraState& state = agent->state();
    const viewport::Camera::Projection projection = toViewportProjection(state.projection);
    viewport_->setCameraState(viewport::toQt(state.target), static_cast<float>(state.azimuthDeg),
                               static_cast<float>(state.elevationDeg), static_cast<float>(state.distance),
                               static_cast<float>(state.fovYDeg), projection);

    // The Camera menu's checked state is the other half of this
    // presenter's CameraStore reflection. twoPointAction_ needs this
    // explicit push since TwoPoint can be entered/left from paths that
    // never click that QAction.
    if (perspectiveAction_) perspectiveAction_->setChecked(state.projection == events::Projection::Perspective);
    if (parallelProjectionAction_) parallelProjectionAction_->setChecked(state.projection == events::Projection::Parallel);
    if (twoPointAction_) twoPointAction_->setChecked(state.projection == events::Projection::TwoPoint);

    // Previous/Next's enabled state, refreshed on the same seam. Known v1
    // gap: an ordinary navigation gesture also pushes a history entry but
    // dispatches no event, so Previous/Next can stay stale by one gesture
    // until some other CameraChanged-firing action refreshes them.
    if (previousAction_) previousAction_->setEnabled(agent->canGoBack());
    if (nextAction_) nextAction_->setEnabled(agent->canGoForward());
}

void ViewportPresenter::pullStyle() {
    auto agent = kernel().agentAs<agent::StyleStore>(agent::kStyleStoreName);
    if (!agent) return;  // No StyleStore registered -- leave the viewport's own hardcoded default in place.
    const QVector3D front(static_cast<float>(agent->defaultFrontColor().r), static_cast<float>(agent->defaultFrontColor().g),
                           static_cast<float>(agent->defaultFrontColor().b));
    const QVector3D back(static_cast<float>(agent->defaultBackColor().r), static_cast<float>(agent->defaultBackColor().g),
                          static_cast<float>(agent->defaultBackColor().b));
    // profiles/depthCue/backEdges are pure draw-time widget state (read
    // fresh every paintGL frame), so no recomputeEdgeStyle() call is needed here.
    viewport_->setStyle(toViewportFaceStyle(agent->faceStyle()), agent->profiles(), agent->depthCue(),
                         agent->backEdges(), agent->ambientOcclusion(), static_cast<float>(agent->aoStrength()),
                         front, back);
}

void ViewportPresenter::pullShadows() {
    auto agent = kernel().agentAs<agent::ShadowStore>(agent::kShadowStoreName);
    if (!agent) return;  // No ShadowStore registered -- leave the viewport's own hardcoded default (shading/shadows off) in place.

    const agent::sun::SolarAngles angles =
        agent::sun::solarAngles(agent->latitudeDeg(), agent->month(), agent->day(), agent->hourLocal());
    const geo::Vec3 dir = agent::sun::sunDirection(angles);
    const QVector3D sunDir(static_cast<float>(dir.x), static_cast<float>(dir.y), static_cast<float>(dir.z));
    viewport_->setShadowState(agent->showShadows(), agent->useSunForShading(), sunDir,
                               static_cast<float>(agent->light()), static_cast<float>(agent->dark()));
}

void ViewportPresenter::pullFog() {
    auto agent = kernel().agentAs<agent::FogStore>(agent::kFogStoreName);
    if (!agent) return;  // No FogStore registered -- leave the viewport's own hardcoded default (fog off) in place.

    const QVector3D customColor(static_cast<float>(agent->colorR()), static_cast<float>(agent->colorG()),
                                 static_cast<float>(agent->colorB()));
    viewport_->setFog(agent->enabled(), static_cast<float>(agent->startDistance()),
                       static_cast<float>(agent->endDistance()), agent->useBackgroundColor(), customColor);
}

void ViewportPresenter::recomputeEdgeStyle() {
    // This app's own pinned "widths 3/2/1" choice; classifyAndBucketEdges itself stays generic over bandCount.
    constexpr int kEdgeDepthBandCount = 3;

    // viewport_->camera() is the widget's own live Qt camera, already
    // reflecting whatever CameraStore/CameraSyncCommand just settled on by
    // the time this handler runs.
    const geo::Vec3 eye = viewport::toGeo(viewport_->camera().eye());
    viewport::BandedEdgeBuckets buckets =
        viewport::classifyAndBucketEdges(cachedEdgeAdjacency_, eye, kEdgeDepthBandCount);

    std::array<std::vector<float>, 3> bandVerts;
    for (int i = 0; i < kEdgeDepthBandCount; ++i) {
        bandVerts[static_cast<std::size_t>(i)] = std::move(buckets.bandVerts[static_cast<std::size_t>(i)]);
    }
    viewport_->setEdgeStyleGeometry(std::move(buckets.profileVerts), std::move(bandVerts));
}

void ViewportPresenter::rebuildAnnotations() {
    auto agent = kernel().agentAs<agent::AnnotationStore>(agent::kAnnotationStoreName);
    if (!agent) {
        viewport_->setAnnotationGeometry({}, {}, {});
        return;
    }

    std::vector<float> normalVerts;
    std::vector<float> invalidVerts;
    std::vector<viewport::ViewportWidget::AnnotationLabel> labels;

    for (const agent::Dimension& dim : agent->dimensions()) {
        // lastA/lastB are Dimension's own cached endpoints (kept current by
        // refreshAssociations on every GeometryChanged), so no GeometryApi access is needed here.
        std::vector<float>& out = dim.associated ? normalVerts : invalidVerts;

        const geo::Vec3 dimA = dim.lastA + dim.offsetDir * dim.offset;
        const geo::Vec3 dimB = dim.lastB + dim.offsetDir * dim.offset;
        const geo::Vec3 abDir = geo::normalized(dim.lastB - dim.lastA);
        // abDir and offsetDir are always mutually perpendicular unit
        // vectors by construction, so their sum is never degenerate.
        const geo::Vec3 tick = geo::normalized(abDir + dim.offsetDir) * kDimensionTickHalfLen;

        appendSegment(out, dim.lastA, dimA);
        appendSegment(out, dim.lastB, dimB);
        appendSegment(out, dimA, dimB);
        appendSegment(out, dimA - tick, dimA + tick);
        appendSegment(out, dimB - tick, dimB + tick);

        viewport::ViewportWidget::AnnotationLabel label;
        label.screenFixed = false;
        label.worldPos = viewport::toQt((dimA + dimB) * 0.5);
        label.text = QString::fromStdString(dim.overrideText.empty() ? formatDistance(geo::distance(dim.lastA, dim.lastB))
                                                                      : dim.overrideText);
        label.leader = false;
        labels.push_back(std::move(label));
    }

    for (const agent::TextNote& note : agent->texts()) {
        viewport::ViewportWidget::AnnotationLabel label;
        label.screenFixed = note.screenFixed;
        if (note.screenFixed) {
            label.screenPos = QPointF(note.screenX, note.screenY);
        } else {
            label.worldPos = viewport::toQt(note.worldAnchor);
            label.leader = true;
        }
        label.text = QString::fromStdString(note.text);
        labels.push_back(std::move(label));
    }

    viewport_->setAnnotationGeometry(std::move(normalVerts), std::move(invalidVerts), std::move(labels));
}

void ViewportPresenter::rebuildSections() {
    auto agent = kernel().agentAs<agent::SectionStore>(agent::kSectionStoreName);
    if (!agent) {
        viewport_->setSectionGeometry({}, {});
        viewport_->setClipPlane(std::nullopt);
        return;
    }

    auto geometryStore = kernel().agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    const geo::Model* model = geometryStore ? &geometryStore->model() : nullptr;
    const SectionBbox bbox = sectionModelBbox(model);
    const double half = sectionPlaneHalfExtent(bbox);

    std::vector<float> activeVerts;
    std::vector<float> inactiveVerts;
    for (const agent::SectionPlane& plane : agent->planes()) {
        if (plane.hidden) continue;  // a hidden plane's own widget doesn't render (see SectionStore::setHidden)
        const auto [u, v] = sectionPlaneBasis(plane.normal);
        std::vector<float>& out = plane.active ? activeVerts : inactiveVerts;
        appendSectionPlaneRectangle(out, plane.point, u, v, half);
    }
    viewport_->setSectionGeometry(std::move(activeVerts), std::move(inactiveVerts));

    const agent::SectionPlane* active = agent->activePlane();
    if (active) {
        viewport_->setClipPlane(viewport::ViewportWidget::ClipPlane{viewport::toQt(active->normal), viewport::toQt(active->point)});
    } else {
        viewport_->setClipPlane(std::nullopt);
    }
}

}  // namespace plnr::ui
