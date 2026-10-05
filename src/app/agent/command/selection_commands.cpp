#include "agent/command/selection_commands.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <ordo/core/kernel.h>

#include <geo/entity.h>
#include <geo/scene_pick.h>
#include <geo/select.h>

#include "agent/edit_context_store.h"
#include "agent/geometry_api.h"
#include "agent/selection_store.h"
#include "agent/tag_store.h"

namespace plnr::agent {

namespace {

// Appends one EntityRef per id in ids (as kind), skipping an id equal to skip (the expansion
// target itself, for the Connected case where its own kind is walked too).
void appendRefs(std::vector<events::EntityRef>& out, geo::EntityKind kind, const std::vector<geo::Id>& ids,
                 const events::EntityRef& skip) {
    for (geo::Id id : ids) {
        const events::EntityRef ref{kind, id};
        if (ref == skip) continue;
        out.push_back(ref);
    }
}

}  // namespace

void PruneSelectionCommand::execute(const events::GeometryChanged& event, ordo::core::CommandContext& context) {
    (void)event;
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!geometry || !selection) {
        // Either agent missing from this kernel -- no-op (Qt-free, no logging).
        return;
    }

    selection->prune([&geometry](const events::EntityRef& ref) {
        switch (ref.kind) {
            case geo::EntityKind::Vertex:
                return geometry->model().vertex(ref.id) != nullptr;
            case geo::EntityKind::Edge:
                return geometry->model().edge(ref.id) != nullptr;
            case geo::EntityKind::Face:
                return geometry->model().face(ref.id) != nullptr;
            case geo::EntityKind::Instance:
                // Instances aren't part of geometry->model() -- alive means still a direct child of the root.
                return geometry->scene().findInstance(geo::kRootDefinitionId, ref.id) != nullptr;
        }
        return false;  // Unknown kind -- treat as dead.
    });
}

void SelectCommand::execute(const events::SelectRequested& event, ordo::core::CommandContext& context) {
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!selection) return;  // SelectionStore not registered on this kernel.

    // Build the ref list: target first (if any), then whatever event.expand
    // asks to add beyond it -- expansion only ever runs off a real target.
    std::vector<events::EntityRef> refs;
    if (event.target) {
        refs.push_back(*event.target);

        if (event.expand != events::SelectExpand::None) {
            auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
            // Expansion needs the model to walk adjacency; if GeometryApi isn't registered,
            // degrade to target-only rather than failing the whole request.
            if (geometry) {
                const geo::Model& model = geometry->model();
                const events::EntityRef& target = *event.target;

                if (event.expand == events::SelectExpand::Attached) {
                    switch (target.kind) {
                        case geo::EntityKind::Vertex:
                            break;  // just the vertex -- already the sole entry above
                        case geo::EntityKind::Edge:
                            appendRefs(refs, geo::EntityKind::Vertex,
                                       geo::collectVertices(model, geo::EntityKind::Edge, target.id), target);
                            break;
                        case geo::EntityKind::Face:
                            appendRefs(refs, geo::EntityKind::Edge, geo::faceBoundaryEdges(model, target.id), target);
                            break;
                        case geo::EntityKind::Instance:
                            break;  // no expansion -- the instance itself is the whole target
                    }
                } else {  // SelectExpand::Connected
                    const geo::ConnectedSet connected = geo::connectedComponent(model, target.kind, target.id);
                    // Faces, then edges, then vertices -- target ref stays first, skipped here to avoid a duplicate.
                    appendRefs(refs, geo::EntityKind::Face, connected.faces, target);
                    appendRefs(refs, geo::EntityKind::Edge, connected.edges, target);
                    appendRefs(refs, geo::EntityKind::Vertex, connected.vertices, target);
                }
            }
        }
    }

    // Dedup before dispatching: toggle() flips membership per entry, so a duplicate would toggle
    // it right back off. replace()/add()/subtract() are idempotent, so dedup keeps every mode safe.
    std::vector<events::EntityRef> deduped;
    std::unordered_set<events::EntityRef> seen;
    deduped.reserve(refs.size());
    for (const events::EntityRef& ref : refs) {
        if (seen.insert(ref).second) {
            deduped.push_back(ref);
        }
    }

    switch (event.mode) {
        case events::SelectMode::Replace:
            selection->replace(deduped);  // empty refs = deselect all
            return;
        case events::SelectMode::Add:
            if (deduped.empty()) return;  // no-target contract: modifier-click on empty space is a no-op
            selection->add(deduped);
            return;
        case events::SelectMode::Toggle:
            if (deduped.empty()) return;
            selection->toggle(deduped);
            return;
        case events::SelectMode::Subtract:
            if (deduped.empty()) return;
            selection->subtract(deduped);
            return;
    }
}

