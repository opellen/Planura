#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ordo/core/agent.h>

#include "agent/events.h"

namespace plnr::agent {

inline constexpr std::string_view kTagStoreName = "tags";

// Id of the always-present default tag (the reference modeler's "Untagged" layer). Every
// entity not explicitly assigned to another tag belongs here implicitly --
// TagStore::assignments_ never carries an entry for it.
inline constexpr std::uint64_t kUntaggedTagId = 1;

// One tag (the reference modeler "layer"): an id, a display name, and a visibility flag.
struct Tag {
    std::uint64_t id{};
    std::string name;
    bool visible{true};
};

// Owns the tag list (the reference modeler "Layers") and which tag each entity is assigned to. Untagged
// (id kUntaggedTagId) always exists and is never removable. Mutators send events::TagsChanged
// only when a call actually changed state.
class TagStore : public ordo::core::Agent {
public:
    TagStore();

    // Creates a new tag; name empty auto-names it "Tag N". Returns the new tag's id.
    std::uint64_t createTag(std::string name);

    // Assigns every ref in refs to tagId. Unknown tagId rejects the whole call, no-op. Assigning
    // to kUntaggedTagId erases refs' map entries (absent = Untagged); any other tag sets them.
    // A ref already on tagId doesn't count as a change.
    bool assignTag(const std::vector<events::EntityRef>& refs, std::uint64_t tagId);

    // Sets tagId's visible flag. Unknown tagId, or a value equal to the current one, is a no-op.
    bool setTagVisible(std::uint64_t tagId, bool visible);

    const std::vector<Tag>& tags() const;

    // The tag ref is assigned to, or kUntaggedTagId if unmapped.
    std::uint64_t tagOf(events::EntityRef ref) const;

    // Whether ref's tag is currently visible (tagOf(ref)'s Tag::visible).
    bool isEntityVisible(events::EntityRef ref) const;

    // Read accessor for the full ref -> tagId map (only entities explicitly moved off Untagged).
    const std::unordered_map<events::EntityRef, std::uint64_t>& assignments() const;

    // -- Restore API -------------------------------
    // File loader / snapshot restore only: plain data manipulation, no notification.

    // Empties tags_ (including the ctor-seeded Untagged entry) and assignments_, resets nextId_.
    // The file format always contains an explicit Untagged record, replayed via restoreTag like
    // any other tag, so this leaves nothing seeded, unlike the constructor.
    void clearForRestore();

    // Inserts tag verbatim and folds nextId_'s recovery in (bumps to tag.id + 1 if higher).
    // Returns false if tag.id is 0 or already used by a tag already restored.
    bool restoreTag(Tag tag);

    // Inserts ref -> tagId verbatim into assignments_ (no dedup check). Returns false if tagId
    // doesn't name a tag already restored via restoreTag.
    bool restoreAssignment(events::EntityRef ref, std::uint64_t tagId);

private:
    // nullptr if id doesn't name a known tag.
    Tag* findTag(std::uint64_t id);

    std::vector<Tag> tags_;

    // Entities explicitly moved off Untagged, keyed by never-reused geo::Model ids -- dead
    // entries go stale harmlessly (never pruned).
    std::unordered_map<events::EntityRef, std::uint64_t> assignments_;

    std::uint64_t nextId_ = kUntaggedTagId + 1;
};

}  // namespace plnr::agent
