#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <geo/model.h>
#include <geo/vec3.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kAnnotationStoreName = "annotations";

// One linear dimension, anchored to two model VERTEX ids (MVP: midpoint/radius dimensions deferred).
// offsetDir/offset place the line parallel to A-B, offset along offsetDir.
// overrideText empty renders live |A-B|; non-empty permanently breaks the dynamic link.
// lastA/lastB freeze at their last value once association breaks.
struct Dimension {
    geo::Id id{};
    geo::Id vertexA{};
    geo::Id vertexB{};
    geo::Vec3 offsetDir;
    double offset{};
    std::string overrideText;
    bool associated{true};
    geo::Vec3 lastA;
    geo::Vec3 lastB;
};

// One text note. screenFixed true: SCREEN text, fixed at widget-pixel (screenX,screenY).
// False: LEADER text, worldAnchor is the resolved world point; leaderTarget optionally names the
// entity but is NOT dynamically tracked (fixed at creation).
struct TextNote {
    geo::Id id{};
    bool screenFixed{};
    double screenX{};
    double screenY{};
    geo::Vec3 worldAnchor;
    std::optional<events::EntityRef> leaderTarget;
    std::string text;
};

// Owns the dimension/text-note set. Ids share ONE monotonic counter across both collections.
// refreshAssociations() re-syncs dimensions against a model; called by a Command on every
// GeometryChanged (Agents can't self-subscribe).
class AnnotationStore : public ordo::core::Agent {
public:
    AnnotationStore();

    // Adds a dimension between vertexA/vertexB, offset by `offset` along unit offsetDir.
    // An unresolved vertex id starts non-associated rather than rejecting the call.
    geo::Id addDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset, const geo::Model& model);

    // Adds a screen text note fixed at widget-pixel position (x, y).
    geo::Id addScreenText(double x, double y, std::string text);

    // Adds a leader text note anchored at world point anchor, optionally naming the entity it points at.
    geo::Id addLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text);

    // Replaces id's text: a Dimension's overrideText (breaking its dynamic distance-label link)
    // or a TextNote's own text. Unknown id, or a value equal to the current text, is a no-op.
    bool setText(geo::Id id, std::string text);

    // Removes one annotation (dimension or text note) by id. Unknown id is a no-op.
    bool remove(geo::Id id);

    // Removes every annotation. No-op when already empty.
    bool removeAll();

    // Re-checks every dimension's vertexA/vertexB against model: both resolve sets associated=true
    // and updates lastA/lastB; either missing sets associated=false and freezes lastA/lastB.
    bool refreshAssociations(const geo::Model& model);

    const std::vector<Dimension>& dimensions() const;
    const std::vector<TextNote>& texts() const;

    // -- Restore API -------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Empties dimensions_ and texts_ and resets nextId_ back to its ctor value.
    void clearForRestore();

    // Inserts dim verbatim (associated/lastA/lastB should already be resolved, or call
    // refreshAssociations() after). False if dim.id is 0 or already used in either collection.
    bool restoreDimension(Dimension dim);

    // Same contract as restoreDimension, for texts_ -- shares the same id space and nextId_ recovery.
    bool restoreTextNote(TextNote text);

private:
    Dimension* findDimension(geo::Id id);
    TextNote* findText(geo::Id id);

    std::vector<Dimension> dimensions_;
    std::vector<TextNote> texts_;
    geo::Id nextId_ = 1;
};

}  // namespace plnr::agent
