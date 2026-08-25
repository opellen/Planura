#include <ordo/qt/presenter_host.h>

namespace ordo::qt {

PresenterHost::~PresenterHost() {
    clear();
}

void PresenterHost::clear() {
    // LIFO: onRemove() newest first, then destroy in the same order.
    for (auto it = presenters_.rbegin(); it != presenters_.rend(); ++it) {
        (*it)->onRemove();
    }
    presenters_.clear();
}

}  // namespace ordo::qt
