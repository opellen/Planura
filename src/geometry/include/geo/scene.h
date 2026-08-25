#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <geo/model.h>
#include <geo/vec3.h>

namespace plnr::geo {

struct SceneJournal;  // geo/journal.h

// Affine 3D transform: a 3x3 linear part plus a translation.
struct Transform {
    // Linear part column-major: col0/col1/col2 are the images of the x/y/z basis vectors.
    Vec3 col0{1.0, 0.0, 0.0};
    Vec3 col1{0.0, 1.0, 0.0};
    Vec3 col2{0.0, 0.0, 1.0};
    Vec3 t{};  // translation, applied after the linear part

    static Transform identity();
    static Transform translation(Vec3 t);

    // Rotation by angleRad radians (right-hand rule) about the line through point along axis.
    // axis is normalized internally; a near-zero axis is a caller error, not handled.
    static Transform rotation(Vec3 point, Vec3 axis, double angleRad);

    // World-axis-aligned scale by (fx, fy, fz) about the fixed point center. A negative factor
    // mirrors that axis and zero collapses it; neither is rejected.
    static Transform scaling(Vec3 center, double fx, double fy, double fz);

    // Reflection across the plane through planePoint with normal planeNormal, normalized
    // internally; a near-zero normal is a caller error, not handled.
    static Transform mirror(Vec3 planePoint, Vec3 planeNormal);

    // Applies the full affine transform (linear part, then translation) to a point.
    Vec3 apply(Vec3 p) const;

    // Linear part only, no translation -- for direction vectors (normals, edge directions).
    Vec3 applyVector(Vec3 v) const;

    // this . inner: composed.apply(p) == this.apply(inner.apply(p)); inner runs first.
    Transform composed(const Transform& inner) const;

    // General affine inverse. The linear part must be non-singular; a singular one asserts.
    Transform inverse() const;

    // Componentwise-within-tol comparison of the linear columns and the translation.
    bool almostEqual(const Transform& other, double tol = kMergeTol) const;
};

// A placed usage of a Definition. Group vs component is a flag on the DEFINITION
// (Definition::isGroup), not here.
struct Instance {
    Id id{};
    Id definitionId{};
    Transform transform;
    std::string name;
};

// Owns geometry (a half-edge Model) plus the instances nested inside it; the root is one of these.
struct Definition {
    Id id{};
    std::string name;
    bool isGroup{};
    Model model;
    std::vector<Instance> children;
};

inline constexpr Id kRootDefinitionId = 1;

// Scene-graph ownership: one always-present root Definition plus every Definition reachable from it
// via Instances. Ids are shared by definitions and instances, monotonic, never reused.
class Scene {
public:
    Scene();

    Definition& root();
    const Definition& root() const;

    // Creates a new, empty-model, non-root definition and returns its id.
    Id createDefinition(std::string name, bool isGroup);

    // Definition lookup by id, root included (kRootDefinitionId resolves to &root()); nullptr if unknown.
    Definition* definition(Id id);
    const Definition* definition(Id id) const;

    // Appends an Instance of childDefId to parentDefId's children and returns its id. kInvalidId,
    // no mutation, if either definition is unknown or childDefId == parentDefId -- a direct
    // self-reference guard only; multi-hop cycles are not checked.
    Id addInstance(Id parentDefId, Id childDefId, Transform transform, std::string name);

    Instance* findInstance(Id parentDefId, Id instanceId);
    const Instance* findInstance(Id parentDefId, Id instanceId) const;

    // Removes the instance from parentDefId's children. False (no-op) if either id is unknown.
    bool removeInstance(Id parentDefId, Id instanceId);

    // Sets an existing instance's transform directly; only existence is checked, no did-it-change
    // guard. Only the transform changes -- name stays as-is. False (no mutation) if
    // parentDefId/instanceId doesn't resolve to an existing Instance.
    bool setInstanceTransform(Id parentDefId, Id instanceId, const Transform& transform);

    // -- Journal --------------------------------------------------------------
    // Attaches (nullptr detaches) a non-owning observer notified of every Definition/Instance
    // create/erase/modify, from ANY mutation path. Mesh events go to Model::setJournal instead.
    void setJournal(SceneJournal* journal);

