#include "agent/transaction_manager.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace plnr::agent {

namespace {

// Exact (non-tolerant) equality -- every comparison below runs on two
// plain copies of the same underlying data (before-image vs. after-image),
// never independently-recomputed geometry, so bit-identical equality is safe.
bool vec3Exact(const geo::Vec3& a, const geo::Vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool tagEqual(const Tag& a, const Tag& b) {
    return a.id == b.id && a.name == b.name && a.visible == b.visible;
}

bool tagsSnapshotEqual(const TagsSnapshot& a, const TagsSnapshot& b) {
    if (a.tags.size() != b.tags.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.tags.size(); ++i) {
        if (!tagEqual(a.tags[i], b.tags[i])) {
            return false;
        }
    }
    return a.assignments == b.assignments;  // unordered_map::operator== -- EntityRef/uint64_t both have ==
}

bool guideEqual(const Guide& a, const Guide& b) {
    return a.id == b.id && a.isLine == b.isLine && vec3Exact(a.point, b.point) && vec3Exact(a.dir, b.dir) &&
           a.hidden == b.hidden;
}

bool guidesEqual(const std::vector<Guide>& a, const std::vector<Guide>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!guideEqual(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

bool dimensionEqual(const Dimension& a, const Dimension& b) {
    return a.id == b.id && a.vertexA == b.vertexA && a.vertexB == b.vertexB && vec3Exact(a.offsetDir, b.offsetDir) &&
           a.offset == b.offset && a.overrideText == b.overrideText && a.associated == b.associated &&
           vec3Exact(a.lastA, b.lastA) && vec3Exact(a.lastB, b.lastB);
}

bool textNoteEqual(const TextNote& a, const TextNote& b) {
    return a.id == b.id && a.screenFixed == b.screenFixed && a.screenX == b.screenX && a.screenY == b.screenY &&
           vec3Exact(a.worldAnchor, b.worldAnchor) && a.leaderTarget == b.leaderTarget && a.text == b.text;
}

bool annotationsSnapshotEqual(const AnnotationsSnapshot& a, const AnnotationsSnapshot& b) {
    if (a.dimensions.size() != b.dimensions.size() || a.texts.size() != b.texts.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.dimensions.size(); ++i) {
        if (!dimensionEqual(a.dimensions[i], b.dimensions[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.texts.size(); ++i) {
        if (!textNoteEqual(a.texts[i], b.texts[i])) {
            return false;
        }
    }
    return true;
}

bool sectionPlaneEqual(const SectionPlane& a, const SectionPlane& b) {
    return a.id == b.id && a.name == b.name && vec3Exact(a.point, b.point) && vec3Exact(a.normal, b.normal) &&
           a.active == b.active && a.hidden == b.hidden;
}

bool sectionsEqual(const std::vector<SectionPlane>& a, const std::vector<SectionPlane>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sectionPlaneEqual(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

bool materialEqual(const Material& a, const Material& b) {
    // assetHash/tileW/tileH join this comparison so a texture-set/clear is
    // detected as an aux-diff change too -- without this, commit() would
    // see an identical snapshot and silently push no undo step.
    return a.id == b.id && a.name == b.name && a.r == b.r && a.g == b.g && a.b == b.b && a.opacity == b.opacity &&
           a.assetHash == b.assetHash && a.tileW == b.tileW && a.tileH == b.tileH;
}

bool materialsSnapshotEqual(const MaterialsSnapshot& a, const MaterialsSnapshot& b) {
    if (a.materials.size() != b.materials.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.materials.size(); ++i) {
        if (!materialEqual(a.materials[i], b.materials[i])) {
            return false;
        }
    }
    // MaterialAssignment has no operator== -- compares front/backMaterialId
    // + uvTransform's own 5 fields directly instead, so a UV-transform-only
    // change is also detected as an aux-diff change.
    if (a.assignments.size() != b.assignments.size()) {
        return false;
    }
    for (const auto& [ref, assign] : a.assignments) {
        auto it = b.assignments.find(ref);
        if (it == b.assignments.end() || it->second.frontMaterialId != assign.frontMaterialId ||
            it->second.backMaterialId != assign.backMaterialId ||
            it->second.uvTransform.offsetU != assign.uvTransform.offsetU ||
            it->second.uvTransform.offsetV != assign.uvTransform.offsetV ||
            it->second.uvTransform.rotationRad != assign.uvTransform.rotationRad ||
            it->second.uvTransform.scaleU != assign.uvTransform.scaleU ||
            it->second.uvTransform.scaleV != assign.uvTransform.scaleV) {
            return false;
        }
    }
    return true;
}

// Discards adjacent VertexCreated/VertexDeleted pairs for the same
// (definitionId, id) -- the shape a rejected geo::Model::addEdge leaves
// behind, else it would push a pointless undo step. Runs to a fixed point.
void pruneTransientVertexPairs(std::vector<TransactionOp>& ops) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i + 1 < ops.size(); ++i) {
            const TransactionOp& created = ops[i];
            const TransactionOp& deletedOp = ops[i + 1];
            if (created.kind == TransactionOpKind::VertexCreated && deletedOp.kind == TransactionOpKind::VertexDeleted &&
                created.definitionId == deletedOp.definitionId && created.id == deletedOp.id) {
                ops.erase(ops.begin() + static_cast<std::ptrdiff_t>(i), ops.begin() + static_cast<std::ptrdiff_t>(i) + 2);
                changed = true;
                break;  // restart the scan -- the removal may expose a new adjacency earlier in the list
            }
        }
    }
}

// Fills TransactionOp::definitionModelNextId for every DefinitionCreated/
// DefinitionDeleted op: max id/idA/idB/loop entry over every other op
// tagged with this op's own id as definitionId, plus 1. O(ops^2), negligible
// at one-gesture scale.
void fillDefinitionNextIdReservations(std::vector<TransactionOp>& ops) {
    for (TransactionOp& op : ops) {
        if (op.kind != TransactionOpKind::DefinitionCreated && op.kind != TransactionOpKind::DefinitionDeleted) {
            continue;
        }
        geo::Id maxId = geo::kInvalidId;
        for (const TransactionOp& other : ops) {
            if (other.definitionId != op.id) {
                continue;  // only ops that happened inside THIS definition's own Model
            }
            switch (other.kind) {
                case TransactionOpKind::VertexCreated:
                case TransactionOpKind::VertexMoved:
                case TransactionOpKind::VertexDeleted:
                case TransactionOpKind::EdgeCreated:
                case TransactionOpKind::EdgeDeleted:
                case TransactionOpKind::FaceCreated:
                case TransactionOpKind::FaceDeleted:
                case TransactionOpKind::FaceLoopModified:
                    maxId = std::max({maxId, other.id, other.idA, other.idB});
                    for (geo::Id v : other.loopA) {
                        maxId = std::max(maxId, v);
                    }
                    for (geo::Id v : other.loopB) {
                        maxId = std::max(maxId, v);
                    }
                    break;
                default:
                    break;  // Definition*/Instance* ops don't name Model-space ids
            }
        }
        op.definitionModelNextId = maxId + 1;
    }
}

// Resolves op's owning geo::Model (root or a definition). nullptr only
// defensively -- a captured delta's own definitionId always named a live
// definition, and the ordered replay contract keeps it that way.
geo::Model* resolveModel(geo::Scene& scene, geo::Id definitionId) {
    geo::Definition* def = scene.definition(definitionId);
    return def != nullptr ? &def->model : nullptr;
}

// Forward apply = "redo": created -> restore* (insert under recorded
// id/values), deleted -> erase*ForRollback, modified -> apply AFTER image.
// Mirrors geo/journal_test.cpp's own applyForward, extended with Scene kinds.
bool applyOpForward(geo::Scene& scene, const TransactionOp& op) {
    switch (op.kind) {
        case TransactionOpKind::VertexCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreVertex(op.id, op.posA);
        }
        case TransactionOpKind::VertexMoved: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->setVertexPosForRollback(op.id, op.posB);
        }
        case TransactionOpKind::VertexDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseVertexForRollback(op.id);
        }
        case TransactionOpKind::EdgeCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreEdge(op.id, op.idA, op.idB);
        }
        case TransactionOpKind::EdgeDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseEdgeForRollback(op.id);
        }
        case TransactionOpKind::FaceCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreFace(op.id, op.loopA);
        }
        case TransactionOpKind::FaceDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseFaceForRollback(op.id);
        }
        case TransactionOpKind::FaceLoopModified: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseFaceForRollback(op.id) && m->restoreFace(op.id, op.loopB);
        }
        case TransactionOpKind::DefinitionCreated: {
            // restoreDefinition builds a brand-new, empty Model -- reserve
            // it up to definitionModelNextId BEFORE any mesh op below tries
            // to restore into it.
            geo::Definition* def = scene.restoreDefinition(op.id, op.nameA, op.isGroup);
            return def != nullptr && def->model.restoreNextId(op.definitionModelNextId);
        }
        case TransactionOpKind::DefinitionDeleted:
            return scene.eraseDefinitionForRollback(op.id);
        case TransactionOpKind::InstanceCreated:
            return scene.restoreInstance(op.definitionId, op.id, op.targetDefinitionId, op.transformA, op.nameA);
        case TransactionOpKind::InstanceDeleted:
            return scene.eraseInstanceForRollback(op.definitionId, op.id);
        case TransactionOpKind::InstanceModified:
            return scene.setInstanceForRollback(op.definitionId, op.id, op.transformB, op.nameB);
    }
    return false;  // unreachable -- TransactionOpKind is exhaustively handled above
}

