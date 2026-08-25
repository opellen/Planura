#include <ordo/core/app_kernel.h>

namespace ordo::core {

AppKernel::AppKernel() : storeContext_(KernelKey{}, *this, dispatcher_) {}

Dispatcher& AppKernel::dispatcher() {
    return dispatcher_;
}

void AppKernel::registerAgent(std::shared_ptr<Agent> agent) {
    Agent& ref = *agent;
    const std::string key = ref.name();
    auto existing = stores_.find(key);
    if (existing != stores_.end()) {
        existing->second->onRemove();
        existing->second->setContext(KernelKey{}, nullptr);
    }
    stores_[key] = agent;
    ref.setContext(KernelKey{}, &storeContext_);
    ref.onRegister();
}

std::shared_ptr<Agent> AppKernel::agent(std::string_view name) {
    auto it = stores_.find(std::string(name));
    return it == stores_.end() ? nullptr : it->second;
}

bool AppKernel::removeAgent(std::string_view name) {
    auto it = stores_.find(std::string(name));
    if (it == stores_.end()) {
        return false;
    }
    it->second->onRemove();
    it->second->setContext(KernelKey{}, nullptr);
    stores_.erase(it);
    return true;
}

}  // namespace ordo::core
