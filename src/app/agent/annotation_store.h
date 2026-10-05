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

// One linear dimension between two model vertices, drawn parallel to A-B and shifted by offset along offsetDir.
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

// One text note. screenFixed: SCREEN text at widget-pixel (screenX,screenY). Otherwise LEADER text at
// worldAnchor; leaderTarget only names the entity and is NOT tracked (fixed at creation).
struct TextNote {
    geo::Id id{};
    bool screenFixed{};
    double screenX{};
    double screenY{};
    geo::Vec3 worldAnchor;
    std::optional<events::EntityRef> leaderTarget;
    std::string text;
};

// Dimension and text ids share ONE monotonic counter.
// A Command must call refreshAssociations() on every GeometryChanged (Agents can't self-subscribe).
class AnnotationStore : public ordo::core::Agent {
public:
    AnnotationStore();

    // offsetDir is unit. An unresolved vertex id starts non-associated rather than rejecting.
    geo::Id addDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset, const geo::Model& model);

    geo::Id addScreenText(double x, double y, std::string text);

    geo::Id addLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text);

    // Sets a Dimension's overrideText (breaking its distance-label link) or a TextNote's text.
    // Unknown id or unchanged text is a no-op.
    bool setText(geo::Id id, std::string text);

    // Unknown id is a no-op.
    bool remove(geo::Id id);

    bool removeAll();

    // Both vertices resolve: associated=true, lastA/lastB updated. Either missing: associated=false, lastA/lastB frozen.
    bool refreshAssociations(const geo::Model& model);

    const std::vector<Dimension>& dimensions() const;
    const std::vector<TextNote>& texts() const;

    // -- Restore API: file loader / snapshot restore only; no notification.
    void clearForRestore();

    // Inserts dim verbatim (associated/lastA/lastB already resolved, or call refreshAssociations() after).
    // False if dim.id is 0 or already used in either collection.
    bool restoreDimension(Dimension dim);

    // Same contract as restoreDimension; shares the id space.
    bool restoreTextNote(TextNote text);

private:
    Dimension* findDimension(geo::Id id);
    TextNote* findText(geo::Id id);

    std::vector<Dimension> dimensions_;
    std::vector<TextNote> texts_;
    geo::Id nextId_ = 1;
};

}  // namespace plnr::agent
