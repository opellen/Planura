#include "agent/tag_store.h"

#include <string>
#include <utility>

namespace plnr::agent {

TagStore::TagStore() : Agent(std::string(kTagStoreName)) {
    tags_.push_back(Tag{kUntaggedTagId, "Untagged", true});
}

Tag* TagStore::findTag(std::uint64_t id) {
    for (Tag& tag : tags_) {
        if (tag.id == id) return &tag;
    }
    return nullptr;
}

std::uint64_t TagStore::createTag(std::string name) {
    const std::uint64_t id = nextId_++;
    if (name.empty()) {
        name = "Tag " + std::to_string(id);
    }
    tags_.push_back(Tag{id, std::move(name), true});
    send(events::TagsChanged{});
    return id;
}

bool TagStore::assignTag(const std::vector<events::EntityRef>& refs, std::uint64_t tagId) {
    if (findTag(tagId) == nullptr) {
        return false;  // unknown tag id -- whole call rejected
    }

    bool changed = false;
    for (const events::EntityRef& ref : refs) {
        if (tagOf(ref) == tagId) {
            continue;  // already on this tag -- not a change
        }
        if (tagId == kUntaggedTagId) {
            assignments_.erase(ref);
        } else {
            assignments_[ref] = tagId;
        }
        changed = true;
    }

    if (changed) {
        send(events::TagsChanged{});
    }
    return changed;
}

bool TagStore::setTagVisible(std::uint64_t tagId, bool visible) {
    Tag* tag = findTag(tagId);
    if (tag == nullptr || tag->visible == visible) {
        return false;  // unknown tag id, or already at this value -- no-op
    }

    tag->visible = visible;
    send(events::TagsChanged{});
    return true;
}

const std::vector<Tag>& TagStore::tags() const {
    return tags_;
}

std::uint64_t TagStore::tagOf(events::EntityRef ref) const {
    auto it = assignments_.find(ref);
    return it != assignments_.end() ? it->second : kUntaggedTagId;
}

bool TagStore::isEntityVisible(events::EntityRef ref) const {
    const std::uint64_t tagId = tagOf(ref);
    for (const Tag& tag : tags_) {
        if (tag.id == tagId) return tag.visible;
    }
    return true;  // tagOf() always resolves to a known tag -- unreachable
}

const std::unordered_map<events::EntityRef, std::uint64_t>& TagStore::assignments() const {
    return assignments_;
}

void TagStore::clearForRestore() {
    tags_.clear();
    assignments_.clear();
    nextId_ = kUntaggedTagId + 1;
}

bool TagStore::restoreTag(Tag tag) {
    if (tag.id == 0 || findTag(tag.id) != nullptr) {
        return false;  // invalid id, or already used by a tag already restored
    }
    if (tag.id >= nextId_) {
        nextId_ = tag.id + 1;
    }
    tags_.push_back(std::move(tag));
    return true;
}

bool TagStore::restoreAssignment(events::EntityRef ref, std::uint64_t tagId) {
    if (findTag(tagId) == nullptr) {
        return false;  // unknown tag id -- the reader's own validation should have caught this
    }
    assignments_[ref] = tagId;
    return true;
}

}  // namespace plnr::agent