// Backward apply = the "undo" direction: created -> erase*ForRollback,
// deleted -> restore* (the before-image), modified -> apply the BEFORE
// image. Mirrors geo/journal_test.cpp's own applyBackward.
bool applyOpBackward(geo::Scene& scene, const TransactionOp& op) {
    switch (op.kind) {
        case TransactionOpKind::VertexCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseVertexForRollback(op.id);
        }
        case TransactionOpKind::VertexMoved: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->setVertexPosForRollback(op.id, op.posA);
        }
        case TransactionOpKind::VertexDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreVertex(op.id, op.posA);
        }
        case TransactionOpKind::EdgeCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseEdgeForRollback(op.id);
        }
        case TransactionOpKind::EdgeDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreEdge(op.id, op.idA, op.idB);
        }
        case TransactionOpKind::FaceCreated: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseFaceForRollback(op.id);
        }
        case TransactionOpKind::FaceDeleted: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->restoreFace(op.id, op.loopA);
        }
        case TransactionOpKind::FaceLoopModified: {
            geo::Model* m = resolveModel(scene, op.definitionId);
            return m != nullptr && m->eraseFaceForRollback(op.id) && m->restoreFace(op.id, op.loopA);
        }
        case TransactionOpKind::DefinitionCreated:
            return scene.eraseDefinitionForRollback(op.id);
        case TransactionOpKind::DefinitionDeleted: {
            // Symmetric to applyOpForward's DefinitionCreated case above.
            // Unreachable via any current live mutator (nothing
            // forward-deletes a definition today), kept for completeness.
            geo::Definition* def = scene.restoreDefinition(op.id, op.nameA, op.isGroup);
            return def != nullptr && def->model.restoreNextId(op.definitionModelNextId);
        }
        case TransactionOpKind::InstanceCreated:
            return scene.eraseInstanceForRollback(op.definitionId, op.id);
        case TransactionOpKind::InstanceDeleted:
            return scene.restoreInstance(op.definitionId, op.id, op.targetDefinitionId, op.transformA, op.nameA);
        case TransactionOpKind::InstanceModified:
            return scene.setInstanceForRollback(op.definitionId, op.id, op.transformA, op.nameA);
    }
    return false;  // unreachable -- TransactionOpKind is exhaustively handled above
}

}  // namespace

