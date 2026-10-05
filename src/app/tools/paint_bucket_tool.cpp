#include "paint_bucket_tool.h"

#include <algorithm>
#include <optional>

#include <geo/select.h>

namespace plnr::tools {

namespace {

constexpr const char* kHint =
    "Click to paint an item or object. | Alt = Sample Material. | Shift = Paint All Matching. | "
    "Ctrl = Paint All Connected. | Shift + Ctrl = Paint All on Same Object.";

// Every Face whose front material equals materialId (0==0 matches
// unpainted) -- Shift's "All Matching" scope. Sorted by id for determinism.
std::vector<events::EntityRef> matchingFaces(const geo::Model& model, ToolContext& ctx, geo::Id materialId) {
    std::vector<events::EntityRef> out;
    for (const auto& [faceId, face] : model.faces()) {
        (void)face;
        const events::EntityRef ref{geo::EntityKind::Face, faceId};
        if (ctx.frontMaterialOf(ref) == materialId) out.push_back(ref);
    }
    std::sort(out.begin(), out.end(), [](const events::EntityRef& a, const events::EntityRef& b) { return a.id < b.id; });
    return out;
}

// faceId's connected-component face set -- Ctrl's "All Connected" scope
// (and Shift+Ctrl's approximation). Walks OUTWARD through half-edge
// adjacency, not geo::closureOf, which walks INWARD to a face's own boundary.
std::vector<events::EntityRef> connectedFaces(const geo::Model& model, geo::Id faceId) {
    const geo::ConnectedSet set = geo::connectedComponent(model, geo::EntityKind::Face, faceId);
    std::vector<events::EntityRef> out;
    out.reserve(set.faces.size());
    for (geo::Id id : set.faces) out.push_back(events::EntityRef{geo::EntityKind::Face, id});
    return out;
}

// The committing (non-Alt) click's paint target set -- see the class
// comment for the modifier-to-scope table.
std::vector<events::EntityRef> resolveTargets(ToolContext& ctx, const events::EntityRef& target, bool shift,
                                               bool ctrl) {
    if (!shift && !ctrl) return {target};                        // plain click: target only
    if (target.kind != geo::EntityKind::Face) return {target};   // Instance: modifiers never expand
    if (!ctx.atRootContext()) return {target};                   // root-only face enumeration (class comment)

    const geo::Model* model = ctx.model();
    if (!model) return {target};

    if (shift && !ctrl) return matchingFaces(*model, ctx, ctx.frontMaterialOf(target));
    // Ctrl, or Shift+Ctrl: same connected-component set (class comment's approximation rationale).
    return connectedFaces(*model, target.id);
}

}  // namespace

void PaintBucketTool::onActivate(ToolContext& ctx) {
    ctx.setHint(kHint);
}

void PaintBucketTool::onDeactivate(ToolContext& ctx) {
    // No preview overlay is ever set by this tool, but every Tool MUST clear it on deactivate per tool.h's contract.
    ctx.setPreview({}, std::nullopt);
}

void PaintBucketTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    // Face-only, container-aware pick -- a hit inside a group/component
    // Instance resolves to that top-level instance.
    const geo::ScenePickResult hit = ctx.pickScene(e, geo::PickOptions{0.0, 0.0});

    std::optional<events::EntityRef> target;
    if (hit.instanceId != geo::kInvalidId) {
        target = events::EntityRef{geo::EntityKind::Instance, hit.instanceId};
    } else if (hit.kind == geo::PickKind::Face) {
        target = events::EntityRef{geo::EntityKind::Face, hit.id};
    }
    if (!target) return;  // miss -- nothing under the cursor

    if (e.alt) {
        // Sample: no mutation, just loads the target's own front material (0/
        // unpainted is valid, same as clicking Default in the real Materials panel).
        ctx.requestSetActiveMaterial(ctx.frontMaterialOf(*target));
        return;
    }

    const geo::Id activeId = ctx.activeMaterialId();
    if (activeId == 0) {
        // the reference modeler always has a default active material loaded; this MVP gap (no
        // active material) is surfaced via the status bar rather than silently no-op'd.
        ctx.setHint("Select a material first.");
        return;
    }

    const std::vector<events::EntityRef> targets = resolveTargets(ctx, *target, e.shift, e.ctrl);
    ctx.requestPaint(targets, activeId);
    ctx.setHint(kHint);  // re-assert over any prior "Select a material first."
}

}  // namespace plnr::tools
