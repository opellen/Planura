#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <ordo/qt/presenter.h>

namespace ordo::qt {

// Owns presenters and drives their register/remove lifecycle -- the modern
// shape of PureMVC's registerMediator: registration IS construction into the
// host. Removal is LIFO (newest first), mirroring construction order.
class PresenterHost {
public:
    PresenterHost() = default;
    ~PresenterHost();  // = clear()

    PresenterHost(const PresenterHost&) = delete;
    PresenterHost& operator=(const PresenterHost&) = delete;

    // Constructs PresenterT in place, takes ownership, calls onRegister().
    // Returns a non-owning pointer valid until clear()/destruction.
    template <typename PresenterT, typename... Args>
    PresenterT* add(Args&&... args) {
        auto presenter = std::make_unique<PresenterT>(std::forward<Args>(args)...);
        PresenterT* raw = presenter.get();
        presenters_.push_back(std::move(presenter));
        raw->onRegister();
        return raw;
    }

    // LIFO: onRemove() then destroy, newest first.
    void clear();

private:
    std::vector<std::unique_ptr<Presenter>> presenters_;
};

}  // namespace ordo::qt