void applyTransactionDelta(GeometryApi& geometry, TagStore* tags, GuideStore* guides, AnnotationStore* annotations,
                            SectionStore* sections, AxesStore* axes, MaterialRepository* materials,
                            const TransactionDelta& delta, ApplyDirection direction) {
    geo::Scene& scene = geometry.sceneForUndoRollback();

    // Ordered mesh/scene replay -- order preservation is semantic: backward
    // = reverse iteration applying each op's inverse image, forward =
    // forward iteration applying each op's direct image.
    if (direction == ApplyDirection::Backward) {
        for (auto it = delta.ops.rbegin(); it != delta.ops.rend(); ++it) {
            const bool ok = applyOpBackward(scene, *it);
            assert(ok && "TransactionDelta backward replay rejected an op it itself captured");
            (void)ok;
        }
    } else {
        for (const TransactionOp& op : delta.ops) {
            const bool ok = applyOpForward(scene, op);
            assert(ok && "TransactionDelta forward replay rejected an op it itself captured");
            (void)ok;
        }
    }

    const bool backward = direction == ApplyDirection::Backward;

    if (delta.tags && tags) {
        const TagsSnapshot& snap = backward ? delta.tags->first : delta.tags->second;
        tags->clearForRestore();
        for (const Tag& t : snap.tags) {
            tags->restoreTag(t);
        }
        for (const auto& [ref, tagId] : snap.assignments) {
            tags->restoreAssignment(ref, tagId);
        }
    }
    if (delta.guides && guides) {
        const std::vector<Guide>& snap = backward ? delta.guides->first : delta.guides->second;
        guides->clearForRestore();
        for (const Guide& g : snap) {
            guides->restoreGuide(g);
        }
    }
    if (delta.annotations && annotations) {
        const AnnotationsSnapshot& snap = backward ? delta.annotations->first : delta.annotations->second;
        annotations->clearForRestore();
        for (const Dimension& d : snap.dimensions) {
            annotations->restoreDimension(d);
        }
        for (const TextNote& t : snap.texts) {
            annotations->restoreTextNote(t);
        }
    }
    if (delta.sections && sections) {
        const std::vector<SectionPlane>& snap = backward ? delta.sections->first : delta.sections->second;
        sections->clearForRestore();
        for (const SectionPlane& p : snap) {
            sections->restorePlane(p);
        }
    }
    if (delta.axes && axes) {
        axes->restoreFrame(backward ? delta.axes->first : delta.axes->second);
    }
    if (delta.materials && materials) {
        const MaterialsSnapshot& snap = backward ? delta.materials->first : delta.materials->second;
        materials->clearForRestore();
        for (const Material& m : snap.materials) {
            materials->restoreMaterial(m);
        }
        for (const auto& [ref, assign] : snap.assignments) {
            // 4-arg overload -- full-fidelity replay needs assign's own
            // uvTransform too, not just front/back. The ONE place a
            // captured snapshot gets replayed onto the live agent.
            materials->restoreAssignment(ref, assign.frontMaterialId, assign.backMaterialId, assign.uvTransform);
        }
    }
    if (delta.hidden) {
        const std::unordered_set<events::EntityRef>& snap = backward ? delta.hidden->first : delta.hidden->second;
        geometry.restoreHidden(std::vector<events::EntityRef>(snap.begin(), snap.end()));
    }
}

