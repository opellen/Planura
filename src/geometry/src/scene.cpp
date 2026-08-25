#include <geo/scene.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

#include <geo/journal.h>

namespace plnr::geo {

Transform Transform::identity() {
    return Transform{};
}

Transform Transform::translation(Vec3 t) {
    Transform result;
    result.t = t;
    return result;
}

Transform Transform::rotation(Vec3 point, Vec3 axis, double angleRad) {
    const Vec3 n = normalized(axis);
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    // Rodrigues' rotation formula: R v = v*c + (n x v)*s + n*(n . v)*(1 - c).
    const auto rotate = [&](Vec3 v) { return v * c + cross(n, v) * s + n * (dot(n, v) * (1.0 - c)); };

    Transform result;
    result.col0 = rotate(Vec3{1.0, 0.0, 0.0});
    result.col1 = rotate(Vec3{0.0, 1.0, 0.0});
    result.col2 = rotate(Vec3{0.0, 0.0, 1.0});
    // point is the fixed point: apply(point) == point, so t = point - R*point.
    result.t = point - rotate(point);
    return result;
}

Transform Transform::scaling(Vec3 center, double fx, double fy, double fz) {
    Transform result;
    result.col0 = Vec3{fx, 0.0, 0.0};
    result.col1 = Vec3{0.0, fy, 0.0};
    result.col2 = Vec3{0.0, 0.0, fz};
    // center is the fixed point: apply(center) == center, so t = center - S*center.
    result.t = center - result.applyVector(center);
    return result;
}

Transform Transform::mirror(Vec3 planePoint, Vec3 planeNormal) {
    const Vec3 n = normalized(planeNormal);
    // Householder reflection: R v = v - 2*(n . v)*n.
    const auto reflect = [&](Vec3 v) { return v - n * (2.0 * dot(n, v)); };

    Transform result;
    result.col0 = reflect(Vec3{1.0, 0.0, 0.0});
    result.col1 = reflect(Vec3{0.0, 1.0, 0.0});
    result.col2 = reflect(Vec3{0.0, 0.0, 1.0});
    // planePoint is the fixed point: apply(planePoint) == planePoint, so
    // t = planePoint - R*planePoint.
    result.t = planePoint - reflect(planePoint);
    return result;
}

Vec3 Transform::apply(Vec3 p) const {
    return col0 * p.x + col1 * p.y + col2 * p.z + t;
}

Vec3 Transform::applyVector(Vec3 v) const {
    return col0 * v.x + col1 * v.y + col2 * v.z;
}

Transform Transform::composed(const Transform& inner) const {
    Transform result;
    result.col0 = applyVector(inner.col0);
    result.col1 = applyVector(inner.col1);
    result.col2 = applyVector(inner.col2);
    result.t = applyVector(inner.t) + t;
    return result;
}

Transform Transform::inverse() const {
    // Reciprocal-basis construction: if M = [col0 col1 col2], the rows of
    // M^-1 are (col1 x col2)/det, (col2 x col0)/det, (col0 x col1)/det (each
    // row is, by construction, orthogonal to the two columns it wasn't built
    // from and has unit dot product with the remaining one).
    const Vec3 r0 = cross(col1, col2);
    const Vec3 r1 = cross(col2, col0);
    const Vec3 r2 = cross(col0, col1);
    const double det = dot(col0, r0);
    assert(std::abs(det) > kEps && "Transform::inverse: singular linear part");

    const double invDet = 1.0 / det;
    const Vec3 row0 = r0 * invDet;
    const Vec3 row1 = r1 * invDet;
    const Vec3 row2 = r2 * invDet;

    Transform result;
    // Transpose the rows just computed into columns -- Transform agents the
    // linear part by its columns, not its rows.
    result.col0 = Vec3{row0.x, row1.x, row2.x};
    result.col1 = Vec3{row0.y, row1.y, row2.y};
    result.col2 = Vec3{row0.z, row1.z, row2.z};
    // apply(p) = M p + t, so the inverse map is p = M^-1 y - M^-1 t.
    result.t = -result.applyVector(t);
    return result;
}

bool Transform::almostEqual(const Transform& other, double tol) const {
    return geo::almostEqual(col0, other.col0, tol) && geo::almostEqual(col1, other.col1, tol) &&
           geo::almostEqual(col2, other.col2, tol) && geo::almostEqual(t, other.t, tol);
}

Scene::Scene() {
    root_.id = kRootDefinitionId;
    root_.name = "Model";
    root_.isGroup = false;
}

Definition& Scene::root() {
    return root_;
}

const Definition& Scene::root() const {
    return root_;
}

void Scene::setJournal(SceneJournal* journal) {
    journal_ = journal;
}

// -- Journal choke points -- see these methods' own declarations in scene.h for the discipline.

Definition* Scene::insertDefinitionRecord(Id id, std::string name, bool isGroup) {
    Definition def;
    def.id = id;
    def.name = std::move(name);
    def.isGroup = isGroup;
    auto [it, inserted] = definitions_.emplace(id, std::move(def));
    if (journal_) {
        journal_->definitionCreated(it->second.id, it->second.name, it->second.isGroup);
    }
    return &it->second;
}

void Scene::eraseDefinitionRecord(Id id) {
    const auto it = definitions_.find(id);
    if (it == definitions_.end()) {
        return;  // defensive: caller guarantees existence
    }
    const std::string name = it->second.name;
    const bool isGroup = it->second.isGroup;
    definitions_.erase(it);
    if (journal_) {
        journal_->definitionDeleted(id, name, isGroup);
    }
}

Id Scene::insertInstanceRecord(Definition& parent, Id id, Id definitionId, Transform transform, std::string name) {
    Instance inst;
    inst.id = id;
    inst.definitionId = definitionId;
    inst.transform = transform;
    inst.name = std::move(name);
    parent.children.push_back(inst);
    if (journal_) {
        journal_->instanceCreated(parent.id, inst.id, inst.definitionId, inst.transform, inst.name);
    }
    return id;
}

bool Scene::eraseInstanceRecord(Definition& parent, Id instanceId) {
    auto it = std::find_if(parent.children.begin(), parent.children.end(),
                            [instanceId](const Instance& inst) { return inst.id == instanceId; });
    if (it == parent.children.end()) {
        return false;
    }
    const Instance before = *it;
    parent.children.erase(it);
    if (journal_) {
        journal_->instanceDeleted(parent.id, before.id, before.definitionId, before.transform, before.name);
    }
    return true;
}

void Scene::setInstanceRecord(Instance& inst, Id parentDefId, const Transform& transform, std::string name) {
    const Transform before = inst.transform;
    const std::string nameBefore = inst.name;
    inst.transform = transform;
    inst.name = std::move(name);
    if (journal_) {
        journal_->instanceModified(parentDefId, inst.id, before, inst.transform, nameBefore, inst.name);
    }
}

Id Scene::createDefinition(std::string name, bool isGroup) {
    const Id id = nextId_++;
    insertDefinitionRecord(id, std::move(name), isGroup);
    return id;
}

Definition* Scene::definition(Id id) {
    if (id == kRootDefinitionId) {
        return &root_;
    }
    auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}

const Definition* Scene::definition(Id id) const {
    if (id == kRootDefinitionId) {
        return &root_;
    }
    auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}

Id Scene::addInstance(Id parentDefId, Id childDefId, Transform transform, std::string name) {
    if (parentDefId == childDefId) {
        // Direct self-reference guard only, see header comment.
        return kInvalidId;
    }
    Definition* parent = definition(parentDefId);
    const Definition* child = definition(childDefId);
    if (parent == nullptr || child == nullptr) {
        return kInvalidId;
    }

    const Id id = nextId_++;
    return insertInstanceRecord(*parent, id, childDefId, transform, std::move(name));
}

Instance* Scene::findInstance(Id parentDefId, Id instanceId) {
    Definition* parent = definition(parentDefId);
    if (parent == nullptr) {
        return nullptr;
    }
    for (Instance& inst : parent->children) {
        if (inst.id == instanceId) {
            return &inst;
        }
    }
    return nullptr;
}

const Instance* Scene::findInstance(Id parentDefId, Id instanceId) const {
    const Definition* parent = definition(parentDefId);
    if (parent == nullptr) {
        return nullptr;
    }
    for (const Instance& inst : parent->children) {
        if (inst.id == instanceId) {
            return &inst;
        }
    }
    return nullptr;
}

bool Scene::isRestorableId(Id id) const {
    if (id == kInvalidId || id == kRootDefinitionId || id >= nextId_) {
        return false;
    }
    if (definitions_.find(id) != definitions_.end()) {
        return false;
    }
    // Shared id space with instances -- check every definition's children
    // for a clashing instance id, root included.
    const auto usedAsInstance = [id](const Definition& def) {
        return std::any_of(def.children.begin(), def.children.end(),
                            [id](const Instance& inst) { return inst.id == id; });
    };
    if (usedAsInstance(root_)) {
        return false;
    }
    for (const auto& [defId, def] : definitions_) {
        if (usedAsInstance(def)) {
            return false;
        }
    }
    return true;
}

bool Scene::restoreNextId(Id nextId) {
    if (nextId < nextId_) {
        return false;  // restoring backward could collide with ids already in use
    }
    nextId_ = nextId;
    return true;
}

Definition* Scene::restoreDefinition(Id id, std::string name, bool isGroup) {
    if (!isRestorableId(id)) {
        return nullptr;
    }
    return insertDefinitionRecord(id, std::move(name), isGroup);
}

bool Scene::restoreInstance(Id parentDefId, Id id, Id definitionId, const Transform& transform, std::string name) {
    if (definitionId == parentDefId) {
        // Direct self-reference guard only, same as addInstance.
        return false;
    }
    Definition* parent = definition(parentDefId);
    const Definition* target = definition(definitionId);
    if (parent == nullptr || target == nullptr) {
        return false;
    }
    if (!isRestorableId(id)) {
        return false;
    }

    insertInstanceRecord(*parent, id, definitionId, transform, std::move(name));
    return true;
}

bool Scene::removeInstance(Id parentDefId, Id instanceId) {
    Definition* parent = definition(parentDefId);
    if (parent == nullptr) {
        return false;
    }
    return eraseInstanceRecord(*parent, instanceId);
}

bool Scene::setInstanceTransform(Id parentDefId, Id instanceId, const Transform& transform) {
    Instance* inst = findInstance(parentDefId, instanceId);
    if (inst == nullptr) {
        return false;
    }
    setInstanceRecord(*inst, parentDefId, transform, inst->name);
    return true;
}

// -- Rollback primitives -- see these methods' own declarations in scene.h for the full contract.

bool Scene::eraseDefinitionForRollback(Id id) {
    if (id == kInvalidId || id == kRootDefinitionId) {
        return false;
    }
    const auto it = definitions_.find(id);
    if (it == definitions_.end()) {
        return false;
    }
    const Definition& def = it->second;
    if (!def.model.vertices().empty() || !def.model.edges().empty() || !def.model.faces().empty()) {
        return false;  // its own model isn't empty
    }
    if (!def.children.empty()) {
        return false;  // it still has its own instances
    }
    // No instance anywhere else in the scene references it either.
    const auto referencedBy = [id](const Definition& d) {
        return std::any_of(d.children.begin(), d.children.end(),
                            [id](const Instance& inst) { return inst.definitionId == id; });
    };
    if (referencedBy(root_)) {
        return false;
    }
    for (const auto& [defId, d] : definitions_) {
        if (defId != id && referencedBy(d)) {
            return false;
        }
    }

    eraseDefinitionRecord(id);
    return true;
}

bool Scene::eraseInstanceForRollback(Id parentDefId, Id instanceId) {
    Definition* parent = definition(parentDefId);
    if (parent == nullptr) {
        return false;
    }
    return eraseInstanceRecord(*parent, instanceId);
}

bool Scene::setInstanceForRollback(Id parentDefId, Id instanceId, const Transform& transform, std::string name) {
    Instance* inst = findInstance(parentDefId, instanceId);
    if (inst == nullptr) {
        return false;
    }
    setInstanceRecord(*inst, parentDefId, transform, std::move(name));
    return true;
}

}  // namespace plnr::geo
