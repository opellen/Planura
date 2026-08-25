#pragma once

#include <unordered_set>
#include <vector>

#include <QVector3D>

#include <ordo/core/app_kernel.h>
#include <ordo/qt/presenter.h>

#include <geo/scene.h>

#include "agent/events.h"
// Full include: EdgeAdjacency is stored by value in a member vector.
#include "viewport/edge_style.h"

class QAction;

namespace plnr::viewport {
class ViewportWidget;
struct TriangleMaterialKey;
}  // namespace plnr::viewport

namespace plnr::agent {
class AnnotationStore;
class AxesStore;
class GeometryApi;
class GuideStore;
class MaterialRepository;
class SectionStore;
class ShadowStore;
class StyleStore;
class TagStore;
}  // namespace plnr::agent

namespace plnr::ui {

// Mirrors GeometryApi/SelectionStore/GuideStore/AnnotationStore state onto
// the viewport's dynamic VBOs. Pure translator: reads agents, never
// mutates; ViewportWidget itself stays kernel-ignorant.
class ViewportPresenter : public ordo::qt::Presenter {
public:
    // Camera-menu QActions owned here; pullCamera() keeps their
    // checked/enabled state current. nullptr-safe.
    ViewportPresenter(ordo::core::AppKernel& kernel, viewport::ViewportWidget* viewport, QAction* perspectiveAction,
                       QAction* parallelProjectionAction, QAction* twoPointAction, QAction* previousAction,
                       QAction* nextAction);

    // Subscribes to the relevant *Changed events and does an initial
    // pull/rebuild so pre-registration state still shows. Connects
    // ViewportWidget::cameraNavigated so live navigation reaches CameraStore.
    void onRegister() override;

private:
    void onGeometryChanged(const events::GeometryChanged& event);
    // Agents the live move-ghost (vertexIds, delta) and re-runs rebuild().
    void onMoveGhostUpdated(const events::MoveGhostUpdated& event);
    // Agents the live extrusion ghost (faceId, distance) and re-runs rebuild().
    void onExtrudeGhostUpdated(const events::ExtrudeGhostUpdated& event);
    void onSelectionChanged(const events::SelectionChanged& event);
    void onEditContextChanged(const events::EditContextChanged& event);
    void onGuidesChanged(const events::GuidesChanged& event);
    void onAxesChanged(const events::AxesChanged& event);
    void onAnnotationsChanged(const events::AnnotationsChanged& event);
    void onSectionsChanged(const events::SectionsChanged& event);
    // Any material create/edit/paint can change a face's resolved color,
    // so this triggers a full rebuild().
    void onMaterialsChanged(const events::MaterialsChanged& event);
    // Re-pulls and pushes StyleStore's face style/colors (pullStyle()).
    void onStyleChanged(const events::StyleChanged& event);
    // Re-pulls and pushes ShadowStore's sun/shadow state (pullShadows());
    // pure draw-time state, no rebuild().
    void onShadowsChanged(const events::ShadowsChanged& event);
    // Re-pulls and pushes FogStore's fog state (pullFog()); pure draw-time
    // state, no rebuild().
    void onFogChanged(const events::FogChanged& event);
    // Re-pulls and applies the restored camera (pullCamera()) plus
    // recomputeEdgeStyle() -- defensive in case restoreCamera ever fires
    // without a following GeometryChanged.
    void onCameraChanged(const events::CameraChanged& event);
    // Re-classifies cachedEdgeAdjacency_ against the new eye position
    // (recomputeEdgeStyle()) without a full rebuild() -- lighter for a
    // per-gesture-end event.
    void onCameraMoved(const events::CameraMoved& event);
    // Forwards the settled navigation gesture to the kernel as
    // CameraNavigated. Never calls pullCamera() itself -- the viewport
    // already shows the change live.
    void onCameraNavigated(QVector3D target, float azimuthDeg, float elevationDeg, float distance, float fovYDeg);