TransactionManager::TransactionManager(ordo::core::AppKernel& kernel)
    : geometry_(kernel.agentAs<GeometryApi>(kGeometryApiName)),
      tags_(kernel.agentAs<TagStore>(kTagStoreName)),
      guides_(kernel.agentAs<GuideStore>(kGuideStoreName)),
      annotations_(kernel.agentAs<AnnotationStore>(kAnnotationStoreName)),
      sections_(kernel.agentAs<SectionStore>(kSectionStoreName)),
      axes_(kernel.agentAs<AxesStore>(kAxesStoreName)),
      materials_(kernel.agentAs<MaterialRepository>(kMaterialRepositoryName)),
      undo_(kernel.agentAs<UndoStore>(kUndoStoreName)),
      rootModelJournal_(*this, geo::kRootDefinitionId),
      sceneJournal_(*this) {}

TransactionManager::~TransactionManager() {
    if (isActive_) {
        detachJournals();
    }
}

void TransactionManager::begin() {
    if (!geometry_ || !undo_) {
        isActive_ = false;  // null-agent-safe: nothing to capture into
        return;
    }

    isActive_ = true;
    ops_.clear();
    definitionJournals_.clear();  // should already be empty (previous commit()/abort() clears it too) -- defensive

    tagsTouched_ = guidesTouched_ = annotationsTouched_ = sectionsTouched_ = axesTouched_ = materialsTouched_ =
        hiddenTouched_ = false;

    if (tags_) {
        tagsBefore_ = TagsSnapshot{tags_->tags(), tags_->assignments()};
    }
    if (guides_) {
        guidesBefore_ = guides_->guides();
    }
    if (annotations_) {
        annotationsBefore_ = AnnotationsSnapshot{annotations_->dimensions(), annotations_->texts()};
    }
    if (sections_) {
        sectionsBefore_ = sections_->planes();
    }
    if (axes_) {
        axesBefore_ = axes_->frame();
    }
    if (materials_) {
        materialsBefore_ = MaterialsSnapshot{materials_->materials(), materials_->assignments()};
    }
    hiddenBefore_ = geometry_->hidden();

    scene_ = &geometry_->sceneForUndoRollback();
    scene_->setJournal(&sceneJournal_);
    scene_->root().model.setJournal(&rootModelJournal_);

    undo_->setActiveTransaction(this);
}

