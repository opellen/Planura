#include <ordo/qt/qt_bridge.h>

#include <ordo/core/version.h>

namespace ordo::qt {

bool isBridgeAvailable() {
    // Touch ordo::core to prove the link between the two libraries works.
    return ordo::core::versionString() != nullptr;
}

}  // namespace ordo::qt