void SelectRegionCommand::execute(const events::SelectRegionRequested& event, ordo::core::CommandContext& context) {
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!selection || !geometry) return;  // either agent missing from this kernel -- nothing to do

    const geo::Frustum frustum = geo::frustumFromCornerRays(event.corners);
    const geo::RegionMode mode = event.crossing ? geo::RegionMode::Crossing : geo::RegionMode::Window;
    // Hidden entities are excluded from region selection. Tag-invisible entities join this filter
    // too; TagStore may be absent from this kernel, in which case tag visibility isn't filtered.
    auto tagStore = context.agentAs<TagStore>(kTagStoreName);
    const auto visibilityFilter = [&geometry, &tagStore](geo::EntityKind kind, geo::Id id) {
        const events::EntityRef ref{kind, id};
        if (geometry->isHidden(ref)) return false;
        if (tagStore && !tagStore->isEntityVisible(ref)) return false;
        return true;
    };
    // regionPickScene can't apply visibilityFilter to instance interiors, so qualifying instances
    // are post-filtered below. Scopes the pick to the current editing context (root if unregistered).
    auto editContextStore = context.agentAs<EditContextStore>(kEditContextStoreName);
    const std::vector<geo::Id> contextPath = editContextStore ? editContextStore->path() : std::vector<geo::Id>{};

    geo::RegionPick region;
    std::vector<geo::Id> instanceIds;
    geo::regionPickScene(geometry->scene(), frustum, mode, visibilityFilter, region, instanceIds, contextPath);

    std::vector<geo::Id> visibleInstanceIds;
    visibleInstanceIds.reserve(instanceIds.size());
    for (geo::Id id : instanceIds) {
        if (visibilityFilter(geo::EntityKind::Instance, id)) {
            visibleInstanceIds.push_back(id);
        }
    }

    // Faces, then edges, then vertices (matches SelectCommand's Connected-expansion ordering);
    // qualifying instances appended last. noSkip is a default EntityRef (id 0 is never real).
    std::vector<events::EntityRef> refs;
    const events::EntityRef noSkip{};
    appendRefs(refs, geo::EntityKind::Face, region.faces, noSkip);
    appendRefs(refs, geo::EntityKind::Edge, region.edges, noSkip);
    appendRefs(refs, geo::EntityKind::Vertex, region.vertices, noSkip);
    appendRefs(refs, geo::EntityKind::Instance, visibleInstanceIds, noSkip);

    // Dedup for the same reason as SelectCommand: toggle() flips membership per entry. Each source
    // list is duplicate-free internally, but an id could repeat across the three (kind, id) lists.
    std::vector<events::EntityRef> deduped;
    std::unordered_set<events::EntityRef> seen;
    deduped.reserve(refs.size());
    for (const events::EntityRef& ref : refs) {
        if (seen.insert(ref).second) {
            deduped.push_back(ref);
        }
    }

    switch (event.mode) {
        case events::SelectMode::Replace:
            selection->replace(deduped);  // empty region = deselect all
            return;
        case events::SelectMode::Add:
            if (deduped.empty()) return;
            selection->add(deduped);
            return;
        case events::SelectMode::Toggle:
            if (deduped.empty()) return;
            selection->toggle(deduped);
            return;
        case events::SelectMode::Subtract:
            if (deduped.empty()) return;
            selection->subtract(deduped);
            return;
    }
}

