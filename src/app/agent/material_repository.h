#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <geo/model.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kMaterialRepositoryName = "materials";

// One material: a name plus flat RGB color and opacity (0-1). PBR is out of scope for v1.
// Materials are never deleted in v1 -- ids are monotonic and never reused.
struct Material {
    geo::Id id{};
    std::string name;
    double r{0.5};
    double g{0.5};
    double b{0.5};
    double opacity{1.0};  // 0 (fully transparent) -- 1 (fully opaque)

    // assetHash empty means untextured; non-empty names a blob in agent::AssetRepository (not
    // validated here). tileW/tileH: unitless model-space.
    std::string assetHash;
    double tileW{1.0};
    double tileH{1.0};
};

// One entity's material assignment. frontMaterialId covers a Face's front side or an Instance's
// paint slot; backMaterialId covers a Face's back side only (unused for an Instance ref).
// 0 in either field means unpainted.
struct MaterialAssignment {
    geo::Id frontMaterialId{};
    geo::Id backMaterialId{};

    // The FRONT slot's per-face UV transform; identity means "no transform" (isIdentityUvTransform).
    // Meaningless for the back slot or an Instance ref, but stored verbatim regardless.
    events::UvTransform uvTransform;
};

// Owns the in-model material list plus entity->material assignments. Mutators send
// events::MaterialsChanged on actual change, EXCEPT setActive() (never dispatches).
class MaterialRepository : public ordo::core::Agent {
public:
    MaterialRepository();

    // Creates a new material with the given color/opacity; name empty auto-names "Material N".
    geo::Id create(std::string name, double r, double g, double b, double opacity);

    // Edits an existing material's name/color/opacity in place. Unknown id, or a call whose every
    // field already matches the current value, is a no-op.
    bool edit(geo::Id id, std::string name, double r, double g, double b, double opacity);

    // Sets every ref's FRONT material slot to materialId (0 clears; back slot untouched). Unknown
    // materialId (!= 0) rejects the whole call. Clearing an entry whose back slot is also 0 erases
    // the map entry entirely.
    bool paint(const std::vector<events::EntityRef>& refs, geo::Id materialId);

    // Sets the Materials panel's active-material selection. Purely transient: never saved to
    // .plr, never marks dirty, and dispatches NOTHING (unlike every other mutator here).
    void setActive(geo::Id id);

    // Sets id's texture fields; assetHash must be non-empty (use clearTexture to untexture).
    // Unknown id, or no actual field change, is a no-op. Does NOT validate assetHash.
    bool setTexture(geo::Id id, std::string assetHash, double tileW, double tileH);

    // Clears id's texture (assetHash empty, tileW/tileH reset to 1.0). Unknown id, or already
    // untextured, is a no-op. The asset blob itself is untouched.
    bool clearTexture(geo::Id id);

    // Sets ref's per-face UV transform (front slot only). A ref with no assignment at all is a
    // no-op. Passing the identity value is an ordinary call that clears it back to
    // invisible-in-file (no separate clear method).
    bool setUvTransform(events::EntityRef ref, events::UvTransform transform);

    const std::vector<Material>& materials() const;

    // nullptr if id doesn't name a known material.
    const Material* material(geo::Id id) const;

    // Full ref -> assignment map, e.g. for the .plr writer's materialAssignments array.
    const std::unordered_map<events::EntityRef, MaterialAssignment>& assignments() const;

    // Raw single-ref lookup; nullptr if ref carries no assignment at all. NOT
    // instance-inheritance-aware -- the presenter composes that from this plus live scene context.
    const MaterialAssignment* assignment(events::EntityRef ref) const;

    geo::Id activeMaterialId() const;

    // -- Restore API -------------------------------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Empties materials_/assignments_, resets nextId_, AND resets activeMaterialId_ to 0 -- unlike
    // every other Restore API here, this also clears transient UI state.
    void clearForRestore();

    // Inserts material verbatim and folds nextId_'s recovery in (bumps to id + 1 if higher).
    // Returns false if material.id is 0 or already used.
    bool restoreMaterial(Material material);

    // Inserts ref -> {front, back} verbatim. Returns false if front/back is nonzero and doesn't
    // name an already-restored material. Forwards to the 4-arg overload at the identity UV transform.
    bool restoreAssignment(events::EntityRef ref, geo::Id front, geo::Id back);

    // Same as above, plus ref's per-face UV transform (never itself validated).
    bool restoreAssignment(events::EntityRef ref, geo::Id front, geo::Id back, events::UvTransform uvTransform);

private:
    Material* findMaterial(geo::Id id);
    const Material* findMaterial(geo::Id id) const;

    std::vector<Material> materials_;
    std::unordered_map<events::EntityRef, MaterialAssignment> assignments_;
    geo::Id activeMaterialId_{};  // transient -- see setActive's own comment
    geo::Id nextId_ = 1;
};

}  // namespace plnr::agent
