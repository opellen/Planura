#include "agent/annotation_store.h"

#include <algorithm>
#include <string>

namespace plnr::agent {

AnnotationStore::AnnotationStore() : Agent(std::string(kAnnotationStoreName)) {}

Dimension* AnnotationStore::findDimension(geo::Id id) {
    for (Dimension& d : dimensions_) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

TextNote* AnnotationStore::findText(geo::Id id) {
    for (TextNote& t : texts_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

geo::Id AnnotationStore::addDimension(geo::Id vertexA, geo::Id vertexB, geo::Vec3 offsetDir, double offset,
                                       const geo::Model& model) {
    const geo::Id id = nextId_++;
    const geo::Vertex* va = model.vertex(vertexA);
    const geo::Vertex* vb = model.vertex(vertexB);
    Dimension dim;
    dim.id = id;
    dim.vertexA = vertexA;
    dim.vertexB = vertexB;
    dim.offsetDir = offsetDir;
    dim.offset = offset;
    dim.associated = (va != nullptr && vb != nullptr);
    dim.lastA = va ? va->pos : geo::Vec3{};
    dim.lastB = vb ? vb->pos : geo::Vec3{};
    dimensions_.push_back(dim);
    send(events::AnnotationsChanged{});
    return id;
}

geo::Id AnnotationStore::addScreenText(double x, double y, std::string text) {
    const geo::Id id = nextId_++;
    TextNote note;
    note.id = id;
    note.screenFixed = true;
    note.screenX = x;
    note.screenY = y;
    note.text = std::move(text);
    texts_.push_back(std::move(note));
    send(events::AnnotationsChanged{});
    return id;
}

geo::Id AnnotationStore::addLeaderText(geo::Vec3 anchor, std::optional<events::EntityRef> target, std::string text) {
    const geo::Id id = nextId_++;
    TextNote note;
    note.id = id;
    note.screenFixed = false;
    note.worldAnchor = anchor;
    note.leaderTarget = target;
    note.text = std::move(text);
    texts_.push_back(std::move(note));
    send(events::AnnotationsChanged{});
    return id;
}

bool AnnotationStore::setText(geo::Id id, std::string text) {
    if (Dimension* d = findDimension(id)) {
        if (d->overrideText == text) return false;  // no-op
        d->overrideText = std::move(text);
        send(events::AnnotationsChanged{});
        return true;
    }
    if (TextNote* t = findText(id)) {
        if (t->text == text) return false;  // no-op
        t->text = std::move(text);
        send(events::AnnotationsChanged{});
        return true;
    }
    return false;  // unknown id
}

bool AnnotationStore::remove(geo::Id id) {
    if (const auto it = std::find_if(dimensions_.begin(), dimensions_.end(), [id](const Dimension& d) { return d.id == id; });
        it != dimensions_.end()) {
        dimensions_.erase(it);
        send(events::AnnotationsChanged{});
        return true;
    }
    if (const auto it = std::find_if(texts_.begin(), texts_.end(), [id](const TextNote& t) { return t.id == id; });
        it != texts_.end()) {
        texts_.erase(it);
        send(events::AnnotationsChanged{});
        return true;
    }
    return false;  // unknown id
}

bool AnnotationStore::removeAll() {
    if (dimensions_.empty() && texts_.empty()) return false;  // no-op
    dimensions_.clear();
    texts_.clear();
    send(events::AnnotationsChanged{});
    return true;
}

bool AnnotationStore::refreshAssociations(const geo::Model& model) {
    bool changed = false;
    for (Dimension& dim : dimensions_) {
        const geo::Vertex* va = model.vertex(dim.vertexA);
        const geo::Vertex* vb = model.vertex(dim.vertexB);
        if (va != nullptr && vb != nullptr) {
            if (!dim.associated || !geo::almostEqual(dim.lastA, va->pos) || !geo::almostEqual(dim.lastB, vb->pos)) {
                changed = true;
            }
            dim.lastA = va->pos;
            dim.lastB = vb->pos;
            dim.associated = true;
        } else {
            if (dim.associated) changed = true;
            dim.associated = false;
            // lastA/lastB deliberately left untouched -- "keep last-known
            // positions" (see Dimension's comment).
        }
    }
    if (changed) send(events::AnnotationsChanged{});
    return changed;
}

const std::vector<Dimension>& AnnotationStore::dimensions() const {
    return dimensions_;
}

const std::vector<TextNote>& AnnotationStore::texts() const {
    return texts_;
}

void AnnotationStore::clearForRestore() {
    dimensions_.clear();
    texts_.clear();
    nextId_ = 1;
}

bool AnnotationStore::restoreDimension(Dimension dim) {
    if (dim.id == 0 || findDimension(dim.id) != nullptr || findText(dim.id) != nullptr) {
        return false;  // invalid id, or already used (shared id space with texts_)
    }
    if (dim.id >= nextId_) {
        nextId_ = dim.id + 1;
    }
    dimensions_.push_back(std::move(dim));
    return true;
}

bool AnnotationStore::restoreTextNote(TextNote text) {
    if (text.id == 0 || findDimension(text.id) != nullptr || findText(text.id) != nullptr) {
        return false;  // invalid id, or already used (shared id space with dimensions_)
    }
    if (text.id >= nextId_) {
        nextId_ = text.id + 1;
    }
    texts_.push_back(std::move(text));
    return true;
}

}  // namespace plnr::agent