void TransactionManager::commit() {
    if (!isActive_) {
        return;
    }

    detachJournals();

    TransactionDelta delta;
    delta.ops = std::move(ops_);
    pruneTransientVertexPairs(delta.ops);
    fillDefinitionNextIdReservations(delta.ops);

    bool auxChanged = false;

    if (tags_ && tagsTouched_) {
        TagsSnapshot after{tags_->tags(), tags_->assignments()};
        if (!tagsSnapshotEqual(tagsBefore_, after)) {
            delta.tags = std::make_pair(std::move(tagsBefore_), std::move(after));
            auxChanged = true;
        }
    }
    if (guides_ && guidesTouched_) {
        std::vector<Guide> after = guides_->guides();
        if (!guidesEqual(guidesBefore_, after)) {
            delta.guides = std::make_pair(std::move(guidesBefore_), std::move(after));
            auxChanged = true;
        }
    }
    if (annotations_ && annotationsTouched_) {
        AnnotationsSnapshot after{annotations_->dimensions(), annotations_->texts()};
        if (!annotationsSnapshotEqual(annotationsBefore_, after)) {
            delta.annotations = std::make_pair(std::move(annotationsBefore_), std::move(after));
            auxChanged = true;
        }
    }
    if (sections_ && sectionsTouched_) {
        std::vector<SectionPlane> after = sections_->planes();
        if (!sectionsEqual(sectionsBefore_, after)) {
            delta.sections = std::make_pair(std::move(sectionsBefore_), std::move(after));
            auxChanged = true;
        }
    }
    if (axes_ && axesTouched_) {
        const Frame after = axes_->frame();
        if (!(after == axesBefore_)) {
            delta.axes = std::make_pair(axesBefore_, after);
            auxChanged = true;
        }
    }
    if (materials_ && materialsTouched_) {
        MaterialsSnapshot after{materials_->materials(), materials_->assignments()};
        if (!materialsSnapshotEqual(materialsBefore_, after)) {
            delta.materials = std::make_pair(std::move(materialsBefore_), std::move(after));
            auxChanged = true;
        }
    }
    if (hiddenTouched_) {
        std::unordered_set<events::EntityRef> after = geometry_->hidden();
        if (hiddenBefore_ != after) {
            delta.hidden = std::make_pair(std::move(hiddenBefore_), std::move(after));
            auxChanged = true;
        }
    }

    if (!delta.ops.empty() || auxChanged) {
        undo_->push(std::move(delta));
    }
    // else: nothing changed -- discard, no step pushed.

    isActive_ = false;
}

