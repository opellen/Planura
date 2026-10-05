#pragma once

#include <string_view>
#include <vector>

#include <geo/infer.h>
#include <geo/vec3.h>
#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kGuideStoreName = "guides";

// One guide: either an infinite guide LINE (isLine == true, point + unit dir) or a single guide
// POINT (isLine == false, point alone; dir unused).
struct Guide {
    geo::Id id{};
    bool isLine{};
    geo::Vec3 point;  // line: any point on the line. point guide: the point itself.
    geo::Vec3 dir;    // line only, unit length; unused (zero) for a point guide.
    bool hidden{};
};

// Owns the guide-line/guide-point set. Mutators send events::GuidesChanged only when state
// actually changes. Guides are NOT part of geo::Model and never appear in pick() -- inference-only
// targets.
class GuideStore : public ordo::core::Agent {
public:
    GuideStore();

    // Adds an infinite guide line through point along dir; dir is normalized here (a near-zero
    // dir yields a zero-vector record rather than rejecting the call). Returns the new guide's id.
    geo::Id addGuideLine(geo::Vec3 point, geo::Vec3 dir);

    // Adds a single guide point at pos. Returns the new guide's id.
    geo::Id addGuidePoint(geo::Vec3 pos);

    // Erases one guide (line or point) by id. Unknown id is a no-op.
    bool erase(geo::Id id);

    // Sets id's hidden flag. Unknown id, or a value equal to the current flag, is a no-op.
    bool setHidden(geo::Id id, bool hidden);

    // Unknown id reads as not hidden (false) -- callers needing to tell "doesn't exist" apart
    // from "visible" should use guides() or erase()'s own return instead.
    bool hidden(geo::Id id) const;

    // Erases every guide. No-op when already empty.
    bool deleteAll();

    const std::vector<Guide>& guides() const;

    // Visible-only line/point views, shaped for InferenceContext::guideLines/guidePoints.
    // Hidden guides are excluded (a hidden guide stops contributing snaps).
    std::vector<geo::GuideLineData> lineView() const;
    std::vector<geo::GuidePointData> pointView() const;

    // -- Restore API -------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Empties guides_ and resets nextId_ back to its ctor value.
    void clearForRestore();

    // Inserts guide verbatim (no re-normalizing dir, unlike addGuideLine) and folds nextId_'s
    // recovery in. Returns false if guide.id is 0 or already used by a guide already restored.
    bool restoreGuide(Guide guide);

private:
    Guide* find(geo::Id id);
    const Guide* find(geo::Id id) const;

    std::vector<Guide> guides_;
    geo::Id nextId_ = 1;
};

}  // namespace plnr::agent