    // -- Restore APIs -- rebuild a scene from serialized records under their ORIGINAL ids (file
    // loader, undo/redo snapshot restore). Replay verbatim in order: restoreNextId once, then every
    // restoreDefinition (each followed by restoring its own Model), then every restoreInstance.

    // MUST be the first restore* call. Sets nextId_ = nextId (loader passes max(restored id) + 1).
    // False (no mutation) if nextId is below the current nextId_.
    bool restoreNextId(Id nextId);

    // Inserts an empty-model, non-root Definition{id, name, isGroup} and returns it; the caller
    // restores its mesh through the returned Definition's model. nullptr (no mutation) if id is
    // kRootDefinitionId, >= nextId_, or already used by a definition or Instance (shared id space).
    Definition* restoreDefinition(Id id, std::string name, bool isGroup);

    // Appends Instance{id, definitionId, transform, name} to parentDefId's children. False (no
    // mutation) if parentDefId or definitionId is unknown, definitionId == parentDefId, or id is
    // invalid/already used in the shared id space. No multi-hop cycle check, matching addInstance.
    bool restoreInstance(Id parentDefId, Id id, Id definitionId, const Transform& transform, std::string name);

    // -- Rollback primitives -- reverse twins of the restore APIs, for the TransactionManager's undo
    // path. Guards validate before any mutation, a rejected call leaves the scene untouched, and
    // they route through the same choke points as the ordinary mutators, so the journal sees them.

    // Erases Definition{id} only if no Instance anywhere references it, it has no children of its
    // own, and its own model is empty -- validated here rather than trusted from delta ordering.
    // False (no mutation) if id is kRootDefinitionId, unknown, or any precondition fails.
    bool eraseDefinitionForRollback(Id id);

    // Erases the instance from parentDefId's children, same operation as removeInstance. False
    // (no-op) if parentDefId or instanceId is unknown.
    bool eraseInstanceForRollback(Id parentDefId, Id instanceId);

    // Sets an existing instance's transform and name to the given image, with no validation beyond
    // existence (a rollback must be exact). False (no mutation) if parentDefId/instanceId doesn't
    // resolve to an existing Instance.
    bool setInstanceForRollback(Id parentDefId, Id instanceId, const Transform& transform, std::string name);

private:
    Definition root_;
    std::unordered_map<Id, Definition> definitions_;  // non-root definitions
    Id nextId_ = 2;  // 1 is root_; shared by definitions and instances, never reused

    SceneJournal* journal_ = nullptr;  // non-owning, nullable

    // True if id is below nextId_ and unused in the shared definition/instance id space (not
    // kRootDefinitionId, not in definitions_, not an Instance id in any definition's children).
    bool isRestorableId(Id id) const;

    // -- Journal choke points -- every Definition/Instance record insert/erase/modify funnels
    // through these, pairing the mutation with a matching journal_ notification.

    // Inserts Definition{id, name, isGroup} and notifies journal_->definitionCreated.
    Definition* insertDefinitionRecord(Id id, std::string name, bool isGroup);
    // Erases Definition{id} and notifies journal_->definitionDeleted with the captured name/isGroup.
    // Caller guarantees id exists and every precondition already holds.
    void eraseDefinitionRecord(Id id);
    // Appends Instance{id, definitionId, transform, name} to parent.children and notifies
    // journal_->instanceCreated(parent.id, ...).
    Id insertInstanceRecord(Definition& parent, Id id, Id definitionId, Transform transform, std::string name);
    // Erases instanceId from parent.children and notifies journal_->instanceDeleted with its
    // before-image. False (no-op) if instanceId isn't in parent.children.
    bool eraseInstanceRecord(Definition& parent, Id instanceId);
    // Sets inst's transform/name and notifies journal_->instanceModified(parentDefId, inst.id,
    // before..., after...).
    void setInstanceRecord(Instance& inst, Id parentDefId, const Transform& transform, std::string name);
};

}  // namespace plnr::geo