void TransactionManager::abort() {
    if (!isActive_) {
        return;
    }

    TransactionDelta delta;
    delta.ops = std::move(ops_);
    fillDefinitionNextIdReservations(delta.ops);
    // Backward application only ever reads .first of each pair (see
    // applyTransactionDelta above) -- .second is left default-constructed
    // and never touched.
    if (tags_ && tagsTouched_) {
        delta.tags = std::make_pair(tagsBefore_, TagsSnapshot{});
    }
    if (guides_ && guidesTouched_) {
        delta.guides = std::make_pair(guidesBefore_, std::vector<Guide>{});
    }
    if (annotations_ && annotationsTouched_) {
        delta.annotations = std::make_pair(annotationsBefore_, AnnotationsSnapshot{});
    }
    if (sections_ && sectionsTouched_) {
        delta.sections = std::make_pair(sectionsBefore_, std::vector<SectionPlane>{});
    }
    if (axes_ && axesTouched_) {
        delta.axes = std::make_pair(axesBefore_, Frame{});
    }
    if (materials_ && materialsTouched_) {
        delta.materials = std::make_pair(materialsBefore_, MaterialsSnapshot{});
    }
    if (hiddenTouched_) {
        delta.hidden = std::make_pair(hiddenBefore_, std::unordered_set<events::EntityRef>{});
    }

    // Journals off FIRST -- the backward replay below must never re-record
    // itself (same structural guarantee UndoCommand/RedoCommand rely on).
    detachJournals();

    applyTransactionDelta(*geometry_, tags_.get(), guides_.get(), annotations_.get(), sections_.get(), axes_.get(),
                           materials_.get(), delta, ApplyDirection::Backward);

    isActive_ = false;
}

void TransactionManager::markStoreTouched(AuxStore which) {
    if (!isActive_) {
        return;
    }
    switch (which) {
        case AuxStore::Tags:
            tagsTouched_ = true;
            break;
        case AuxStore::Guides:
            guidesTouched_ = true;
            break;
        case AuxStore::Annotations:
            annotationsTouched_ = true;
            break;
        case AuxStore::Sections:
            sectionsTouched_ = true;
            break;
        case AuxStore::Axes:
            axesTouched_ = true;
            break;
        case AuxStore::Materials:
            materialsTouched_ = true;
            break;
        case AuxStore::Hidden:
            hiddenTouched_ = true;
            break;
    }
}

void TransactionManager::markAllAuxTouched() {
    if (!isActive_) {
        return;
    }
    tagsTouched_ = guidesTouched_ = annotationsTouched_ = sectionsTouched_ = axesTouched_ = materialsTouched_ =
        hiddenTouched_ = true;
}

void TransactionManager::pushOp(TransactionOp op) {
    ops_.push_back(std::move(op));
}

void TransactionManager::attachDefinitionJournal(geo::Id definitionId) {
    if (scene_ == nullptr) {
        return;  // defensive: shouldn't happen -- only called while isActive_ (see CapturingSceneJournal below)
    }
    geo::Definition* def = scene_->definition(definitionId);
    if (def == nullptr) {
        return;  // defensive: shouldn't happen -- definitionId was just created by the same call that notified us
    }
    auto journal = std::make_unique<CapturingModelJournal>(*this, definitionId);
    def->model.setJournal(journal.get());
    definitionJournals_.emplace_back(definitionId, std::move(journal));
}

void TransactionManager::detachJournals() {
    if (scene_ != nullptr) {
        scene_->setJournal(nullptr);
        scene_->root().model.setJournal(nullptr);
        for (auto& [defId, journal] : definitionJournals_) {
            geo::Definition* def = scene_->definition(defId);
            if (def != nullptr) {
                def->model.setJournal(nullptr);
            }
        }
    }
    definitionJournals_.clear();
    scene_ = nullptr;
    if (undo_) {
        undo_->setActiveTransaction(nullptr);
    }
}

// -- CapturingModelJournal ---------------------------------------------------

