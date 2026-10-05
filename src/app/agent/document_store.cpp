#include "agent/document_store.h"

#include <utility>

namespace plnr::agent {

DocumentStore::DocumentStore() : Agent(std::string(kDocumentStoreName)) {}

void DocumentStore::markDirty() {
    ++revision_;
    if (dirty_) return;  // already dirty -- no-op, no event
    dirty_ = true;
    send(events::DocumentStateChanged{});
}

void DocumentStore::setSaved(std::string path) {
    filePath_ = std::move(path);
    dirty_ = false;
    send(events::DocumentStateChanged{});
}

void DocumentStore::setPathKeepDirty(std::string path) {
    filePath_ = std::move(path);
    send(events::DocumentStateChanged{});
}

std::uint64_t DocumentStore::revision() const {
    return revision_;
}

void DocumentStore::resetNew() {
    filePath_.clear();
    dirty_ = false;
    send(events::DocumentStateChanged{});
}

const std::string& DocumentStore::filePath() const {
    return filePath_;
}

bool DocumentStore::dirty() const {
    return dirty_;
}

const std::string& DocumentStore::units() const {
    return units_;
}

}  // namespace plnr::agent
