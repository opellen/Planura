// Tool-layer lever parity: drives the Rectangle tool through the viewport's debugMouse* levers and
// checks the kernel gets the geometry the bridge's injected-click path produces. Also Follow Me,
// preselect vs manual-drag, asserted to sweep identical topology.

#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <geo/model.h>

#include "agent/events.h"
#include "agent/geometry_api.h"
#include "agent/selection_store.h"
#include "harness/probe_scenarios.h"
#include "harness/probe_support.h"
#include "main_window.h"
#include "viewport/viewport_widget.h"

namespace {

struct ModelCounts {
    size_t edges{};
    size_t faces{};
};

const plnr::geo::Model* modelOf(plnr::MainWindow& window) {
    auto api = window.focusedSession().kernel().agentAs<plnr::agent::GeometryApi>(plnr::agent::kGeometryApiName);
    return api ? &api->model() : nullptr;
}

ModelCounts countModel(plnr::MainWindow& window) {
    const plnr::geo::Model* model = modelOf(window);
    if (!model) return {};
    return {model->edges().size(), model->faces().size()};
}

// --- Follow Me scenarios ---

constexpr size_t kSweptNewVertices = 6;
constexpr size_t kSweptNewEdges = 12;
constexpr size_t kSweptNewFaces = 7;

struct FollowMeFixture {
    plnr::geo::Id pathAB{};
    plnr::geo::Id pathBBt{};
    plnr::geo::Id profileFace{};
    plnr::geo::Vec3 a{0.0, 0.0, 0.0};
    plnr::geo::Vec3 b{2.0, 0.0, 0.0};
    plnr::geo::Vec3 bt{2.0, 0.0, 2.0};
    std::vector<plnr::geo::Vec3> triangle{{-1.5, -0.5, 0.0}, {-1.5, 0.5, 0.0}, {-1.5, 0.0, 1.0}};
};

std::optional<plnr::geo::Id> vertexAt(const plnr::geo::Model& model, const plnr::geo::Vec3& p) {
    for (const auto& [id, v] : model.vertices()) {
        if (plnr::geo::length(v.pos - p) < 1e-6) return id;
    }
    return std::nullopt;
}

std::optional<plnr::geo::Id> edgeBetween(const plnr::geo::Model& model, const plnr::geo::Vec3& p,
                                         const plnr::geo::Vec3& q) {
    const auto vp = vertexAt(model, p);
    const auto vq = vertexAt(model, q);
    if (!vp || !vq) return std::nullopt;
    for (const auto& [id, e] : model.edges()) {
        const plnr::geo::HalfEdge* h0 = model.halfEdge(e.halfEdges[0]);
        const plnr::geo::HalfEdge* h1 = model.halfEdge(e.halfEdges[1]);
        if (!h0 || !h1) continue;
        if ((h0->origin == *vp && h1->origin == *vq) || (h0->origin == *vq && h1->origin == *vp)) return id;
    }
    return std::nullopt;
}

// Resets the document and builds the fixture via kernel events: an open 2-edge L path A-B-B'
// (bottom rail along +X, vertical rail up) and a closed triangle profile in the plane x = -1.5,
// perpendicular to the first rail and clear of the path. Self-contained via NewDocumentRequested.
bool buildFollowMeFixture(plnr::MainWindow& window, FollowMeFixture& fx) {
    auto& kernel = window.focusedSession().kernel();
    kernel.send(plnr::events::NewDocumentRequested{});
    kernel.send(plnr::events::AddPolylineRequested{{fx.a, fx.b, fx.bt}, /*closed=*/false});
    kernel.send(plnr::events::AddPolylineRequested{fx.triangle, /*closed=*/true});

    const plnr::geo::Model* model = modelOf(window);
    if (!model) return false;
    const auto ab = edgeBetween(*model, fx.a, fx.b);
    const auto bbt = edgeBetween(*model, fx.b, fx.bt);
    if (!ab || !bbt) return false;
    fx.pathAB = *ab;
    fx.pathBBt = *bbt;
    for (const auto& [id, face] : model->faces()) {
        if (model->faceVertexLoop(id).size() == 3) fx.profileFace = id;
    }
    return fx.profileFace != plnr::geo::Id{};
}

struct ModelIds {
    std::vector<plnr::geo::Id> vertices;
    std::vector<plnr::geo::Id> edges;
    std::vector<plnr::geo::Id> faces;
};

ModelIds idsOf(const plnr::geo::Model& model) {
    ModelIds ids;
    for (const auto& [id, v] : model.vertices()) ids.vertices.push_back(id);
    for (const auto& [id, e] : model.edges()) ids.edges.push_back(id);
    for (const auto& [id, f] : model.faces()) ids.faces.push_back(id);
    return ids;
}

bool hasId(const std::vector<plnr::geo::Id>& ids, plnr::geo::Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// What a sweep added relative to `before`: counts plus the new vertices' positions (sorted).
struct SweptTopology {
    size_t newVertices{};
    size_t newEdges{};
    size_t newFaces{};
    std::vector<plnr::geo::Vec3> newVertexPositions;
};

SweptTopology sweptSince(const plnr::geo::Model& model, const ModelIds& before) {
    SweptTopology out;
    for (const auto& [id, v] : model.vertices()) {
        if (hasId(before.vertices, id)) continue;
        ++out.newVertices;
        out.newVertexPositions.push_back(v.pos);
    }
    for (const auto& [id, e] : model.edges()) out.newEdges += hasId(before.edges, id) ? 0 : 1;
    for (const auto& [id, f] : model.faces()) out.newFaces += hasId(before.faces, id) ? 0 : 1;
    std::sort(out.newVertexPositions.begin(), out.newVertexPositions.end(),
              [](const plnr::geo::Vec3& l, const plnr::geo::Vec3& r) {
                  if (l.x != r.x) return l.x < r.x;
                  if (l.y != r.y) return l.y < r.y;
                  return l.z < r.z;
              });
    return out;
}

// Preselect mode's swept vertices, for the drag scenario's equivalence assert. Empty if preselect did not run (filtered run).
std::vector<plnr::geo::Vec3>& preselectSweptVertices() {
    static std::vector<plnr::geo::Vec3> positions;
    return positions;
}

}  // namespace

int runLeverParityScenario(plnr::MainWindow& window) {
    using plnr::events::ToolId;
    bool ok = true;

    plnr::viewport::ViewportWidget* viewport = window.viewportWidget();
    ok &= probeCheck("levers.viewport-found", viewport != nullptr);
    if (!viewport) return 1;

    // Opposite corners of a ground-plane rectangle.
    const plnr::geo::Vec3 cornerA{0.0, 0.0, 0.0};
    const plnr::geo::Vec3 cornerB{2.0, 1.0, 0.0};
    ok &= probeCheck("levers.corner-a-visible", projectWorld(viewport, cornerA).visible);
    ok &= probeCheck("levers.corner-b-visible", projectWorld(viewport, cornerB).visible);

    const ModelCounts before = countModel(window);

    window.toolAction(ToolId::Rectangle)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("levers.rectangle-active", window.activeTool() == ToolId::Rectangle);

    ok &= probeCheck("levers.click-corner-a", clickWorld(viewport, cornerA));
    pumpEventsFor(50);
    ok &= probeCheck("levers.click-corner-b", clickWorld(viewport, cornerB));
    pumpEventsFor(50);

    const ModelCounts after = countModel(window);
    ok &= probeCheck("levers.rectangle-adds-four-edges", after.edges == before.edges + 4,
                     QString::number(after.edges) + QStringLiteral(" vs ") + QString::number(before.edges));
    ok &= probeCheck("levers.rectangle-adds-one-face", after.faces == before.faces + 1,
                     QString::number(after.faces) + QStringLiteral(" vs ") + QString::number(before.faces));

    // Any quad face whose every vertex sits on the ground plane.
    bool groundFace = false;
    if (const plnr::geo::Model* model = modelOf(window)) {
        for (const auto& [id, face] : model->faces()) {
            const std::vector<plnr::geo::Id> loop = model->faceVertexLoop(id);
            bool flat = loop.size() == 4;
            for (plnr::geo::Id vid : loop) {
                const plnr::geo::Vertex* v = model->vertex(vid);
                flat = flat && v && std::abs(v->pos.z) < 1e-6;
            }
            groundFace = groundFace || flat;
        }
    }
    ok &= probeCheck("levers.face-is-planar-ground", groundFace);

    window.toolAction(ToolId::Select)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("levers.select-rearmed", window.activeTool() == ToolId::Select);

    return ok ? 0 : 1;
}

// Follow Me, preselect mode: path edges selected via kernel events, then one click on the profile
// sweeps it. Leaves the swept fixture, path edges selected, Select active.
int runFollowMePreselectScenario(plnr::MainWindow& window) {
    using plnr::events::ToolId;
    bool ok = true;

    plnr::viewport::ViewportWidget* viewport = window.viewportWidget();
    ok &= probeCheck("followme.preselect.viewport-found", viewport != nullptr);
    if (!viewport) return 1;

    FollowMeFixture fx;
    ok &= probeCheck("followme.preselect.fixture-built", buildFollowMeFixture(window, fx));
    if (fx.profileFace == plnr::geo::Id{}) return 1;
    auto& kernel = window.focusedSession().kernel();

    // Preselect the two path edges (Replace, then Add -- what Ctrl-click does through the Select tool).
    kernel.send(plnr::events::SelectRequested{plnr::events::SelectMode::Replace,
                                              plnr::events::EntityRef{plnr::geo::EntityKind::Edge, fx.pathAB},
                                              plnr::events::SelectExpand::None});
    kernel.send(plnr::events::SelectRequested{plnr::events::SelectMode::Add,
                                              plnr::events::EntityRef{plnr::geo::EntityKind::Edge, fx.pathBBt},
                                              plnr::events::SelectExpand::None});
    auto selection = kernel.agentAs<plnr::agent::SelectionStore>(plnr::agent::kSelectionStoreName);
    ok &= probeCheck("followme.preselect.path-edges-selected", selection && selection->items().size() == 2);

    window.toolAction(ToolId::FollowMe)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("followme.preselect.tool-active", window.activeTool() == ToolId::FollowMe);

    const auto centroid = worldOfFaceCentroid(kernel, fx.profileFace);
    ok &= probeCheck("followme.preselect.profile-centroid", centroid.has_value());
    if (!centroid) return 1;
    ok &= probeCheck("followme.preselect.profile-visible", projectWorld(viewport, *centroid).visible);

    const ModelIds before = idsOf(*modelOf(window));
    ok &= probeCheck("followme.preselect.click-profile", clickWorld(viewport, *centroid));
    pumpEventsFor(50);

    const plnr::geo::Model* model = modelOf(window);
    const SweptTopology swept = sweptSince(*model, before);
    ok &= probeCheck("followme.preselect.swept-vertices", swept.newVertices == kSweptNewVertices,
                     QString::number(swept.newVertices));
    ok &= probeCheck("followme.preselect.swept-edges", swept.newEdges == kSweptNewEdges,
                     QString::number(swept.newEdges));
    ok &= probeCheck("followme.preselect.swept-faces", swept.newFaces == kSweptNewFaces,
                     QString::number(swept.newFaces));
    ok &= probeCheck("followme.preselect.profile-face-kept", model->face(fx.profileFace) != nullptr);
    preselectSweptVertices() = swept.newVertexPositions;

    window.toolAction(ToolId::Select)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("followme.preselect.select-rearmed", window.activeTool() == ToolId::Select);
    return ok ? 0 : 1;
}

// Follow Me, manual drag mode: same fixture, NO preselection; press the profile, drag across both path
// edges, release at the path end. Core assert: equivalence with preselect. Rebuilds the fixture itself.
int runFollowMeDragScenario(plnr::MainWindow& window) {
    using plnr::events::ToolId;
    bool ok = true;

    plnr::viewport::ViewportWidget* viewport = window.viewportWidget();
    ok &= probeCheck("followme.drag.viewport-found", viewport != nullptr);
    if (!viewport) return 1;

    FollowMeFixture fx;
    ok &= probeCheck("followme.drag.fixture-built", buildFollowMeFixture(window, fx));
    if (fx.profileFace == plnr::geo::Id{}) return 1;
    auto& kernel = window.focusedSession().kernel();

    auto selection = kernel.agentAs<plnr::agent::SelectionStore>(plnr::agent::kSelectionStoreName);
    ok &= probeCheck("followme.drag.no-preselection", selection && selection->items().empty());

    window.toolAction(ToolId::FollowMe)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("followme.drag.tool-active", window.activeTool() == ToolId::FollowMe);

    const auto centroid = worldOfFaceCentroid(kernel, fx.profileFace);
    const auto midAB = worldOfEdgeMidpoint(kernel, fx.pathAB);
    const auto midBBt = worldOfEdgeMidpoint(kernel, fx.pathBBt);
    ok &= probeCheck("followme.drag.anchors-resolved", centroid && midAB && midBBt);
    if (!centroid || !midAB || !midBBt) return 1;
    ok &= probeCheck("followme.drag.anchors-visible",
                     projectWorld(viewport, *centroid).visible && projectWorld(viewport, *midAB).visible &&
                         projectWorld(viewport, *midBBt).visible && projectWorld(viewport, fx.bt).visible);

    const ModelIds before = idsOf(*modelOf(window));

    // Press on the profile, drag across both rail midpoints, release at the path end.
    viewport->debugMousePress(projectWorld(viewport, *centroid).widget, Qt::LeftButton, {});
    ok &= probeCheck("followme.drag.move-rail-ab", moveWorld(viewport, *midAB));
    ok &= probeCheck("followme.drag.move-rail-bbt", moveWorld(viewport, *midBBt));
    ok &= probeCheck("followme.drag.move-path-end", moveWorld(viewport, fx.bt));
    // Nothing is swept until the gesture ends.
    ok &= probeCheck("followme.drag.nothing-swept-before-release",
                     modelOf(window)->vertices().size() == before.vertices.size());
    viewport->debugMouseRelease(projectWorld(viewport, fx.bt).widget, Qt::LeftButton, {});
    pumpEventsFor(50);

    const plnr::geo::Model* model = modelOf(window);
    const SweptTopology swept = sweptSince(*model, before);
    ok &= probeCheck("followme.drag.swept-vertices", swept.newVertices == kSweptNewVertices,
                     QString::number(swept.newVertices));
    ok &= probeCheck("followme.drag.swept-edges", swept.newEdges == kSweptNewEdges, QString::number(swept.newEdges));
    ok &= probeCheck("followme.drag.swept-faces", swept.newFaces == kSweptNewFaces, QString::number(swept.newFaces));
    ok &= probeCheck("followme.drag.profile-face-kept", model->face(fx.profileFace) != nullptr);

    // Mode equivalence on geometry; skipped only when preselect did not run (filtered run).
    const std::vector<plnr::geo::Vec3>& preselect = preselectSweptVertices();
    if (!preselect.empty()) {
        bool same = preselect.size() == swept.newVertexPositions.size();
        for (size_t i = 0; same && i < preselect.size(); ++i) {
            same = plnr::geo::length(preselect[i] - swept.newVertexPositions[i]) < 1e-6;
        }
        ok &= probeCheck("followme.drag.matches-preselect-vertices", same);
    }

    window.toolAction(ToolId::Select)->trigger();
    pumpEventsFor(50);
    ok &= probeCheck("followme.drag.select-rearmed", window.activeTool() == ToolId::Select);
    return ok ? 0 : 1;
}