void TransactionManager::CapturingModelJournal::vertexCreated(geo::Id id, geo::Vec3 pos) {
    TransactionOp op;
    op.kind = TransactionOpKind::VertexCreated;
    op.definitionId = definitionId_;
    op.id = id;
    op.posA = pos;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::vertexMoved(geo::Id id, geo::Vec3 before, geo::Vec3 after) {
    TransactionOp op;
    op.kind = TransactionOpKind::VertexMoved;
    op.definitionId = definitionId_;
    op.id = id;
    op.posA = before;
    op.posB = after;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::vertexDeleted(geo::Id id, geo::Vec3 posBefore) {
    TransactionOp op;
    op.kind = TransactionOpKind::VertexDeleted;
    op.definitionId = definitionId_;
    op.id = id;
    op.posA = posBefore;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::edgeCreated(geo::Id id, geo::Id v0, geo::Id v1) {
    TransactionOp op;
    op.kind = TransactionOpKind::EdgeCreated;
    op.definitionId = definitionId_;
    op.id = id;
    op.idA = v0;
    op.idB = v1;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::edgeDeleted(geo::Id id, geo::Id v0, geo::Id v1) {
    TransactionOp op;
    op.kind = TransactionOpKind::EdgeDeleted;
    op.definitionId = definitionId_;
    op.id = id;
    op.idA = v0;
    op.idB = v1;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::faceCreated(geo::Id id, const std::vector<geo::Id>& loop) {
    TransactionOp op;
    op.kind = TransactionOpKind::FaceCreated;
    op.definitionId = definitionId_;
    op.id = id;
    op.loopA = loop;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::faceDeleted(geo::Id id, const std::vector<geo::Id>& loopBefore) {
    TransactionOp op;
    op.kind = TransactionOpKind::FaceDeleted;
    op.definitionId = definitionId_;
    op.id = id;
    op.loopA = loopBefore;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingModelJournal::faceLoopModified(geo::Id id, const std::vector<geo::Id>& loopBefore,
                                                                   const std::vector<geo::Id>& loopAfter) {
    TransactionOp op;
    op.kind = TransactionOpKind::FaceLoopModified;
    op.definitionId = definitionId_;
    op.id = id;
    op.loopA = loopBefore;
    op.loopB = loopAfter;
    owner_.pushOp(std::move(op));
}

// -- CapturingSceneJournal ----------------------------------------------------

void TransactionManager::CapturingSceneJournal::definitionCreated(geo::Id id, const std::string& name, bool isGroup) {
    TransactionOp op;
    op.kind = TransactionOpKind::DefinitionCreated;
    op.id = id;
    op.nameA = name;
    op.isGroup = isGroup;
    owner_.pushOp(std::move(op));

    // Attach a journal to the new definition's own Model NOW -- any mesh
    // write into it for the rest of this transaction (e.g. makeGroup's
    // copyInto, right after createDefinition) must be captured too.
    owner_.attachDefinitionJournal(id);
}

void TransactionManager::CapturingSceneJournal::definitionDeleted(geo::Id id, const std::string& name, bool isGroup) {
    TransactionOp op;
    op.kind = TransactionOpKind::DefinitionDeleted;
    op.id = id;
    op.nameA = name;
    op.isGroup = isGroup;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingSceneJournal::instanceCreated(geo::Id parentDefId, geo::Id id, geo::Id definitionId,
                                                                  const geo::Transform& transform,
                                                                  const std::string& name) {
    TransactionOp op;
    op.kind = TransactionOpKind::InstanceCreated;
    op.definitionId = parentDefId;
    op.id = id;
    op.targetDefinitionId = definitionId;
    op.transformA = transform;
    op.nameA = name;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingSceneJournal::instanceDeleted(geo::Id parentDefId, geo::Id id, geo::Id definitionId,
                                                                  const geo::Transform& transformBefore,
                                                                  const std::string& nameBefore) {
    TransactionOp op;
    op.kind = TransactionOpKind::InstanceDeleted;
    op.definitionId = parentDefId;
    op.id = id;
    op.targetDefinitionId = definitionId;
    op.transformA = transformBefore;
    op.nameA = nameBefore;
    owner_.pushOp(std::move(op));
}

void TransactionManager::CapturingSceneJournal::instanceModified(geo::Id parentDefId, geo::Id id,
                                                                   const geo::Transform& before, const geo::Transform& after,
                                                                   const std::string& nameBefore,
                                                                   const std::string& nameAfter) {
    TransactionOp op;
    op.kind = TransactionOpKind::InstanceModified;
    op.definitionId = parentDefId;
    op.id = id;
    op.transformA = before;
    op.transformB = after;
    op.nameA = nameBefore;
    op.nameB = nameAfter;
    owner_.pushOp(std::move(op));
}

}  // namespace plnr::agent