    // Appends model's edges/faces into edgeVerts/faceTris after applying xf;
    // called once for the root (identity, applyVisibility=true) and once
    // per Instance during the scene walk (composed xf, applyVisibility=false).
    // faceMaterialKeys/edgeAdjacency are non-null exactly when writing to
    // the normal buffer (never the dimmed overlay); non-null also switches
    // faceTris to the interleaved pos+uv+normal vertex layout (8 floats/vertex).
    void appendModel(const geo::Model& model, const geo::Transform& xf, bool applyVisibility,
                      const agent::GeometryApi* geometryStore, const agent::TagStore* tagStore,
                      const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                      std::vector<float>& edgeVerts, std::vector<float>& faceTris,
                      std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                      std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const;

    // Synthesizes the in-progress PushPull prism's shaded faces from the
    // armed face's loop + extrudeGhostDistance_ (root model only). Faces
    // only -- PushPullTool's own line preview draws the edges, keeping
    // cachedEdgeAdjacency_ in sync with real edges.
    void appendExtrudeGhost(const geo::Model& model, std::vector<float>& faceTris,
                             std::vector<viewport::TriangleMaterialKey>& faceMaterialKeys) const;

    // Appends def's own geometry (never visibility-filtered) plus every
    // descendant instance recursively into one bucket, once the walk has
    // settled into normal or dimmed. faceMaterialKeys/edgeAdjacency are
    // decided once by the caller and stay constant through the recursion;
    // inheritedMaterialId is re-derived per Instance regardless.
    void appendSubtreeInto(const geo::Scene& scene, const geo::Definition& def, const geo::Transform& xf,
                            const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                            std::vector<float>& edgeOut, std::vector<float>& faceOut,
                            std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                            std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const;

    // Walks the scene from def, following remainingPath (the current
    // context's unconsumed instance-id chain), routing the in-context
    // branch into normal*/ and everything else into dimmed*. isRoot gates
    // hidden/tag visibility. normalFaceMaterialKeys/normalEdgeAdjacency are
    // populated only for the in-context branch -- dimmed is never material-batched or edge-style classified.
    void appendRouted(const geo::Scene& scene, const geo::Definition& def, const geo::Transform& xf, bool isRoot,
                       const agent::GeometryApi* geometryStore, const agent::TagStore* tagStore,
                       const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                       const std::vector<geo::Id>& remainingPath, std::vector<float>& normalEdges,
                       std::vector<float>& normalFaces,
                       std::vector<viewport::TriangleMaterialKey>& normalFaceMaterialKeys,
                       std::vector<viewport::EdgeAdjacency>& normalEdgeAdjacency, std::vector<float>& dimmedEdges,
                       std::vector<float>& dimmedFaces) const;

    // Re-reads the whole Scene (root plus every nested Instance, routed
    // into normal/dimmed by the current editing context) and re-uploads
    // all four buffers, then rebuildSelection() (geometry can move/delete
    // selected entities without a SelectionChanged). Also caches the
    // walk's per-edge adjacency and calls recomputeEdgeStyle() -- the only
    // place that data is (re)derived; CameraMoved/CameraChanged reuse the cache instead.
    void rebuild();

    // Re-uploads the selection-highlight buffers (edges/faces/points, in
    // selection order). Either agent missing clears the overlay.
    void rebuildSelection();

    // Re-uploads the guide overlay: each line becomes a finite dashed
    // segment (+-kGuideLineExtent from its point -- a true infinite line
    // can't be a VBO), each point a small world-space cross. GuideStore missing clears it.
    void rebuildGuides();

    // Pushes AxesStore's frame to the viewport; defaults to origin/X/Y/Z when unregistered.
    void rebuildAxes();

    // Re-uploads the annotation overlay from Dimension's cached lastA/lastB
    // (kept current by GeometryChangedCommand's refreshAssociations, so no
    // GeometryApi access is needed here) plus TextNotes. AnnotationStore missing clears it.
    void rebuildAnnotations();

    // Re-uploads the section-plane overlay (active/inactive buckets),
    // sized against the current model bbox -- also called from rebuild()
    // since a geometry edit changes that sizing. Pushes the active plane's
    // clip-plane uniform, clearing it when none is active. SectionStore missing clears both.
    void rebuildSections();

    // Pushes CameraStore's state onto ViewportWidget's Camera; called at
    // registration and on every CameraChanged. Also refreshes the
    // projection/previous/next QActions' state from the same read.
    void pullCamera();

    // Pushes StyleStore's face style/edge flags/colors/AO onto
    // ViewportWidget. No recomputeEdgeStyle() call -- edge flags are pure
    // draw-time width state, never change which buffer an edge lands in.
    void pullStyle();

    // Pushes ShadowStore's shadow/sun state onto ViewportWidget, computing
    // the world sun direction via agent::sun::solarAngles/sunDirection
    // (zero vector when below the horizon = ambient-only). No rebuild() --
    // per-vertex normals are already baked in; only the sun direction they're dotted against changes.
    void pullShadows();

    // Pushes FogStore's fog state onto ViewportWidget. No rebuild() -- fog
    // is pure per-fragment draw-time state.
    void pullFog();

    // Re-classifies cachedEdgeAdjacency_ against the current eye
    // (viewport::classifyAndBucketEdges) and pushes the result.
    // kEdgeDepthBandCount (3) is this app's own pinned band count.
    void recomputeEdgeStyle();

    viewport::ViewportWidget* viewport_;

    // Non-owning.
    QAction* perspectiveAction_ = nullptr;
    QAction* parallelProjectionAction_ = nullptr;
    QAction* twoPointAction_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* nextAction_ = nullptr;

    // Last rebuild()'s per-edge adjacency (normal buffer only), cached so
    // onCameraMoved/onCameraChanged can reclassify without a full scene walk.
    std::vector<viewport::EdgeAdjacency> cachedEdgeAdjacency_;

    // Live move-ghost: which root-model vertex ids render translated by
    // moveGhostDelta_ during appendModel's walk. Empty = no ghost; cleared
    // by onGeometryChanged so a real mutation wins.
    std::unordered_set<geo::Id> moveGhostVertexIds_;
    geo::Vec3 moveGhostDelta_;

    // Live extrusion ghost: armed face + drag distance; kInvalidId = no
    // ghost. Cleared by onGeometryChanged, same as the move ghost.
    geo::Id extrudeGhostFaceId_{geo::kInvalidId};
    double extrudeGhostDistance_{0.0};
};

}  // namespace plnr::ui
