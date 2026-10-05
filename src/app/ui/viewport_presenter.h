#pragma once

#include <unordered_set>
#include <vector>

#include <QVector3D>

#include <ordo/core/kernel.h>
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
    ViewportPresenter(viewport::ViewportWidget* viewport, QAction* perspectiveAction,
                       QAction* parallelProjectionAction, QAction* twoPointAction, QAction* previousAction,
                       QAction* nextAction);

    // Subscribes to the relevant *Changed events and does an initial pull/rebuild so
    // pre-registration state still shows; connects ViewportWidget::cameraNavigated to CameraStore.
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
    // Any material create/edit/paint can change a face's resolved color: full rebuild().
    void onMaterialsChanged(const events::MaterialsChanged& event);
    // Re-pulls and pushes StyleStore's face style/colors (pullStyle()).
    void onStyleChanged(const events::StyleChanged& event);
    // Re-pulls ShadowStore's sun/shadow state; draw-time only, no rebuild().
    void onShadowsChanged(const events::ShadowsChanged& event);
    // Re-pulls FogStore's fog state; draw-time only, no rebuild().
    void onFogChanged(const events::FogChanged& event);
    // Re-pulls the restored camera plus recomputeEdgeStyle(), in case restoreCamera ever
    // fires without a following GeometryChanged.
    void onCameraChanged(const events::CameraChanged& event);
    // Re-classifies cachedEdgeAdjacency_ against the new eye position without a full rebuild().
    void onCameraMoved(const events::CameraMoved& event);
    // Forwards the settled navigation gesture to the kernel as CameraNavigated. Never calls
    // pullCamera(); the viewport already shows the change live.
    void onCameraNavigated(QVector3D target, float azimuthDeg, float elevationDeg, float distance, float fovYDeg);

    // Appends model's edges/faces into edgeVerts/faceTris after applying xf; called once for the
    // root (identity, applyVisibility=true) and once per Instance (composed xf, applyVisibility=false).
    // faceMaterialKeys/edgeAdjacency are non-null exactly when writing to the normal buffer (never
    // the dimmed overlay); non-null also switches faceTris to interleaved pos+uv+normal (8 floats/vertex).
    void appendModel(const geo::Model& model, const geo::Transform& xf, bool applyVisibility,
                      const agent::GeometryApi* geometryStore, const agent::TagStore* tagStore,
                      const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                      std::vector<float>& edgeVerts, std::vector<float>& faceTris,
                      std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                      std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const;

    // Synthesizes the in-progress PushPull prism's shaded faces from the armed face's loop +
    // extrudeGhostDistance_ (root model only). Faces only; PushPullTool's own line preview draws the edges.
    void appendExtrudeGhost(const geo::Model& model, std::vector<float>& faceTris,
                             std::vector<viewport::TriangleMaterialKey>& faceMaterialKeys) const;

    // Appends def's own geometry (never visibility-filtered) plus every descendant instance,
    // recursively, into one bucket. faceMaterialKeys/edgeAdjacency are fixed by the caller;
    // inheritedMaterialId is re-derived per Instance.
    void appendSubtreeInto(const geo::Scene& scene, const geo::Definition& def, const geo::Transform& xf,
                            const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                            std::vector<float>& edgeOut, std::vector<float>& faceOut,
                            std::vector<viewport::TriangleMaterialKey>* faceMaterialKeys,
                            std::vector<viewport::EdgeAdjacency>* edgeAdjacency) const;

    // Walks the scene from def along remainingPath (the context's unconsumed instance-id chain),
    // routing the in-context branch into normal* and everything else into dimmed*. isRoot gates
    // hidden/tag visibility. normalFaceMaterialKeys/normalEdgeAdjacency fill only for the
    // in-context branch; dimmed is never material-batched or edge-style classified.
    void appendRouted(const geo::Scene& scene, const geo::Definition& def, const geo::Transform& xf, bool isRoot,
                       const agent::GeometryApi* geometryStore, const agent::TagStore* tagStore,
                       const agent::MaterialRepository* materialStore, geo::Id inheritedMaterialId,
                       const std::vector<geo::Id>& remainingPath, std::vector<float>& normalEdges,
                       std::vector<float>& normalFaces,
                       std::vector<viewport::TriangleMaterialKey>& normalFaceMaterialKeys,
                       std::vector<viewport::EdgeAdjacency>& normalEdgeAdjacency, std::vector<float>& dimmedEdges,
                       std::vector<float>& dimmedFaces) const;

    // Re-reads the whole Scene (routed into normal/dimmed by the editing context) and re-uploads
    // all four buffers, then rebuildSelection() (geometry can move/delete selected entities without
    // a SelectionChanged). Also caches per-edge adjacency and calls recomputeEdgeStyle();
    // CameraMoved/CameraChanged reuse the cache.
    void rebuild();

    // Re-uploads the selection-highlight buffers (edges/faces/points, in
    // selection order). Either agent missing clears the overlay.
    void rebuildSelection();

    // Re-uploads the guide overlay: each line a finite dashed segment (+-kGuideLineExtent; a true
    // infinite line can't be a VBO), each point a small world-space cross. GuideStore missing clears it.
    void rebuildGuides();

    // Pushes AxesStore's frame to the viewport; defaults to origin/X/Y/Z when unregistered.
    void rebuildAxes();

    // Re-uploads the annotation overlay from Dimension's cached lastA/lastB (kept current by
    // GeometryChangedCommand) plus TextNotes. AnnotationStore missing clears it.
    void rebuildAnnotations();

    // Re-uploads the section-plane overlay (active/inactive buckets), sized against the model bbox,
    // so rebuild() calls it too. Pushes the active plane's clip-plane uniform, clearing it when none
    // is active. SectionStore missing clears both.
    void rebuildSections();

    // Pushes CameraStore's state onto the viewport Camera, at registration and on every
    // CameraChanged; also refreshes the projection/previous/next QActions.
    void pullCamera();

    // Pushes StyleStore's face style/edge flags/colors/AO onto the viewport. No recomputeEdgeStyle():
    // edge flags are draw-time width state and never change an edge's buffer.
    void pullStyle();

    // Pushes ShadowStore's state onto the viewport; the world sun direction comes from agent::sun
    // (zero vector below the horizon = ambient-only). No rebuild(): normals are already baked in;
    // only the sun direction they're dotted against changes.
    void pullShadows();

    // Pushes FogStore's fog state onto the viewport. No rebuild(): fog is per-fragment draw-time state.
    void pullFog();

    // Re-classifies cachedEdgeAdjacency_ against the current eye (viewport::classifyAndBucketEdges)
    // and pushes the result. kEdgeDepthBandCount (3) is this app's own pinned band count.
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
