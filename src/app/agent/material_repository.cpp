#include "agent/material_repository.h"

#include <string>
#include <utility>

namespace plnr::agent {

MaterialRepository::MaterialRepository() : Agent(std::string(kMaterialRepositoryName)) {}

Material* MaterialRepository::findMaterial(geo::Id id) {
    for (Material& m : materials_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const Material* MaterialRepository::findMaterial(geo::Id id) const {
    for (const Material& m : materials_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

geo::Id MaterialRepository::create(std::string name, double r, double g, double b, double opacity) {
    const geo::Id id = nextId_++;
    if (name.empty()) {
        name = "Material " + std::to_string(id);
    }
    materials_.push_back(Material{id, std::move(name), r, g, b, opacity});
    send(events::MaterialsChanged{});
    return id;
}

bool MaterialRepository::edit(geo::Id id, std::string name, double r, double g, double b, double opacity) {
    Material* m = findMaterial(id);
    if (m == nullptr) {
        return false;  // unknown material id
    }
    if (m->name == name && m->r == r && m->g == g && m->b == b && m->opacity == opacity) {
        return false;  // every field already matches -- no-op
    }

    m->name = std::move(name);
    m->r = r;
    m->g = g;
    m->b = b;
    m->opacity = opacity;
    send(events::MaterialsChanged{});
    return true;
}

bool MaterialRepository::paint(const std::vector<events::EntityRef>& refs, geo::Id materialId) {
    if (materialId != 0 && findMaterial(materialId) == nullptr) {
        return false;  // unknown material id -- whole call rejected
    }

    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        auto it = assignments_.find(ref);
        const bool exists = it != assignments_.end();
        const geo::Id currentFront = exists ? it->second.frontMaterialId : geo::Id{0};
        if (currentFront == materialId) {
            continue;  // already at this front value -- not a change
        }

        if (materialId == 0) {
            // exists is guaranteed true here: otherwise currentFront would
            // already equal materialId (0) and the check above would have
            // skipped this ref.
            if (it->second.backMaterialId == 0) {
                assignments_.erase(it);
            } else {
                it->second.frontMaterialId = 0;
            }
        } else if (exists) {
            it->second.frontMaterialId = materialId;
        } else {
            assignments_.emplace(ref, MaterialAssignment{materialId, 0});
        }
        changed = true;
    }

    if (changed) {
        send(events::MaterialsChanged{});
    }
    return changed;
}

void MaterialRepository::setActive(geo::Id id) {
    activeMaterialId_ = id;
    // No event -- see this method's own header comment.
}

bool MaterialRepository::setTexture(geo::Id id, std::string assetHash, double tileW, double tileH) {
    Material* m = findMaterial(id);
    if (m == nullptr || assetHash.empty()) {
        return false;  // unknown material id, or an empty hash (use clearTexture instead)
    }
    if (m->assetHash == assetHash && m->tileW == tileW && m->tileH == tileH) {
        return false;  // every field already matches -- no-op
    }
    m->assetHash = std::move(assetHash);
    m->tileW = tileW;
    m->tileH = tileH;
    send(events::MaterialsChanged{});
    return true;
}

bool MaterialRepository::clearTexture(geo::Id id) {
    Material* m = findMaterial(id);
    if (m == nullptr || m->assetHash.empty()) {
        return false;  // unknown material id, or already untextured -- no-op
    }
    m->assetHash.clear();
    m->tileW = 1.0;
    m->tileH = 1.0;
    send(events::MaterialsChanged{});
    return true;
}

bool MaterialRepository::setUvTransform(events::EntityRef ref, events::UvTransform transform) {
    auto it = assignments_.find(ref);
    if (it == assignments_.end()) {
        return false;  // nothing painted here -- no assignment to attach a UV transform to
    }
    const events::UvTransform& current = it->second.uvTransform;
    if (current.offsetU == transform.offsetU && current.offsetV == transform.offsetV &&
        current.rotationRad == transform.rotationRad && current.scaleU == transform.scaleU &&
        current.scaleV == transform.scaleV) {
        return false;  // every field already matches -- no-op
    }
    it->second.uvTransform = transform;
    send(events::MaterialsChanged{});
    return true;
}

const std::vector<Material>& MaterialRepository::materials() const {
    return materials_;
}

const Material* MaterialRepository::material(geo::Id id) const {
    return findMaterial(id);
}

const std::unordered_map<events::EntityRef, MaterialAssignment>& MaterialRepository::assignments() const {
    return assignments_;
}

const MaterialAssignment* MaterialRepository::assignment(events::EntityRef ref) const {
    auto it = assignments_.find(ref);
    return it != assignments_.end() ? &it->second : nullptr;
}

geo::Id MaterialRepository::activeMaterialId() const {
    return activeMaterialId_;
}

void MaterialRepository::clearForRestore() {
    materials_.clear();
    assignments_.clear();
    activeMaterialId_ = 0;
    nextId_ = 1;
}

bool MaterialRepository::restoreMaterial(Material material) {
    if (material.id == 0 || findMaterial(material.id) != nullptr) {
        return false;  // invalid id, or already used by a material already restored
    }
    if (material.id >= nextId_) {
        nextId_ = material.id + 1;
    }
    materials_.push_back(std::move(material));
    return true;
}

bool MaterialRepository::restoreAssignment(events::EntityRef ref, geo::Id front, geo::Id back) {
    return restoreAssignment(ref, front, back, events::UvTransform{});
}

bool MaterialRepository::restoreAssignment(events::EntityRef ref, geo::Id front, geo::Id back,
                                       events::UvTransform uvTransform) {
    if ((front != 0 && findMaterial(front) == nullptr) || (back != 0 && findMaterial(back) == nullptr)) {
        return false;  // unknown material id -- the reader's own validation should have caught this
    }
    assignments_[ref] = MaterialAssignment{front, back, uvTransform};
    return true;
}

}  // namespace plnr::agent