void SelectAllCommand::execute(const events::SelectAllRequested& event, ordo::core::CommandContext& context) {
    (void)event;
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!selection || !geometry) return;  // either agent missing from this kernel -- nothing to do

    // Scope to the current editing context, same resolution SelectRegionCommand
    // uses: empty path (root) when EditContextStore isn't registered.
    auto editContextStore = context.agentAs<EditContextStore>(kEditContextStoreName);
    const std::vector<geo::Id> contextPath = editContextStore ? editContextStore->path() : std::vector<geo::Id>{};
    const geo::Model* model = geometry->contextModel(contextPath);
    if (!model) return;  // invalid/foreign path -- nothing to select (defensive; a live context always resolves)

    // Same hidden/tag-visibility filter as SelectRegionCommand's own
    // visibilityFilter -- TagStore may be absent from this kernel, in which
    // case tag visibility is simply not filtered.
    auto tagStore = context.agentAs<TagStore>(kTagStoreName);
    const auto visible = [&geometry, &tagStore](geo::EntityKind kind, geo::Id id) {
        const events::EntityRef ref{kind, id};
        if (geometry->isHidden(ref)) return false;
        if (tagStore && !tagStore->isEntityVisible(ref)) return false;
        return true;
    };

    // model->vertices()/edges()/faces() are unordered_maps (nondeterministic order); ids are
    // collected and sorted ascending so Select All's resulting order is deterministic.
    const auto sortedVisibleIds = [&visible](const auto& entityMap, geo::EntityKind kind) {
        std::vector<geo::Id> ids;
        ids.reserve(entityMap.size());
        for (const auto& [id, entity] : entityMap) {
            (void)entity;
            if (visible(kind, id)) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    const std::vector<geo::Id> faceIds = sortedVisibleIds(model->faces(), geo::EntityKind::Face);
    const std::vector<geo::Id> edgeIds = sortedVisibleIds(model->edges(), geo::EntityKind::Edge);
    const std::vector<geo::Id> vertexIds = sortedVisibleIds(model->vertices(), geo::EntityKind::Vertex);

    // Faces, then edges, then vertices -- same ordering SelectCommand's
    // Connected expansion and SelectRegionCommand already use.
    std::vector<events::EntityRef> refs;
    refs.reserve(faceIds.size() + edgeIds.size() + vertexIds.size());
    const events::EntityRef noSkip{};  // no expansion target to dedup against here, see appendRefs' own comment
    appendRefs(refs, geo::EntityKind::Face, faceIds, noSkip);
    appendRefs(refs, geo::EntityKind::Edge, edgeIds, noSkip);
    appendRefs(refs, geo::EntityKind::Vertex, vertexIds, noSkip);

    // Instances: only at the ROOT context, and only its direct children (Scene::root().children is
    // already in ascending-id order). NOT selected while inside an edit context -- PruneSelectionCommand
    // treats "alive" as "direct child of ROOT", so an in-context instance would get pruned right back out.
    if (contextPath.empty()) {
        for (const geo::Instance& instance : geometry->scene().root().children) {
            if (visible(geo::EntityKind::Instance, instance.id)) {
                refs.push_back(events::EntityRef{geo::EntityKind::Instance, instance.id});
            }
        }
    }

    // Always replace -- no dedup needed (refs is already duplicate-free by
    // construction) and no SelectMode switch (the reference modeler's Select All has no
    // modifier-click variant).
    selection->replace(std::move(refs));
}

void SetHiddenCommand::execute(const events::SetHiddenRequested& event, ordo::core::CommandContext& context) {
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) return;  // GeometryApi not registered on this kernel.

    geometry->setHidden(event.refs, event.hidden);

    if (event.hidden) {
        // the reference modeler: hiding an entity deselects it too.
        auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
        if (selection) {
            selection->subtract(event.refs);
        }
    }
}

void UnhideAllCommand::execute(const events::UnhideAllRequested& event, ordo::core::CommandContext& context) {
    (void)event;
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    if (!geometry) return;  // GeometryApi not registered on this kernel.

    geometry->unhideAll();
}

void DeleteSelectionCommand::execute(const events::DeleteSelectionRequested& event, ordo::core::CommandContext& context) {
    (void)event;
    auto geometry = context.agentAs<GeometryApi>(kGeometryApiName);
    auto selection = context.agentAs<SelectionStore>(kSelectionStoreName);
    if (!geometry || !selection) return;  // either agent missing from this kernel -- nothing to do

    // GeometryApi::removeEntities is already a no-op (no mutation, no
    // event) on an empty refs list, so an empty selection needs no explicit
    // guard here -- see its own header comment.
    geometry->removeEntities(selection->items());
}

}  // namespace plnr::agent
