#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include "agent/annotation_store.h"
#include "agent/axes_store.h"
#include "agent/events.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/tag_store.h"

// The payload UndoStore's stacks hold. Pure data shape, no behavior -- kept in its own header so
// undo_store.h can agent TransactionDelta by value without pulling in Transaction.
namespace plnr::agent {

// One captured geo::ModelJournal/geo::SceneJournal event, tagged to replay in either direction.
// Flat struct (Kind enum + overlapping fields) so a TransactionDelta can hold ONE ordered list
// spanning both mesh and scene events -- order across a cross-model gesture is semantic.
enum class TransactionOpKind {
    VertexCreated,
    VertexMoved,
    VertexDeleted,
    EdgeCreated,
    EdgeDeleted,
    FaceCreated,
    FaceDeleted,
    FaceLoopModified,
    DefinitionCreated,
    DefinitionDeleted,
    InstanceCreated,
    InstanceDeleted,
    InstanceModified,
};

// Which fields are meaningful depends on kind.
struct TransactionOp {
    TransactionOpKind kind{};

    // Mesh ops: the owning definition's id (geo::kRootDefinitionId for
    // root). Instance ops: reused as parentDefId. Unused for Definition*
    // ops (id below IS the definition).
    geo::Id definitionId{};

    geo::Id id{};  // vertex/edge/face id, OR definition id, OR instance id

    // Vertex ops.
    geo::Vec3 posA;  // created: pos. moved: before. deleted: posBefore.
    geo::Vec3 posB;  // moved: after. Unused otherwise.

    // Edge ops.
    geo::Id idA{};  // v0
    geo::Id idB{};  // v1

    // Face ops -- ordered vertex-id boundary loops (geo::Model::faceVertexLoop's convention).
    std::vector<geo::Id> loopA;  // created: loop. deleted: loopBefore. loopModified: loopBefore.
    std::vector<geo::Id> loopB;  // loopModified: loopAfter. Unused otherwise.

    // Definition ops.
    std::string nameA;  // definition/instance name (before-image where applicable)
    bool isGroup{};      // Definition* ops only
    // Definition* ops only: the definition's own Model::nextId_ reservation to restore after
    // recreating it (a recreated Definition gets a fresh empty Model).
    geo::Id definitionModelNextId{};

    // Instance ops.
    geo::Id targetDefinitionId{};  // the Definition an instance points to (geo::Instance::definitionId)
    geo::Transform transformA;     // created/deleted: transform. modified: before.
    geo::Transform transformB;     // modified: after. Unused otherwise.
    std::string nameB;             // modified: nameAfter. Unused otherwise.
};

// Full-copy before/after pair for the {tags,assignments} aux category (aux agents are tiny,
// simpler as whole-copy diffs than an incremental diff like the mesh side above).
struct TagsSnapshot {
    std::vector<Tag> tags;
    std::unordered_map<events::EntityRef, std::uint64_t> assignments;
};

// Same, for the {dimensions,texts} aux category (AnnotationStore shares one
// id counter across both collections, so they travel together).
struct AnnotationsSnapshot {
    std::vector<Dimension> dimensions;
    std::vector<TextNote> texts;
};

// Same shape as TagsSnapshot, for the {materials, assignments} aux category. activeMaterialId is
// deliberately excluded -- transient UI-adjacent state, never undo-worthy.
struct MaterialsSnapshot {
    std::vector<Material> materials;
    std::unordered_map<events::EntityRef, MaterialAssignment> assignments;
};

// One committed undo/redo step. ops is the ORDERED mesh/scene event list -- undo replays it in
// reverse (before-images), redo forward (after-images). Each aux-agent diff is present only when
// that category changed; .first is before (undo), .second is after (redo).
struct TransactionDelta {
    std::vector<TransactionOp> ops;

    std::optional<std::pair<TagsSnapshot, TagsSnapshot>> tags;
    std::optional<std::pair<std::vector<Guide>, std::vector<Guide>>> guides;
    std::optional<std::pair<AnnotationsSnapshot, AnnotationsSnapshot>> annotations;
    std::optional<std::pair<std::vector<SectionPlane>, std::vector<SectionPlane>>> sections;
    std::optional<std::pair<Frame, Frame>> axes;
    std::optional<std::pair<std::unordered_set<events::EntityRef>, std::unordered_set<events::EntityRef>>> hidden;
    std::optional<std::pair<MaterialsSnapshot, MaterialsSnapshot>> materials;
};

}  // namespace plnr::agent
